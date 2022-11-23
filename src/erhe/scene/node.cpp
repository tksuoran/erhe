#include "erhe/scene/node.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/scene/scene_host.hpp"
#include "erhe/scene/scene_log.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

#include <fmt/format.h>

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

INode_attachment::~INode_attachment() noexcept
{
    Node* const host_node = get_node();
    if (host_node != nullptr)
    {
        host_node->detach(this);
    }
}

auto INode_attachment::get_node() const -> Node*
{
    return m_node;
}

auto INode_attachment::flag_bits() const -> uint64_t
{
    return m_flag_bits;
}

auto INode_attachment::flag_bits() -> uint64_t&
{
    return m_flag_bits;
}

void INode_attachment::set_node(Node* const node)
{
    Scene_host* const old_host = (m_node != nullptr) ? m_node->get_scene_host() : nullptr;
    m_node = node;
    Scene_host* const new_host = (m_node != nullptr) ? m_node->get_scene_host() : nullptr;
    if (new_host != old_host)
    {
        handle_node_scene_host_update(old_host, new_host);
        if (m_node != nullptr)
        {
            handle_node_transform_update();
        }
    }
};

Node::Node(std::string_view name)
    : m_id{}
{
    node_data.name  = name;
    node_data.label = fmt::format("{}##Node{}", name, m_id.get_id());
}

Node::~Node() noexcept
{
    sanity_check();

    std::shared_ptr<Node> parent = node_data.parent.lock();
    set_parent({});
    for (auto& child : node_data.children)
    {
        child->set_parent(parent);
    }
    for (auto& attachment : node_data.attachments)
    {
        attachment->set_node(nullptr);
    }
    sanity_check();
}

auto Node::node_type() const -> const char*
{
    return "Node";
}

auto Node::is_selected() const -> bool
{
    return (node_data.visibility_mask & Node_visibility::selected) == Node_visibility::selected;
}

auto Node::name() const -> const std::string&
{
    return node_data.name;
}

auto Node::label() const -> const std::string&
{
    return node_data.label;
}

void Node::set_name(const std::string_view name)
{
    node_data.name = name;
    node_data.label = fmt::format("{}##Node{}", name, m_id.get_id());
}

void Node::attach(const std::shared_ptr<INode_attachment>& attachment)
{
    ERHE_PROFILE_FUNCTION

    ERHE_VERIFY(attachment);

    log->trace(
        "{} ({}).attach({})",
        name(),
        node_type(),
        attachment->node_attachment_type()
    );

#ifndef NDEBUG
    const auto i = std::find(node_data.attachments.begin(), node_data.attachments.end(), attachment);
    if (i != node_data.attachments.end())
    {
        log->error("Attachment {} already attached to {}", attachment->node_attachment_type(), name());
        return;
    }
#endif

    node_data.attachments.push_back(attachment);
    attachment->set_node(this);
    handle_visibility_mask_update();
    sanity_check();
}

auto Node::detach(INode_attachment* attachment) -> bool
{
    ERHE_PROFILE_FUNCTION

    if (!attachment)
    {
        log->warn("empty attachment, cannot detach");
        return false;
    }

    log->trace(
        "{} ({}).detach({})",
        name(),
        node_type(),
        attachment->node_attachment_type()
    );

    auto* node = attachment->get_node();
    if (node != this)
    {
        log->warn(
            "Attachment {} node {} != this {}",
            attachment->node_attachment_type(),
            node
                ? node->name()
                : "(none)",
            name()
        );
        return false;
    }

    const auto i = std::remove_if(
        node_data.attachments.begin(),
        node_data.attachments.end(),
        [attachment](const std::shared_ptr<INode_attachment>& node_attachment)
        {
            return node_attachment.get() == attachment;
        }
    );
    if (i != node_data.attachments.end())
    {
        log->trace(
            "Removing {} attachment from node",
            attachment->node_attachment_type(),
            name()
        );
        node_data.attachments.erase(i, node_data.attachments.end());
        attachment->set_node(nullptr);
        sanity_check();
        return true;
    }

    log->warn(
        "Detaching {} from node {} failed - was not attached",
        attachment->node_attachment_type(),
        name()
    );

    return false;
}

auto Node::child_count() const -> std::size_t
{
    return node_data.children.size();
}

auto Node::get_id() const -> erhe::toolkit::Unique_id<Node>::id_type
{
    return m_id.get_id();
}

auto Node::get_index_in_parent() const -> std::size_t
{
    const auto& current_parent = parent().lock();
    if (current_parent)
    {
        const auto index = current_parent->get_index_of_child(this);
        return index.has_value() ? index.value() : 0;
    }
    return 0;
}

auto Node::get_index_of_child(const Node* child) const -> std::optional<std::size_t>
{
    for (
        std::size_t i = 0, end = node_data.children.size();
        i < end;
        ++i
    )
    {
        if (node_data.children[i].get() == child)
        {
            return i;
        }
    }
    return {};
}

auto Node::is_ancestor(const Node* ancestor_candidate) const -> bool
{
    const auto& current_parent = parent().lock();
    if (!current_parent)
    {
        return false;
    }
    if (current_parent.get() == ancestor_candidate)
    {
        return true;
    }
    return current_parent->is_ancestor(ancestor_candidate);
}

auto Node::get_scene_host() const -> Scene_host*
{
    return node_data.host;
}

auto Node::get_scene() const -> Scene*
{
    return (node_data.host != nullptr)
        ? node_data.host->get_scene()
        : nullptr;
}

void Node::handle_add_child(
    const std::shared_ptr<Node>& child_node,
    const std::size_t            position
)
{
    ERHE_VERIFY(child_node);

#ifndef NDEBUG
    const auto i = std::find(node_data.children.begin(), node_data.children.end(), child_node);
    if (i != node_data.children.end())
    {
        log->error("Node {} already has child {}", name(), child_node->name());
        return;
    }
#endif

    node_data.children.insert(node_data.children.begin() + position, child_node);
}

void Node::handle_remove_child(
    Node* const child_node
)
{
    ERHE_VERIFY(child_node != nullptr);

    const auto i = std::remove_if(
        node_data.children.begin(),
        node_data.children.end(),
        [child_node](const std::shared_ptr<Node>& node)
        {
            return node.get() == child_node;
        }
    );
    if (i != node_data.children.end())
    {
        node_data.children.erase(i, node_data.children.end());
    }
    else
    {
        log->error(
            "child node {} cannot be removed from parent node {}: child not found",
            child_node->name(),
            name()
        );
    }
}

//// void Node::set_parent(
////     const std::weak_ptr<Node>& new_parent_node,
////     const std::size_t          position
//// )
//// {
////     set_parent(new_parent_node.lock(), position);
//// }

void Node::set_parent(
    const std::shared_ptr<Node>& new_parent_node,
    const std::size_t            position
)
{
    const auto& world_from_node = world_from_node_transform();

    Node* old_parent = node_data.parent.lock().get();
    node_data.parent = new_parent_node;
    Node* new_parent = node_data.parent.lock().get();

    if (old_parent == new_parent)
    {
        return;
    }

    if (old_parent)
    {
        old_parent->handle_remove_child(this);
    }

    if (new_parent)
    {
        new_parent->handle_add_child(shared_from_this(), position);
    }

    set_world_from_node(world_from_node);
    set_depth_recursive(
        new_parent
            ? new_parent->depth() + 1
            : 0
    );
    handle_parent_update(old_parent, new_parent);
    sanity_check();
}

void Node::set_parent(
    Node* const       new_parent_node,
    const std::size_t position
)
{
    if (new_parent_node != nullptr)
    {
        set_parent(new_parent_node->shared_from_this(), position);
    }
    else
    {
        set_parent(std::shared_ptr<Node>{}, position);
    }
}

void Node::set_depth_recursive(const std::size_t depth)
{
    ERHE_PROFILE_FUNCTION

    if (node_data.depth == depth)
    {
        return;
    }
    node_data.depth = depth;
    for (const auto& child : node_data.children)
    {
        child->set_depth_recursive(depth + 1);
    }
}

void Node::handle_parent_update(Node* old_parent, Node* new_parent)
{
    ERHE_VERIFY(old_parent != new_parent);
    Scene_host* old_scene_host = old_parent != nullptr ? old_parent->get_scene_host() : nullptr;
    Scene_host* new_scene_host = new_parent != nullptr ? new_parent->get_scene_host() : nullptr;
    if (old_scene_host != new_scene_host)
    {
        handle_scene_host_update(old_scene_host, new_scene_host);
    }
    sanity_check();
}

void Node::handle_scene_host_update(
    Scene_host* const old_scene_host,
    Scene_host* const new_scene_host
)
{
    ERHE_VERIFY(old_scene_host != new_scene_host);

    if (old_scene_host != nullptr)
    {
        Scene* old_scene = old_scene_host->get_scene();
        if (old_scene != nullptr)
        {
            old_scene->unregister_node(shared_from_this());
        }
    }
    if (new_scene_host != nullptr)
    {
        Scene* new_scene = new_scene_host->get_scene();
        if (new_scene != nullptr)
        {
            new_scene->register_node(shared_from_this());
        }
    }

    for (const auto& attachment : node_data.attachments)
    {
        attachment->handle_node_scene_host_update(old_scene_host, new_scene_host);
    }
}

void Node::handle_transform_update(uint64_t serial) const
{
    ERHE_PROFILE_FUNCTION

    const uint64_t effective_serial = (serial > 0)
        ? serial
        : Node_transforms::get_next_serial();

    node_data.transforms.update_serial = effective_serial;
    for (const auto& attachment : node_data.attachments)
    {
        attachment->handle_node_transform_update();
    }
}

void Node::handle_visibility_mask_update()
{
    ERHE_PROFILE_FUNCTION

    for (const auto& attachment : node_data.attachments)
    {
        attachment->handle_node_visibility_mask_update(node_data.visibility_mask);
    }
}

auto Node::root() -> std::weak_ptr<Node>
{
    const auto& current_parent = parent().lock();
    if (!current_parent)
    {
        return shared_from_this();
    }
    return current_parent->root();
}

void Node::update_transform(uint64_t serial) const
{
    ERHE_PROFILE_FUNCTION

    const auto& current_parent = parent().lock();
    if (!current_parent) {
        return;
    }

    serial = std::max(serial, current_parent->node_data.transforms.update_serial);

    if (node_data.transforms.update_serial >= serial) {
        return;
    }

    node_data.transforms.world_from_node.set(
        current_parent->world_from_node() * parent_from_node(),
        node_from_parent() * current_parent->node_from_world()
    );
    handle_transform_update(serial);
}

void Node::update_world_from_node()
{
    const auto& current_parent = parent().lock();
    if (current_parent)
    {
        node_data.transforms.world_from_node.set(
            current_parent->world_from_node() * parent_from_node(),
            node_from_parent() * current_parent->node_from_world()
        );
    }
    else
    {
        node_data.transforms.world_from_node.set(
            parent_from_node(),
            node_from_parent()
        );
    }
}

void Node::sanity_check() const
{
#if 1
    sanity_check_root_path(this);

    const auto& current_parent = parent().lock();
    if (current_parent)
    {
        bool child_found_in_parent = false;
        for (const auto& child : current_parent->children())
        {
            if (child.get() == this)
            {
                child_found_in_parent = true;
                break;
            }
        }
        if (!child_found_in_parent)
        {
            log->error(
                "Node {} parent {} does not have node as child",
                name(),
                current_parent->name()
            );
        }
    }

    for (const auto& child : node_data.children)
    {
        if (child->parent().lock().get() != this)
        {
            log->error(
                "Node {} child {} parent == {}",
                name(),
                child->name(),
                (child->parent().lock())
                    ? child->parent().lock()->name()
                    : "(none)"
            );
        }
        if (child->depth() != depth() + 1)
        {
            log->error(
                "Node {} depth = {}, child {} depth = {}",
                name(),
                depth(),
                child->name(),
                child->depth()
            );
        }
        if (child->get_scene_host() != get_scene_host())
        {
            log->error(
                "Scene host mismatch: parent node = {}, child node = {}",
                name(),
                child->name()
            );
        }
        child->sanity_check();
    }

    for (const auto& attachment : node_data.attachments)
    {
        auto* node = attachment->get_node();
        if (node != this)
        {
            log->error(
                "Node {} attachment {} node == {}",
                name(),
                attachment->node_attachment_type(),
                (node != nullptr)
                    ? node->name()
                    : "(none)"
            );
        }
    }
#endif
}

void Node::sanity_check_root_path(const Node* node) const
{
    const auto& current_parent = parent().lock();
    if (current_parent)
    {
        if (current_parent.get() == node)
        {
            log->error(
                "Node {} has itself as an ancestor",
                node->name()
            );
        }
        current_parent->sanity_check_root_path(node);
    }
}

auto Node::parent() const -> std::weak_ptr<Node>
{
    return node_data.parent;
}

auto Node::depth() const -> std::size_t
{
    return node_data.depth;
}

auto Node::children() const -> const std::vector<std::shared_ptr<Node>>&
{
    return node_data.children;
}

auto Node::mutable_children() -> std::vector<std::shared_ptr<Node>>&
{
    return node_data.children;
}

auto Node::attachments() const -> const std::vector<std::shared_ptr<INode_attachment>>&
{
    return node_data.attachments;
}

auto Node::get_visibility_mask() const -> uint64_t
{
    return node_data.visibility_mask;
}

void Node::set_visibility_mask(const uint64_t value)
{
    if (node_data.visibility_mask == value)
    {
        return;
    }
    node_data.visibility_mask = value;
    handle_visibility_mask_update();
}

auto Node::get_flag_bits() const -> uint64_t
{
    return node_data.flag_bits;
}

void Node::set_flag_bits(const uint64_t value)
{
    if (node_data.flag_bits == value)
    {
        return;
    }
    node_data.flag_bits = value;
}

auto Node::parent_from_node_transform() const -> const Transform&
{
    return node_data.transforms.parent_from_node;
}

auto Node::node_from_parent_transform() const -> const Transform
{
    return Transform::inverse(node_data.transforms.parent_from_node);
}

auto Node::parent_from_node() const -> glm::mat4
{
    return node_data.transforms.parent_from_node.matrix();
}

auto Node::world_from_node_transform() const -> const Transform&
{
    return node_data.transforms.world_from_node;
}

auto Node::node_from_world_transform() const -> const Transform
{
    return Transform::inverse(node_data.transforms.world_from_node);
}

auto Node::world_from_node() const -> glm::mat4
{
    return node_data.transforms.world_from_node.matrix();
}

auto Node::node_from_parent() const -> glm::mat4
{
    return node_data.transforms.parent_from_node.inverse_matrix();
}

auto Node::node_from_world() const -> glm::mat4
{
    return node_data.transforms.world_from_node.inverse_matrix();
}

auto Node::world_from_parent() const -> glm::mat4
{
    const auto& current_parent = parent().lock();
    if (current_parent)
    {
        return current_parent->world_from_node();
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

auto Node::transform_direction_from_world_to_local(const glm::vec3 d) const -> glm::vec3
{
    return glm::vec3{node_from_world() * glm::vec4{d, 0.0f}};
}

void Node::set_parent_from_node(const glm::mat4 m)
{
    node_data.transforms.parent_from_node.set(m);
    update_world_from_node();
    handle_transform_update(Node_transforms::get_next_serial());
}

void Node::set_parent_from_node(const Transform& transform)
{
    ERHE_PROFILE_FUNCTION

    node_data.transforms.parent_from_node = transform;
    update_world_from_node();
    handle_transform_update(Node_transforms::get_next_serial());
}

void Node::set_node_from_parent(const glm::mat4 matrix)
{
    node_data.transforms.parent_from_node.set(glm::inverse(matrix), matrix);
    update_world_from_node();
    handle_transform_update(Node_transforms::get_next_serial());
}

void Node::set_node_from_parent(const Transform& transform)
{
    node_data.transforms.parent_from_node = Transform::inverse(transform);
    update_world_from_node();
    handle_transform_update(Node_transforms::get_next_serial());
}

void Node::set_world_from_node(const glm::mat4 matrix)
{
    const auto& current_parent = parent().lock();
    if (current_parent)
    {
        set_parent_from_node(current_parent->node_from_world() * matrix);
    }
    else
    {
        set_parent_from_node(matrix);
    }
}

void Node::set_world_from_node(const Transform& transform)
{
    const auto& current_parent = parent().lock();
    if (current_parent)
    {
        set_parent_from_node(current_parent->node_from_world_transform() * transform);
    }
    else
    {
        set_parent_from_node(transform);
    }
}

auto is_empty(const Node* const node) -> bool
{
    return (node->get_flag_bits() & Node_flag_bit::is_empty) == Node_flag_bit::is_empty;
}

auto is_empty(const std::shared_ptr<Node>& node) -> bool
{
    return is_empty(node.get());
}

auto is_transform(Node* const node) -> bool
{
    return (node->get_flag_bits() & Node_flag_bit::is_transform) == Node_flag_bit::is_transform;
}

auto is_transform(const std::shared_ptr<Node>& node) -> bool
{
    return is_transform(node.get());
}

auto Node_data::diff_mask(const Node_data& lhs, const Node_data& rhs) -> unsigned int
{
    unsigned int mask{0};

    if (lhs.transforms.parent_from_node != rhs.transforms.parent_from_node) mask |= Node_data::bit_transform;
    if (lhs.transforms.world_from_node  != rhs.transforms.world_from_node ) mask |= Node_data::bit_transform;
    if (lhs.host                        != rhs.host                       ) mask |= Node_data::bit_host;
    // if (lhs.parent                      != rhs.parent                     ) mask |= Node_data::bit_parent;
    // children
    // attachments
    if (lhs.visibility_mask             != rhs.visibility_mask            ) mask |= Node_data::bit_visibility_mask;
    if (lhs.flag_bits                   != rhs.flag_bits                  ) mask |= Node_data::bit_flag_bits;
    if (lhs.depth                       != rhs.depth                      ) mask |= Node_data::bit_depth;
    if (lhs.wireframe_color             != rhs.wireframe_color            ) mask |= Node_data::bit_wireframe_color;
    if (lhs.name                        != rhs.name                       ) mask |= Node_data::bit_name;
    if (lhs.label                       != rhs.label                      ) mask |= Node_data::bit_label;
    return mask;
}

} // namespace erhe::scene
