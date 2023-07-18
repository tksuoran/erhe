#include "erhe/scene/node.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/scene/scene_host.hpp"
#include "erhe/scene/scene_log.hpp"
#include "erhe/toolkit/bit_helpers.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

#include <fmt/format.h>

#include <sstream>

namespace erhe::scene
{

uint64_t Node_transforms::s_global_update_serial = 0;

auto Node_transforms::get_current_serial() -> uint64_t
{
    return s_global_update_serial;
}

auto Node_transforms::get_next_serial() -> uint64_t
{
    return ++s_global_update_serial;
}

// -----------------------------------------------------------------------------
#pragma region Node_attachment
Node_attachment::Node_attachment()
{
}

Node_attachment::Node_attachment(const std::string_view name)
    : Item{name}
{
}

Node_attachment::~Node_attachment() noexcept
{
    log->trace("~Node_attachment `{}`", get_name());

    Node* const host_node = get_node();
    if (host_node != nullptr) {
        host_node->detach(this);
    }
}

auto Node_attachment::get_node() -> Node*
{
    return m_node;
}

auto Node_attachment::get_node() const -> const Node*
{
    return m_node;
}

auto Node_attachment::get_item_host() const -> Item_host*
{
    if (m_node == nullptr) {
        return nullptr;
    }
    return m_node->get_item_host();
}

void Node_attachment::handle_node_update(
    Node* const old_node,
    Node* const new_node
)
{
    const uint64_t old_flag_bits = old_node ? old_node->get_flag_bits() : 0;
    const uint64_t new_flag_bits = new_node ? new_node->get_flag_bits() : 0;
    const bool     visible       = (new_flag_bits & Item_flags::visible) == Item_flags::visible;
    if (old_flag_bits != new_flag_bits) {
        handle_node_flag_bits_update(old_flag_bits, new_flag_bits);
    }
    set_visible(visible);
}

void Node_attachment::handle_node_flag_bits_update(
    const uint64_t old_node_flag_bits,
    const uint64_t new_node_flag_bits
)
{
    static_cast<void>(old_node_flag_bits);
    const bool visible = (new_node_flag_bits & Item_flags::visible) == Item_flags::visible;
    set_visible(visible);
};

void Node_attachment::set_node(
    Node* const       node,
    const std::size_t position
)
{
    if (m_node == node) {
        return;
    }
    Node* const old_node = m_node;
    Item_host* const old_host = (m_node != nullptr) ? m_node->get_item_host() : nullptr;
    m_node = node;
    Item_host* const new_host = (m_node != nullptr) ? m_node->get_item_host() : nullptr;

    if (old_node != nullptr) {
        old_node->handle_remove_attachment(this);
    }
    if (node != nullptr) {
        auto weak_this = weak_from_this();
        ERHE_VERIFY(!weak_this.expired());
        auto shared_this = std::static_pointer_cast<Node_attachment>(weak_this.lock());
        node->handle_add_attachment(
            shared_this,
            position
        );

        handle_node_update(old_node, node);
        if (new_host != old_host) {
            handle_item_host_update(old_host, new_host);
            if (m_node != nullptr) {
                handle_node_transform_update();
            }
        }
    }
};

#pragma endregion Node_attachment
// -----------------------------------------------------------------------------

Node::Node() = default;

Node::Node(const std::string_view name)
    : Item{name}
{
}

Node::~Node() noexcept
{
    node_sanity_check();

    log->trace(
        "~Node '{}' depth = {} child count = {}",
        get_name(),
        get_depth(),
        item_data.children.size()
    );

    while (!node_data.attachments.empty()) {
        // Causes trigger Node::handle_remove_attachment() calls to this Node
        node_data.attachments.back()->set_node(nullptr);
    }
}

auto Node::get_static_type() -> uint64_t
{
    return Item_type::node;
}

auto Node::get_static_type_name() -> const char*
{
    return Item_type::c_bit_labels[Item_type::index_node];
}

auto Node::get_type() const -> uint64_t
{
    return get_static_type();
}

auto Node::get_type_name() const -> const char*
{
    return get_static_type_name();
}

auto Node::get_parent_node() const -> std::shared_ptr<Node>
{
    std::shared_ptr<Item> shared_parent_item = item_data.parent.lock();

    if (!is_node(shared_parent_item)) {
        return std::shared_ptr<Node>{};
    }
    return std::static_pointer_cast<Node>(shared_parent_item);
}

void Node::set_parent(Node* const new_parent_node, const std::size_t position)
{
    if (new_parent_node != nullptr) {
        set_parent(new_parent_node->shared_from_this(), position);
    } else {
        set_parent(std::shared_ptr<Item>{}, position);
    }
}

void Node::set_parent(
    const std::shared_ptr<Item>& new_parent_item,
    const std::size_t            position
)
{
    const auto& world_from_node = world_from_node_transform();
    Item::set_parent(new_parent_item, position);

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

    log->trace(
        "{} (attach({} {})",
        get_name(),
        attachment->get_type_name(),
        attachment->get_name()
    );

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

    log->trace(
        "{} (detach({} {})",
        get_name(),
        attachment->get_type_name(),
        attachment->get_name()
    );

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

auto Node::get_attachment_count(const Item_filter& filter) const -> std::size_t
{
    std::size_t result{};
    for (const auto& attachment : node_data.attachments) {
        if (filter(attachment->get_flag_bits())) {
            ++result;
        }
    }
    return result;
}

void Node::handle_add_attachment(
    const std::shared_ptr<Node_attachment>& attachment,
    const std::size_t                       position
)
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
    node_data.attachments.insert(node_data.attachments.begin() + position, attachment);
}

void Node::handle_remove_attachment(
    Node_attachment* const attachment_to_remove
)
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
    for (const auto& attachment : node_data.attachments) {
        attachment->handle_node_flag_bits_update(old_flag_bits, new_flag_bits);
    }
}

auto Node::get_attachments() const -> const std::vector<std::shared_ptr<Node_attachment>>&
{
    return node_data.attachments;
}

#pragma endregion Node attachments

auto Node::get_item_host() const -> Item_host*
{
    return node_data.host;
}

auto Node::get_scene() const -> Scene*
{
    return (node_data.host != nullptr)
        ? node_data.host->get_hosted_scene()
        : nullptr;
}

void Node::handle_parent_update(
    Item* const old_parent_item,
    Item* const new_parent_item
)
{
    // Keep this alive to make it simple to call node_sanity_check()
    auto shared_this = weak_from_this().lock();

    ERHE_VERIFY(old_parent_item != new_parent_item);
    ERHE_VERIFY((old_parent_item == nullptr) || is_node(old_parent_item));
    ERHE_VERIFY((new_parent_item == nullptr) || is_node(new_parent_item));
    Node*      const old_parent     = reinterpret_cast<Node* const>(old_parent_item);
    Node*      const new_parent     = reinterpret_cast<Node* const>(new_parent_item);
    Item_host* const old_scene_host = old_parent != nullptr ? old_parent->get_item_host() : nullptr;
    Item_host* const new_scene_host = new_parent != nullptr ? new_parent->get_item_host() : nullptr;
    if (old_scene_host != new_scene_host) {
        handle_scene_host_update(old_scene_host, new_scene_host);
    }

    node_sanity_check();
}

void Node::handle_scene_host_update(
    Item_host* const old_item_host,
    Item_host* const new_item_host
)
{
    ERHE_VERIFY(old_item_host != new_item_host);

    ERHE_VERIFY(old_item_host == node_data.host);

    for (const auto& attachment : node_data.attachments) {
        attachment->handle_item_host_update(old_item_host, new_item_host);
    }

    auto shared_this = shared_from_this(); // Keep alive guarantee

    if (old_item_host != nullptr) {
        old_item_host->unregister_node(
            std::static_pointer_cast<Node>(
                shared_this
            )
        );
    }
    if (new_item_host != nullptr) {
        new_item_host->register_node(
            std::static_pointer_cast<Node>(shared_from_this())
        );
    }

    for (const auto& child : item_data.children) {
        auto child_node = as_node(child);
        if (!child_node) {
            continue;
        }
        child_node->handle_scene_host_update(old_item_host, new_item_host);
    }
    // Set by new_item_host->register_node()
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
    for (const auto& child : item_data.children) {
        Item_host* child_host  = child->get_item_host();
        Item_host* self_host   = get_item_host();
        Scene*     child_scene = (child_host != nullptr) ? child_host->get_hosted_scene() : nullptr;
        Scene*     self_scene  = (self_host  != nullptr) ? self_host ->get_hosted_scene() : nullptr;

        if (child_host != self_host) {
            log->error(
                "Scene host mismatch: parent node = `{}` host = `{}` scene = `{}`, child node = `{}` host = `{}` scene = `{}`",
                get_name(),
                (self_host  != nullptr) ? self_host ->get_host_name() : "(none)",
                (self_scene != nullptr) ? self_scene->get_name()      : "(none)",
                child->get_name(),
                (child_host  != nullptr) ? child_host ->get_host_name() : "(none)",
                (child_scene != nullptr) ? child_scene->get_name()      : "(none)"
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

    item_sanity_check();
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

[[nodiscard]] auto Node::look_at(const Node& target) const -> glm::mat4
{
    const glm::vec3 eye_position    = position_in_world();
    const glm::vec3 target_position = target.position_in_world();
    const glm::vec3 up_direction    = world_from_node() * glm::vec4{0.0f, 1.0f, 0.0f, 0.0f};
    return erhe::toolkit::create_look_at(eye_position, target_position, up_direction);
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

auto Node_data::diff_mask(const Node_data& lhs, const Node_data& rhs)
-> unsigned int
{
    unsigned int mask{0};

    if (lhs.transforms.parent_from_node != rhs.transforms.parent_from_node) mask |= Node_data::bit_transform;
    if (lhs.transforms.world_from_node  != rhs.transforms.world_from_node ) mask |= Node_data::bit_transform;
    return mask;
}

#pragma region Item type casts
using namespace erhe::toolkit;

auto is_node(const Item* const item) -> bool
{
    if (item == nullptr) {
        return false;
    }
    return test_all_rhs_bits_set(item->get_type(), Item_type::node);
}

auto is_node(const std::shared_ptr<Item>& item) -> bool
{
    return is_node(item.get());
}

auto as_node(Item* const item) -> Node*
{
    if (item == nullptr) {
        return nullptr;
    }
    if (!test_all_rhs_bits_set(item->get_type(), Item_type::node)) {
        return nullptr;
    }
    return reinterpret_cast<Node*>(item);
}

auto as_node(const std::shared_ptr<Item>& item) -> std::shared_ptr<Node>
{
    if (!item) {
        return {};
    }
    if (!test_all_rhs_bits_set(item->get_type(), Item_type::node)) {
        return {};
    }
    return std::static_pointer_cast<Node>(item);
}

auto as_node_attachment(const std::shared_ptr<Item>& item) -> std::shared_ptr<Node_attachment>
{
    if (!item) {
        return {};
    }
    if (!test_all_rhs_bits_set(item->get_type(), Item_type::node_attachment)) {
        return {};
    }
    return std::static_pointer_cast<Node_attachment>(item);
}
#pragma endregion Item type casts


} // namespace erhe::scene
