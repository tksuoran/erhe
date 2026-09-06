#include "erhe_scene/node.hpp"
#include "erhe_scene/node_attachment.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/scene_host.hpp"
#include "erhe_scene/scene_log.hpp"
#include "erhe_utility/bit_helpers.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <cmath>

namespace erhe::scene {

namespace {

// A transform whose world transform is the identity (the usual scene-root case) lets a
// child's world transform be stored as parent-relative verbatim - no matrix round-trip and
// no glm::decompose, which is numerically unstable for small scales (it can drift one axis,
// e.g. 0.001 -> 0.4, across the repeated set/decompose round-trips that parenting forces).
[[nodiscard]] auto is_identity_transform(const Trs_transform& t) -> bool
{
    // Matrix comparison instead of TRS components: the TRS getters would
    // force a deferred glm::decompose, and this is called on per-frame write
    // paths (physics writeback, update_world_from_node).
    constexpr float eps = 1e-5f;
    const glm::mat4& m = t.get_matrix();
    const glm::mat4 identity{1.0f};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (std::abs(m[column][row] - identity[column][row]) >= eps) {
                return false;
            }
        }
    }
    return true;
}

}

using namespace erhe;

namespace {

using erhe::property::Dependency_object;
using erhe::property::Property;
using erhe::property::Property_bridge;
using erhe::property::Property_flags;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;
using erhe::property::Property_value;

constexpr uint32_t c_transform_flags = Property_flags::serialize | Property_flags::affects_transform;

template <typename T>
auto make_transform_bridge(
    T (Trs_transform::*getter)() const,
    void (Trs_transform::*setter)(T)
) -> Property_bridge
{
    return Property_bridge{
        .get = [getter](const Dependency_object& object) -> Property_value {
            const Xformable& node = static_cast<const Xformable&>(object);
            return (node.node_data.transforms.parent_from_node.*getter)();
        },
        .set = [setter](Dependency_object& object, const Property_value& value) {
            Xformable& node = static_cast<Xformable&>(object);
            (node.node_data.transforms.parent_from_node.*setter)(std::get<T>(value));
            node.update_world_from_node();
            node.handle_transform_update(Node_transforms::get_next_serial());
        }
    };
}

} // anonymous namespace

const Property<glm::vec3> Xformable::translation_property = Property<glm::vec3>::register_property(
    "translation", Xformable::property_owner_type(),
    Property_metadata{
        .default_value = glm::vec3{0.0f, 0.0f, 0.0f},
        .flags         = c_transform_flags,
        .ui            = Property_ui{.step = 0.01f, .tooltip = "Position relative to the parent node", .label = "Translation"},
        .bridge        = make_transform_bridge<glm::vec3>(&Trs_transform::get_translation, &Trs_transform::set_translation)
    }
);
const Property<glm::quat> Xformable::rotation_property = Property<glm::quat>::register_property(
    "rotation", Xformable::property_owner_type(),
    Property_metadata{
        .default_value = glm::quat{1.0f, 0.0f, 0.0f, 0.0f},
        .flags         = c_transform_flags,
        .ui            = Property_ui{.step = 0.5f, .tooltip = "Rotation relative to the parent node (edited as Euler degrees)", .label = "Rotation"},
        .bridge        = make_transform_bridge<glm::quat>(&Trs_transform::get_rotation, &Trs_transform::set_rotation)
    }
);
const Property<glm::vec3> Xformable::scale_property = Property<glm::vec3>::register_property(
    "scale", Xformable::property_owner_type(),
    Property_metadata{
        .default_value = glm::vec3{1.0f, 1.0f, 1.0f},
        .flags         = c_transform_flags,
        .ui            = Property_ui{.step = 0.01f, .tooltip = "Scale relative to the parent node", .label = "Scale"},
        .bridge        = make_transform_bridge<glm::vec3>(&Trs_transform::get_scale, &Trs_transform::set_scale)
    }
);

// Computed world transform components (D26)
const Property<glm::vec3> Xformable::world_translation_property = Property<glm::vec3>::register_computed(
    "world_translation", Xformable::property_owner_type(),
    [](const Dependency_object& object) -> Property_value {
        return static_cast<const Xformable&>(object).world_from_node_transform().get_translation();
    },
    Property_metadata{
        .flags = Property_flags::none,
        .ui    = Property_ui{.group = "World", .tooltip = "Position in world space (computed)", .label = "Translation"}
    }
);
const Property<glm::quat> Xformable::world_rotation_property = Property<glm::quat>::register_computed(
    "world_rotation", Xformable::property_owner_type(),
    [](const Dependency_object& object) -> Property_value {
        return static_cast<const Xformable&>(object).world_from_node_transform().get_rotation();
    },
    Property_metadata{
        .flags = Property_flags::none,
        .ui    = Property_ui{.group = "World", .tooltip = "Rotation in world space (computed, shown as Euler degrees)", .label = "Rotation"}
    }
);
const Property<glm::vec3> Xformable::world_scale_property = Property<glm::vec3>::register_computed(
    "world_scale", Xformable::property_owner_type(),
    [](const Dependency_object& object) -> Property_value {
        return static_cast<const Xformable&>(object).world_from_node_transform().get_scale();
    },
    Property_metadata{
        .flags = Property_flags::none,
        .ui    = Property_ui{.group = "World", .tooltip = "Scale in world space (computed)", .label = "Scale"}
    }
);

uint64_t Node_transforms::s_global_update_serial = 0;

auto Node_transforms::get_current_serial() -> uint64_t
{
    return s_global_update_serial;
}

auto Node_transforms::get_next_serial() -> uint64_t
{
    return ++s_global_update_serial;
}

Node_data::Node_data() = default;

Node_data::Node_data(const Node_data& src, for_clone)
    : transforms{src.transforms}
    , host      {nullptr} // clone is created as not attached to anything
{
    // Attachments are handled in Xformable(const Xformable&)
}

Xformable::Xformable() = default;
Xformable::Xformable(const Xformable&) { ERHE_FATAL("TODO"); }
Xformable& Xformable::operator=(const Xformable&) { ERHE_FATAL("TODO"); }

Xformable::Xformable(const std::string_view name)
    : Item{name}
{
}

Xformable::Xformable(const Xformable& src, for_clone)
    : Item     {src, erhe::for_clone{}}
    , node_data{src.node_data, erhe::for_clone{}}
{
    for (const auto& src_attachment : src.get_attachments()) {
        auto attachment_clone_item = src_attachment->clone_attachment();
        auto attachment_clone = std::dynamic_pointer_cast<Node_attachment>(attachment_clone_item);
        if (attachment_clone) {
            attach(attachment_clone);
        }
    }
}

Xformable::~Xformable() noexcept
{
    node_sanity_check(true);

    log->trace(
        "~Xformable '{}' depth = {} child count = {}",
        get_name(),
        get_depth(),
        get_child_count()
    );

    while (!node_data.attachments.empty()) {
        // Causes trigger Xformable::handle_remove_attachment() calls to this Xformable
        node_data.attachments.back()->set_node(nullptr);
    }
}

auto Xformable::shared_node_from_this() -> std::shared_ptr<Xformable>
{
    return std::static_pointer_cast<Xformable>(shared_from_this());
}

auto Xformable::get_parent_node() const -> std::shared_ptr<Xformable>
{
    // The nearest Xformable ancestor, not simply the parent: a prim outside
    // Xformable - a Scope - has no transform, so a transform composes with the
    // first Xformable above it and passes through the prims that have none
    // (doc/usd-compatibility-plan.md C5). The walk steps only over those
    // transformless prims, so on a tree of nodes it is the single hop the cast
    // was; it is a walk rather than a cached pointer because a cached nearest
    // ancestor would have to be invalidated through the whole subtree on every
    // reparent of an ancestor.
    std::shared_ptr<erhe::Hierarchy> ancestor = get_parent().lock();
    while (ancestor && !is<Xformable>(ancestor.get())) {
        ancestor = ancestor->get_parent().lock();
    }
    return std::static_pointer_cast<Xformable>(ancestor);
}

void Xformable::set_node_parent(Xformable* parent)
{
    set_parent(parent, std::numeric_limits<std::size_t>::max());
}

void Xformable::set_node_parent(Xformable* const new_parent_node, const std::size_t position)
{
    if (new_parent_node != nullptr) {
        auto shared_parent_node = std::static_pointer_cast<erhe::Hierarchy>(new_parent_node->shared_from_this());
        set_parent(shared_parent_node, position);
    } else {
        set_parent(std::shared_ptr<erhe::Hierarchy>{}, position);
    }
}

void Xformable::set_parent(const std::shared_ptr<erhe::Hierarchy>& new_parent_item, const std::size_t position)
{
    // Copy, not a reference: handle_parent_update() refreshes the cached
    // world transform (world = new_parent_world * old_local) during
    // Hierarchy::set_parent() below, which would turn the world-preserving
    // re-application into a no-op re-read of the already-reparented value.
    const Trs_transform world_from_node = world_from_node_transform();
    Hierarchy::set_parent(new_parent_item, position);

    // NOTE: For now, we do not care about transforms of orphan nodes.
    //       If we want to change that (to remove the check below), we
    //       should lock weak_from_this before calling set_parent()
    //       above.
    if (new_parent_item) {
        set_world_from_node(world_from_node);
    }
}

#pragma region Xformable attachments
void Xformable::attach(const std::shared_ptr<Node_attachment>& attachment)
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(attachment);

    log->trace("{} (attach({} {})", describe(), attachment->get_type_name(), attachment->get_name());

    attachment->set_node(this);
    node_sanity_check();
}

auto Xformable::detach(Node_attachment* attachment) -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (!attachment) {
        log->warn("empty attachment, cannot detach");
        return false;
    }

    log->trace("{} (detach({} {})", get_name(), attachment->get_type_name(), attachment->get_name());

    auto* node = attachment->get_node();
    if (node != this) {
        log->warn(
            "Attachment {} {} node {} != this {}",
            attachment->get_type_name(),
            attachment->get_name(),
            node ? node->get_name() : "(none)",
            get_name()
        );
        return false;
    }

    attachment->set_node(nullptr);
    return true;
}

auto Xformable::get_attachment_count(const erhe::Item_filter& filter) const -> std::size_t
{
    std::size_t result{};
    for (const auto& attachment : node_data.attachments) {
        if (filter(attachment->get_flag_bits())) {
            ++result;
        }
    }
    return result;
}

void Xformable::for_each_inheritance_child(const std::function<void(erhe::property::Dependency_object&)>& callback)
{
    Hierarchy::for_each_inheritance_child(callback);
    for (const std::shared_ptr<Node_attachment>& attachment : node_data.attachments) {
        if (attachment) {
            callback(*attachment);
        }
    }
}

auto Xformable::get_secondary_property_owner_type() const -> std::optional<erhe::property::Owner_type>
{
    return erhe::Item_base::property_owner_type();
}

void Xformable::handle_add_attachment(const std::shared_ptr<Node_attachment>& attachment, std::size_t position)
{
    ERHE_VERIFY(attachment);

#ifndef NDEBUG
    const auto i = std::find(node_data.attachments.begin(), node_data.attachments.end(), attachment);
    if (i != node_data.attachments.end()) {
        log->error("Xformable {} already has attachment {}", describe(), attachment->get_name());
        return;
    }
#endif

    log->trace("'{}'::handle_add_attachment '{}'", describe(), attachment->get_name());
    position = std::min(node_data.attachments.size(), position);
    node_data.attachments.insert(node_data.attachments.begin() + position, attachment);
    erhe::bump_item_mutation_serial();
}

void Xformable::handle_remove_attachment(Node_attachment* const attachment_to_remove)
{
    ERHE_VERIFY(attachment_to_remove != nullptr);

    const auto i = std::remove_if(
        node_data.attachments.begin(),
        node_data.attachments.end(),
        [attachment_to_remove](const std::shared_ptr<Node_attachment>& entry) {
            return entry.get() == attachment_to_remove;
        }
    );
    if (i != node_data.attachments.end()) {
        log->trace("Removing attachment '{}' from node '{}'", attachment_to_remove->get_name(), get_name());
        node_data.attachments.erase(i, node_data.attachments.end());
        erhe::bump_item_mutation_serial();
    } else {
        log->error(
            "attachment '{}' cannot be removed from node '{}': attachment not found",
            attachment_to_remove->get_name(),
            get_name()
        );
    }
}

void Xformable::handle_flag_bits_update(const uint64_t old_flag_bits, const uint64_t new_flag_bits)
{
    if (((old_flag_bits ^ new_flag_bits) & erhe::Item_flags::no_transform_update) != 0) {
        Scene* const scene = get_scene();
        if (scene != nullptr) {
            scene->handle_node_no_transform_update_changed(*this);
        }
    }
    for (const auto& attachment : get_attachments()) {
        attachment->handle_node_flag_bits_update(old_flag_bits, new_flag_bits);
    }
}

auto Xformable::get_attachments() const -> const std::vector<std::shared_ptr<Node_attachment>>&
{
    return node_data.attachments;
}

#pragma endregion Xformable attachments

auto Xformable::get_item_host() const -> erhe::Item_host*
{
    return node_data.host;
}

auto Xformable::get_scene() const -> Scene*
{
    return (node_data.host != nullptr)
        ? node_data.host->get_hosted_scene()
        : nullptr;
}

void Xformable::handle_parent_update(erhe::Hierarchy* const old_parent_item, erhe::Hierarchy* const new_parent_item)
{
    // Keep this alive to make it simple to call node_sanity_check()
    auto shared_this = weak_from_this().lock();

    ERHE_VERIFY(old_parent_item != new_parent_item);

    // Any prim may parent any other prim (C5), so the parent is taken as the
    // Hierarchy it is: Typed::handle_parent_update carries the parent's item
    // host to this subtree.
    Typed::handle_parent_update(old_parent_item, new_parent_item);

    // A plain reparent keeps parent_from_node, so world_from_node changes:
    // refresh it eagerly and notify attachments / queue the subtree for
    // propagation, like the transform setters do. (World-preserving reparent
    // flows adjust the local transform afterwards through a setter, which
    // repeats this - harmless.) Skipped when detaching (new parent null,
    // e.g. scene teardown): attachments may already be severed from their
    // host resources there.
    if (new_parent_item != nullptr) {
        update_world_from_node();
        handle_transform_update(0);
    }

    hierarchy_sanity_check();
}

void Xformable::handle_item_host_update(erhe::Item_host* const old_item_host, erhe::Item_host* const new_item_host)
{
    ERHE_VERIFY(old_item_host != new_item_host);

    ERHE_VERIFY(old_item_host == node_data.host);

    const auto shared_this = shared_node_from_this(); // Keep alive guarantee

    auto* old_scene_host = dynamic_cast<Scene_host*>(old_item_host);
    auto* new_scene_host = dynamic_cast<Scene_host*>(new_item_host);

    if (old_scene_host != nullptr) {
        old_scene_host->unregister_node(shared_this);
    }
    if (new_scene_host != nullptr) {
        new_scene_host->register_node(shared_this); // updates node_data.host from Scene::register_node()
    } else {
        node_data.host = nullptr; // Orphan
    }

    // This must come *after* node_data.host has been updated
    for (const auto& attachment : node_data.attachments) {
        attachment->handle_item_host_update(old_item_host, new_item_host);
    }

    // Every prim child, not only the transformable ones: a Scope between this
    // node and a node below it carries the host through (C5).
    for (const auto& child : get_children()) {
        erhe::Typed* const typed_child = dynamic_cast<erhe::Typed*>(child.get());
        if (typed_child == nullptr) {
            continue;
        }
        typed_child->handle_item_host_update(old_item_host, new_item_host);
    }
    // Set by new_item_host->register_node(), or Orphan path above in this function
    ERHE_VERIFY(node_data.host == new_item_host);
}

bool Xformable::s_check_no_transform_update_writes{false};

void Xformable::handle_transform_update(const uint64_t serial)
{
    ERHE_PROFILE_FUNCTION();

    if (s_check_no_transform_update_writes && is_no_transform_update()) {
        log->warn("Transform write to no_transform_update node '{}'", get_name());
    }

    const uint64_t effective_serial = (serial > 0)
        ? serial
        : Node_transforms::get_next_serial();

    node_data.transforms.parent_from_node_serial = effective_serial;
    node_data.transforms.world_from_node_serial  = effective_serial;
    for (const auto& attachment : node_data.attachments) {
        attachment->handle_node_transform_update();
    }

    // Expressions reading this node's transform properties (D22): the
    // bridged storage changed without set_value, so announce it here, where
    // every transform writer funnels. One null check when nothing depends on
    // the node.
    invalidate_dependents(translation_property);
    invalidate_dependents(rotation_property);
    invalidate_dependents(scale_property);
    // The computed world components (D26) changed too: this runs on the
    // written node and, from update_transform, on every descendant the
    // propagation pass recomputes.
    invalidate_dependents(world_translation_property);
    invalidate_dependents(world_rotation_property);
    invalidate_dependents(world_scale_property);

    // Descendants inherit this node's new world transform; queue the subtree
    // for propagation in the scene's next update_node_transforms() pass. Every
    // transform mutation path funnels through here, so no writer (animation,
    // physics, tools, MCP, undo, import, ...) needs its own integration.
    Scene* const scene = get_scene();
    if (scene != nullptr) {
        scene->mark_node_transform_dirty(*this);
    }
}

void Xformable::update_transform(uint64_t serial)
{
    ERHE_PROFILE_FUNCTION();

    //if (is_transform_world_normative()) {
    //    const auto& current_parent = get_parent_node();
    //    if (!current_parent) {
    //        return;
    //    }
    //
    //    serial = std::max(serial, current_parent->node_data.transforms.world_from_node_serial);
    //
    //    // if (node_data.transforms.update_serial >= serial) {
    //    //     return;
    //    // }
    //
    //    node_data.transforms.parent_from_node.set(
    //        current_parent->node_from_world() * world_from_node(),
    //        node_from_world() * current_parent->world_from_node()
    //    );
    //    handle_transform_update(serial);
    //} else 
    {
        const auto& current_parent = get_parent_node();
        if (!current_parent) {
            return;
        }

        serial = std::max(serial, current_parent->node_data.transforms.parent_from_node_serial);

        // if (node_data.transforms.update_serial >= serial) {
        //     return;
        // }
        // if (is_shown_in_ui()) {
        //     log_frame->trace("{} TX update parent {}", get_name(), current_parent->get_name());
        // }

        const glm::mat4 world_from_node = current_parent->world_from_node() * parent_from_node();
        // Affine transform: det(mat4) == det(upper-left mat3), and the inverse
        // is deferred (set(matrix) leaves it to the first node_from_world()
        // read) - this loop runs for every node under a moving subtree.
        const float determinant = glm::determinant(glm::mat3{world_from_node});

        node_data.transforms.world_from_node.set(world_from_node);

        if (determinant < 0.0f) {
            enable_flag_bits(erhe::Item_flags::negative_determinant);
        } else {
            disable_flag_bits(erhe::Item_flags::negative_determinant);
        }
        handle_transform_update(serial);
    }
}

void Xformable::update_world_from_node()
{
    const auto& current_parent = get_parent_node();
    if (current_parent && !is_identity_transform(current_parent->world_from_node_transform())) {
        node_data.transforms.world_from_node.set(current_parent->world_from_node() * parent_from_node());
    } else {
        // No parent, or an identity parent: world_from_node == parent_from_node. Copy the
        // TRS components rather than re-decomposing the matrix, so scale and rotation are
        // preserved exactly at (near) zero scale (glm::decompose is unstable there).
        node_data.transforms.world_from_node = node_data.transforms.parent_from_node;
    }
}

void Xformable::node_sanity_check(bool destruction_in_progress) const
{
    for (const auto& child : get_children()) {
        erhe::Item_host* child_host       = child->get_item_host();
        erhe::Item_host* self_host        = get_item_host();
        auto*            child_scene_host = static_cast<Scene_host*>(child_host);
        auto*            self_scene_host  = static_cast<Scene_host*>(self_host);
        Scene*           child_scene      = (child_host != nullptr) ? child_scene_host->get_hosted_scene() : nullptr;
        Scene*           self_scene       = (self_host  != nullptr) ? self_scene_host ->get_hosted_scene() : nullptr;

        if (child_host != self_host) {
            log->error(
                "Scene host mismatch: parent node = `{}` host = `{}` scene = `{}`, child node = `{}` host = `{}` scene = `{}`",
                get_name(),
                (self_host  != nullptr) ? self_host ->get_host_name() : "(none)",
                (self_scene != nullptr) ? self_scene->get_name() : "(none)",
                child->get_name(),
                (child_host  != nullptr) ? child_host ->get_host_name() : "(none)",
                (child_scene != nullptr) ? child_scene->get_name() : "(none)"
            );
        }
    }

    for (const auto& attachment : node_data.attachments) {
        auto* node = attachment->get_node();
        if (node != this) {
            log->error(
                "Xformable '{}' attachment {} '{}' node == '{}'",
                get_name(),
                attachment->get_type_name(),
                attachment->get_name(),
                (node != nullptr)
                    ? node->get_name()
                    : "(none)"
            );
        }
    }

    hierarchy_sanity_check(destruction_in_progress);
}

auto Xformable::parent_from_node_transform() const -> const Trs_transform&
{
    return node_data.transforms.parent_from_node;
}

auto Xformable::parent_from_node_transform() -> Trs_transform&
{
    return node_data.transforms.parent_from_node;
}

auto Xformable::parent_from_node() const -> glm::mat4
{
    return node_data.transforms.parent_from_node.get_matrix();
}

auto Xformable::world_from_node_transform() const -> const Trs_transform&
{
    return node_data.transforms.world_from_node;
}

auto Xformable::world_from_node() const -> glm::mat4
{
    return node_data.transforms.world_from_node.get_matrix();
}

auto Xformable::node_from_parent() const -> glm::mat4
{
    return node_data.transforms.parent_from_node.get_inverse_matrix();
}

auto Xformable::node_from_world() const -> glm::mat4
{
    return node_data.transforms.world_from_node.get_inverse_matrix();
}

auto Xformable::world_from_parent() const -> glm::mat4
{
    const auto& current_parent = get_parent_node();
    if (current_parent) {
        return current_parent->world_from_node();
    }
    return glm::mat4{1};
}

auto Xformable::parent_from_world() const -> glm::mat4
{
    const auto& current_parent = get_parent_node();
    if (current_parent) {
        return current_parent->node_from_world();
    }
    return glm::mat4{1};
}

auto Xformable::position_in_world() const -> glm::vec4
{
    return world_from_node() * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};
}

auto Xformable::direction_in_world() const -> glm::vec4
{
    return world_from_node() * glm::vec4{0.0f, 0.0f, 1.0f, 0.0f};
}

auto Xformable::look_at(const glm::vec3 target_position) const -> glm::mat4
{
    const glm::vec3 eye_position = glm::vec3{position_in_world()};
    const glm::vec3 up_direction = glm::vec3{world_from_node() * glm::vec4{0.0f, 1.0f, 0.0f, 0.0f}};
    return erhe::math::create_look_at(eye_position, target_position, up_direction);
}

auto Xformable::look_at(const Xformable& target) const -> glm::mat4
{
    const glm::vec3 eye_position    = glm::vec3{position_in_world()};
    const glm::vec3 target_position = glm::vec3{target.position_in_world()};
    const glm::vec3 up_direction    = glm::vec3{world_from_node() * glm::vec4{0.0f, 1.0f, 0.0f, 0.0f}};
    return erhe::math::create_look_at(eye_position, target_position, up_direction);
}

auto Xformable::transform_point_from_world_to_local(const glm::vec3 p) const -> glm::vec3
{
    // Does not homogenize
    return glm::vec3{node_from_world() * glm::vec4{p, 1.0f}};
}

auto Xformable::transform_direction_from_world_to_local(const glm::vec3 direction) const -> glm::vec3
{
    return glm::vec3{node_from_world() * glm::vec4{direction, 0.0f}};
}

auto Xformable::transform_point_from_local_to_world(const glm::vec3 p) const -> glm::vec3
{
    // Does not homogenize
    return glm::vec3{world_from_node() * glm::vec4{p, 1.0f}};
}

auto Xformable::transform_direction_from_local_to_world(const glm::vec3 direction) const -> glm::vec3
{
    return glm::vec3{world_from_node() * glm::vec4{direction, 0.0f}};
}

void Xformable::set_parent_from_node(const glm::mat4 parent_from_node)
{
    node_data.transforms.parent_from_node.set(parent_from_node);
    update_world_from_node();
    handle_transform_update(Node_transforms::get_next_serial());
}

void Xformable::set_parent_from_node(const Transform& parent_from_node)
{
    ERHE_PROFILE_FUNCTION();

    node_data.transforms.parent_from_node.set(
        parent_from_node.get_matrix(),
        parent_from_node.get_inverse_matrix()
    );
    update_world_from_node();
    handle_transform_update(Node_transforms::get_next_serial());
}

void Xformable::set_parent_from_node(const Trs_transform& parent_from_node)
{
    ERHE_PROFILE_FUNCTION();

    // Copy the TRS components directly instead of going through the matrix. Re-decomposing
    // a matrix would lose the rotation when the scale is (near) zero (a rank-deficient
    // matrix has no recoverable rotation); copying preserves it.
    node_data.transforms.parent_from_node = parent_from_node;
    update_world_from_node();
    handle_transform_update(Node_transforms::get_next_serial());
}

void Xformable::set_node_from_parent(const glm::mat4 node_from_parent)
{
    node_data.transforms.parent_from_node.set(
        glm::inverse(node_from_parent),
        node_from_parent
    );
    update_world_from_node();
    handle_transform_update(Node_transforms::get_next_serial());
}

void Xformable::set_node_from_parent(const Transform& node_from_parent)
{
    node_data.transforms.parent_from_node.set(
        node_from_parent.get_inverse_matrix(),
        node_from_parent.get_matrix()
    );
    update_world_from_node();
    handle_transform_update(Node_transforms::get_next_serial());
}

void Xformable::set_world_from_node(const glm::mat4 world_from_node)
{
    const auto& current_parent = get_parent_node();
    if (current_parent) {
        set_parent_from_node(current_parent->node_from_world() * world_from_node);
    } else {
        set_parent_from_node(world_from_node);
    }
}

void Xformable::set_world_from_node(const Transform& world_from_node)
{
    const auto& current_parent = get_parent_node();
    if (current_parent) {
        set_parent_from_node(
            current_parent->node_from_world() * world_from_node.get_matrix()
        );
    } else {
        set_parent_from_node(world_from_node);
    }
}

void Xformable::set_world_from_node(const Trs_transform& world_from_node)
{
    const auto& current_parent = get_parent_node();
    if (current_parent && !is_identity_transform(current_parent->world_from_node_transform())) {
        // Non-identity parent: parent-relative requires composition through matrices.
        set_parent_from_node(
            current_parent->node_from_world() * world_from_node.get_matrix()
        );
    } else {
        // No parent, or an identity parent (the scene root): store the TRS components
        // directly. No matrix round-trip and no glm::decompose, so scale and rotation
        // survive (near) zero scale intact.
        set_parent_from_node(world_from_node);
    }
}

void Xformable::set_node_from_world(const glm::mat4 node_from_world)
{
    node_data.transforms.world_from_node.set(glm::inverse(node_from_world), node_from_world);
    const auto& world_from_node = node_data.transforms.world_from_node.get_matrix();
    const auto& current_parent  = get_parent_node();
    if (current_parent) {
        node_data.transforms.parent_from_node.set(
            current_parent->node_from_world() * world_from_node,
            node_from_world * current_parent->world_from_node()
        );
    } else {
        node_data.transforms.parent_from_node = node_data.transforms.world_from_node;
    }
    handle_transform_update(Node_transforms::get_next_serial());
}

void Xformable::set_node_from_world(const Transform& node_from_world)
{
    node_data.transforms.world_from_node.set(
        node_from_world.get_inverse_matrix(),
        node_from_world.get_matrix()
    );
    const auto& current_parent = get_parent_node();
    if (current_parent) {
        node_data.transforms.parent_from_node.set(
            current_parent->node_from_world() * node_data.transforms.world_from_node.get_matrix(),
            node_from_world.get_matrix() * current_parent->world_from_node()
        );
    } else {
        node_data.transforms.parent_from_node = node_data.transforms.world_from_node;
    }
    handle_transform_update(Node_transforms::get_next_serial());
}

auto Node_data::diff_mask(const Node_data& lhs, const Node_data& rhs)-> unsigned int
{
    unsigned int mask{0};

    if (lhs.transforms.parent_from_node != rhs.transforms.parent_from_node) mask |= Node_data::bit_transform;
    if (lhs.transforms.world_from_node  != rhs.transforms.world_from_node ) mask |= Node_data::bit_transform;
    return mask;
}

void set_prim_parent(const std::shared_ptr<Xformable>& prim, const std::shared_ptr<erhe::Hierarchy>& parent)
{
    ERHE_VERIFY(prim);
    // The qualified call to the two-argument overload: the one-argument
    // Hierarchy::set_parent forwards through the virtual, which lands back
    // in Xformable's world-preserving override.
    prim->Hierarchy::set_parent(parent, std::numeric_limits<std::size_t>::max());
}

} // namespace erhe::scene
