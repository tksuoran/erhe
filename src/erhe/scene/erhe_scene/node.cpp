#include "erhe_scene/node.hpp"
#include "erhe_scene/node_attachment.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/scene_host.hpp"
#include "erhe_scene/scene_log.hpp"
#include "erhe_bit/bit_helpers.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::scene {

using namespace erhe;

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
    // Attachments are handled in Node(const Node&)
}

auto Node::get_static_type() -> uint64_t
{
    return erhe::Item_type::node;
}

auto Node::get_type() const -> uint64_t
{
    return get_static_type();
}

auto Node::get_type_name() const -> std::string_view
{
    return static_type_name;
}


Node::Node() = default;
Node::Node(const Node&) { ERHE_FATAL("TODO"); }
Node& Node::operator=(const Node&) { ERHE_FATAL("TODO"); }

Node::Node(const std::string_view name)
    : Item{name}
{
}

Node::Node(const Node& src, for_clone)
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

Node::~Node() noexcept
{
    node_sanity_check();

    log->trace(
        "~Node '{}' depth = {} child count = {}",
        get_name(),
        get_depth(),
        get_child_count()
    );

    while (!node_data.attachments.empty()) {
        // Causes trigger Node::handle_remove_attachment() calls to this Node
        node_data.attachments.back()->set_node(nullptr);
    }
}

auto Node::shared_node_from_this() -> std::shared_ptr<Node>
{
    return std::static_pointer_cast<Node>(shared_from_this());
}

auto Node::get_parent_node() const -> std::shared_ptr<Node>
{
    const auto shared_parent_item = get_parent().lock();
    return std::static_pointer_cast<Node>(shared_parent_item);
}

void Node::set_node_parent(Node* parent)
{
    set_parent(parent, std::numeric_limits<std::size_t>::max());
}

void Node::set_node_parent(Node* const new_parent_node, const std::size_t position)
{
    if (new_parent_node != nullptr) {
        auto shared_parent_node = std::static_pointer_cast<erhe::Hierarchy>(new_parent_node->shared_from_this());
        set_parent(shared_parent_node, position);
    } else {
        set_parent(std::shared_ptr<erhe::Hierarchy>{}, position);
    }
}

void Node::set_parent(const std::shared_ptr<erhe::Hierarchy>& new_parent_item, const std::size_t position)
{
    const auto& world_from_node = world_from_node_transform();
    Hierarchy::set_parent(new_parent_item, position);

    // NOTE: For now, we do not care about transforms of orphan nodes.
    //       If we want to change that (to remove the check below), we
    //       should lock weak_from_this before calling set_parent()
    //       above.
    if (new_parent_item) {
        set_world_from_node(world_from_node);
    }
}

#pragma region Node attachments
void Node::attach(const std::shared_ptr<Node_attachment>& attachment)
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(attachment);

    log->trace("{} (attach({} {})", describe(), attachment->get_type_name(), attachment->get_name());

    attachment->set_node(this);
    node_sanity_check();
}

auto Node::detach(Node_attachment* attachment) -> bool
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

auto Node::get_attachment_count(const erhe::Item_filter& filter) const -> std::size_t
{
    std::size_t result{};
    for (const auto& attachment : node_data.attachments) {
        if (filter(attachment->get_flag_bits())) {
            ++result;
        }
    }
    return result;
}

void Node::handle_add_attachment(const std::shared_ptr<Node_attachment>& attachment, std::size_t position)
{
    ERHE_VERIFY(attachment);

#ifndef NDEBUG
    const auto i = std::find(node_data.attachments.begin(), node_data.attachments.end(), attachment);
    if (i != node_data.attachments.end()) {
        log->error("Node {} already has attachment {}", describe(), attachment->get_name());
        return;
    }
#endif

    log->trace("'{}'::handle_add_attachment '{}'", describe(), attachment->get_name());
    position = std::min(node_data.attachments.size(), position);
    node_data.attachments.insert(node_data.attachments.begin() + position, attachment);
}

void Node::handle_remove_attachment(Node_attachment* const attachment_to_remove)
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
    } else {
        log->error(
            "attachment '{}' cannot be removed from node '{}': attachment not found",
            attachment_to_remove->get_name(),
            get_name()
        );
    }
}

void Node::handle_flag_bits_update(const uint64_t old_flag_bits, const uint64_t new_flag_bits)
{
    for (const auto& attachment : get_attachments()) {
        attachment->handle_node_flag_bits_update(old_flag_bits, new_flag_bits);
    }
}

auto Node::get_attachments() const -> const std::vector<std::shared_ptr<Node_attachment>>&
{
    return node_data.attachments;
}

#pragma endregion Node attachments

auto Node::get_item_host() const -> erhe::Item_host*
{
    return node_data.host;
}

auto Node::get_scene() const -> Scene*
{
    return (node_data.host != nullptr)
        ? node_data.host->get_hosted_scene()
        : nullptr;
}

void Node::handle_parent_update(erhe::Hierarchy* const old_parent_item, erhe::Hierarchy* const new_parent_item)
{
    // Keep this alive to make it simple to call node_sanity_check()
    auto shared_this = weak_from_this().lock();

    ERHE_VERIFY(old_parent_item != new_parent_item);
    ERHE_VERIFY((old_parent_item == nullptr) || is<Node>(old_parent_item));
    ERHE_VERIFY((new_parent_item == nullptr) || is<Node>(new_parent_item));
    Node* const old_parent = static_cast<Node* const>(old_parent_item);
    Node* const new_parent = static_cast<Node* const>(new_parent_item);
    erhe::Item_host* const old_item_host = (old_parent != nullptr) ? old_parent->get_item_host() : nullptr;
    erhe::Item_host* const new_item_host = (new_parent != nullptr) ? new_parent->get_item_host() : nullptr;
    if (old_item_host != new_item_host) {
        handle_item_host_update(old_item_host, new_item_host);
    }

    hierarchy_sanity_check();
}

void Node::handle_item_host_update(erhe::Item_host* const old_item_host, erhe::Item_host* const new_item_host)
{
    ERHE_VERIFY(old_item_host != new_item_host);

    ERHE_VERIFY(old_item_host == node_data.host);

    const auto shared_this = shared_node_from_this(); // Keep alive guarantee

    Scene_host* old_scene_host = static_cast<Scene_host*>(old_item_host);
    Scene_host* new_scene_host = static_cast<Scene_host*>(new_item_host);

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

    for (const auto& child : get_children()) {
        auto child_node = std::dynamic_pointer_cast<Node>(child);
        if (!child_node) {
            continue;
        }
        child_node->handle_item_host_update(old_item_host, new_item_host);
    }
    // Set by new_item_host->register_node(), or Orphan path above in this function
    ERHE_VERIFY(node_data.host == new_item_host);
}

void Node::handle_transform_update(const uint64_t serial) const
{
    ERHE_PROFILE_FUNCTION();

    const uint64_t effective_serial = (serial > 0)
        ? serial
        : Node_transforms::get_next_serial();

    node_data.transforms.parent_from_node_serial = effective_serial;
    node_data.transforms.world_from_node_serial  = effective_serial;
    for (const auto& attachment : node_data.attachments) {
        attachment->handle_node_transform_update();
    }
}

void Node::update_transform(uint64_t serial)
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
        if (is_shown_in_ui()) {
            log_frame->trace("{} TX update parent {}", get_name(), current_parent->get_name());
        }

        node_data.transforms.world_from_node.set(
            current_parent->world_from_node() * parent_from_node(),
            node_from_parent() * current_parent->node_from_world()
        );
        handle_transform_update(serial);
    }
}

void Node::update_world_from_node()
{
    const auto& current_parent = get_parent_node();
    if (current_parent) {
        node_data.transforms.world_from_node.set(
            current_parent->world_from_node() * parent_from_node(),
            node_from_parent() * current_parent->node_from_world()
        );
    } else {
        node_data.transforms.world_from_node.set(
            parent_from_node(),
            node_from_parent()
        );
    }
}

void Node::node_sanity_check() const
{
    for (const auto& child : get_children()) {
        erhe::Item_host* child_host  = child->get_item_host();
        erhe::Item_host* self_host   = get_item_host();
        Scene_host*      child_scene_host = static_cast<Scene_host*>(child_host);
        Scene_host*      self_scene_host  = static_cast<Scene_host*>(self_host);
        Scene*           child_scene = (child_host != nullptr) ? child_scene_host->get_hosted_scene() : nullptr;
        Scene*           self_scene  = (self_host  != nullptr) ? self_scene_host ->get_hosted_scene() : nullptr;

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
                "Node '{}' attachment {} '{}' node == '{}'",
                get_name(),
                attachment->get_type_name(),
                attachment->get_name(),
                (node != nullptr)
                    ? node->get_name()
                    : "(none)"
            );
        }
    }

    hierarchy_sanity_check();
}

auto Node::parent_from_node_transform() const -> const Trs_transform&
{
    return node_data.transforms.parent_from_node;
}

auto Node::parent_from_node_transform() -> Trs_transform&
{
    return node_data.transforms.parent_from_node;
}

auto Node::parent_from_node() const -> glm::mat4
{
    return node_data.transforms.parent_from_node.get_matrix();
}

auto Node::world_from_node_transform() const -> const Trs_transform&
{
    return node_data.transforms.world_from_node;
}

auto Node::world_from_node() const -> glm::mat4
{
    return node_data.transforms.world_from_node.get_matrix();
}

auto Node::node_from_parent() const -> glm::mat4
{
    return node_data.transforms.parent_from_node.get_inverse_matrix();
}

auto Node::node_from_world() const -> glm::mat4
{
    return node_data.transforms.world_from_node.get_inverse_matrix();
}

auto Node::world_from_parent() const -> glm::mat4
{
    const auto& current_parent = get_parent_node();
    if (current_parent) {
        return current_parent->world_from_node();
    }
    return glm::mat4{1};
}

auto Node::parent_from_world() const -> glm::mat4
{
    const auto& current_parent = get_parent_node();
    if (current_parent) {
        return current_parent->node_from_world();
    }
    return glm::mat4{1};
}

auto Node::position_in_world() const -> glm::vec4
{
    return world_from_node() * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f};
}

auto Node::direction_in_world() const -> glm::vec4
{
    return world_from_node() * glm::vec4{0.0f, 0.0f, 1.0f, 0.0f};
}

auto Node::look_at(const glm::vec3 target_position) const -> glm::mat4
{
    const glm::vec3 eye_position = position_in_world();
    const glm::vec3 up_direction = world_from_node() * glm::vec4{0.0f, 1.0f, 0.0f, 0.0f};
    return erhe::math::create_look_at(eye_position, target_position, up_direction);
}

auto Node::look_at(const Node& target) const -> glm::mat4
{
    const glm::vec3 eye_position    = position_in_world();
    const glm::vec3 target_position = target.position_in_world();
    const glm::vec3 up_direction    = world_from_node() * glm::vec4{0.0f, 1.0f, 0.0f, 0.0f};
    return erhe::math::create_look_at(eye_position, target_position, up_direction);
}

auto Node::transform_point_from_world_to_local(const glm::vec3 p) const -> glm::vec3
{
    // Does not homogenize
    return glm::vec3{node_from_world() * glm::vec4{p, 1.0f}};
}

auto Node::transform_direction_from_world_to_local(const glm::vec3 direction) const -> glm::vec3
{
    return glm::vec3{node_from_world() * glm::vec4{direction, 0.0f}};
}

auto Node::transform_point_from_local_to_world(const glm::vec3 p) const -> glm::vec3
{
    // Does not homogenize
    return glm::vec3{world_from_node() * glm::vec4{p, 1.0f}};
}

auto Node::transform_direction_from_local_to_world(const glm::vec3 direction) const -> glm::vec3
{
    return glm::vec3{world_from_node() * glm::vec4{direction, 0.0f}};
}

void Node::set_parent_from_node(const glm::mat4 parent_from_node)
{
    node_data.transforms.parent_from_node.set(parent_from_node);
    update_world_from_node();
    handle_transform_update(Node_transforms::get_next_serial());
}

void Node::set_parent_from_node(const Transform& parent_from_node)
{
    ERHE_PROFILE_FUNCTION();

    node_data.transforms.parent_from_node.set(
        parent_from_node.get_matrix(),
        parent_from_node.get_inverse_matrix()
    );
    update_world_from_node();
    handle_transform_update(Node_transforms::get_next_serial());
}

void Node::set_node_from_parent(const glm::mat4 node_from_parent)
{
    node_data.transforms.parent_from_node.set(
        glm::inverse(node_from_parent),
        node_from_parent
    );
    update_world_from_node();
    handle_transform_update(Node_transforms::get_next_serial());
}

void Node::set_node_from_parent(const Transform& node_from_parent)
{
    node_data.transforms.parent_from_node.set(
        node_from_parent.get_inverse_matrix(),
        node_from_parent.get_matrix()
    );
    update_world_from_node();
    handle_transform_update(Node_transforms::get_next_serial());
}

void Node::set_world_from_node(const glm::mat4 world_from_node)
{
    const auto& current_parent = get_parent_node();
    if (current_parent) {
        set_parent_from_node(current_parent->node_from_world() * world_from_node);
    } else {
        set_parent_from_node(world_from_node);
    }
}

void Node::set_world_from_node(const Transform& world_from_node)
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

void Node::set_node_from_world(const glm::mat4 node_from_world)
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

void Node::set_node_from_world(const Transform& node_from_world)
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

auto is_node(const Item_base* const item) -> bool
{
    if (item == nullptr) {
        return false;
    }
    return erhe::bit::test_all_rhs_bits_set(item->get_type(), erhe::Item_type::node);
}

auto is_node(const std::shared_ptr<erhe::Item_base>& item) -> bool
{
    return is_node(item.get());
}

} // namespace erhe::scene
