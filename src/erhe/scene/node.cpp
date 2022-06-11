#include "erhe/scene/node.hpp"
#include "erhe/scene/log.hpp"
#include "erhe/toolkit/verify.hpp"
#include "erhe/toolkit/profile.hpp"

#include <fmt/format.h>

namespace erhe::scene
{

INode_attachment::~INode_attachment()
{
    auto* const host = get_node();
    if (host != nullptr)
    {
        host->detach(this);
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

Node::Node(std::string_view name)
    : m_id{}
{
    node_data.name  = name;
    node_data.label = fmt::format("{}##Node{}", name, m_id.get_id());
}

Node::~Node()
{
    sanity_check();

    for (auto& child : node_data.children)
    {
        child->on_detached_from(*this);
    }
    for (auto& attachment : node_data.attachments)
    {
        attachment->on_detached_from(this);
    }
    unparent();

    const auto& root_node = root().lock();
    if (root_node.get() != this)
    {
        root_node->detach(this);
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
    attachment->on_attached_to(this);
    sanity_check();
    update_transform();
    on_visibility_mask_changed();
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
        attachment->on_detached_from(this);
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

auto Node::child_count() const -> size_t
{
    return node_data.children.size();
}

auto Node::get_id() const -> erhe::toolkit::Unique_id<Node>::id_type
{
    return m_id.get_id();
}

auto Node::get_index_in_parent() const -> size_t
{
    const auto& current_parent = parent().lock();
    if (current_parent)
    {
        const auto index = current_parent->get_index_of_child(this);
        return index.has_value() ? index.value() : 0;
    }
    return 0;
}

auto Node::get_index_of_child(const Node* child) const -> nonstd::optional<size_t>
{
    for (
        size_t i = 0, end = node_data.children.size();
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

void Node::attach(
    const std::shared_ptr<Node>& child_node,
    const bool                   primary_operation
)
{
    ERHE_PROFILE_FUNCTION

    if (child_node.get() == this)
    {
        log->error("Cannot attach node to itself");
        return;
    }
    if (is_ancestor(child_node.get()))
    {
        log->error("Cannot attach node to ancestor");
        return;
    }

    ERHE_VERIFY(child_node);

    SPDLOG_LOGGER_TRACE(
        log,
        "{} ({}).attach({} ({}))",
        name(),
        node_type(),
        child_node->name(),
        child_node->node_type()
    );

#ifndef NDEBUG
    const auto i = std::find(node_data.children.begin(), node_data.children.end(), child_node);
    if (i != node_data.children.end())
    {
        log->error("Node {} already attached to {}", child_node->name(), name());
        return;
    }
#endif

    const auto child_parent_from_node = node_from_world_transform() * child_node->world_from_node_transform();

    const auto& current_parent = child_node->parent().lock();
    if (current_parent)
    {
        current_parent->detach(child_node.get(), false);
    }

    node_data.children.push_back(child_node);
    child_node->set_parent                (shared_from_this());
    child_node->set_parent_from_node      (child_parent_from_node);
    child_node->set_depth_recursive       (depth() + 1);
    child_node->update_transform_recursive();
    if (primary_operation)
    {
        child_node->on_attached();
    }
    sanity_check();
}

void Node::attach(
    const std::shared_ptr<Node>& child_node,
    const size_t                 position,
    const bool                   primary_operation
)
{
    if (child_node.get() == this)
    {
        log->error("Cannot attach node to itself");
        return;
    }
    if (is_ancestor(child_node.get()))
    {
        log->error("Cannot attach ancestor to node");
        return;
    }

    ERHE_PROFILE_FUNCTION

    ERHE_VERIFY(child_node);

    log->trace(
        "{} ({}).attach_at(child node = {} ({}), position = {})",
        name(),
        node_type(),
        child_node->name(),
        child_node->node_type(),
        position
    );

#ifndef NDEBUG
    const auto i = std::find(node_data.children.begin(), node_data.children.end(), child_node);
    if (i != node_data.children.end())
    {
        log->error("Node {} already attached to {}", child_node->name(), name());
        return;
    }
#endif

    const auto child_parent_from_node = node_from_world_transform() * child_node->world_from_node_transform();

    const auto& current_parent = child_node->parent().lock();
    if (current_parent)
    {
        current_parent->detach(child_node.get(), false);
    }

    node_data.children.insert(node_data.children.begin() + position, child_node);
    child_node->set_parent                (shared_from_this());
    child_node->set_parent_from_node      (child_parent_from_node);
    child_node->set_depth_recursive       (depth() + 1);
    child_node->update_transform_recursive();
    if (primary_operation)
    {
        child_node->on_attached();
    }
    sanity_check();
}

auto Node::detach(
    Node*      child_node,
    const bool primary_operation
) -> bool
{
    ERHE_PROFILE_FUNCTION

    if (!child_node)
    {
        log->warn("empty child_node, cannot detach");
        return false;
    }

    const auto& root_node = root().lock();

    // Detaching from the implicit root is nop.
    // However as secondary (temporary) operation, it is allowed.
    if (
        primary_operation &&
        (root_node.get() == this)
    )
    {
        return true;
    }

    log->trace(
        "{} ({}).detach({} ({}))",
        name(),
        node_type(),
        child_node->name(),
        child_node->node_type()
    );

    const auto& parent = child_node->parent().lock();

    if (parent.get() != this)
    {
        log->warn(
            "Child node {} parent {} != this {}",
            child_node->name(),
            parent
                ? parent->name()
                : "(none)",
            name()
        );
        return false;
    }

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
        log->trace("Removing attachment {} from node", child_node->name());

        const auto& world_from_node = child_node->world_from_node_transform();

        node_data.children.erase(i, node_data.children.end());

        const auto& current_parent = child_node->parent().lock();
        if (current_parent.get() != this)
        {
            log->error(
                "Cannot detach node {} from {} - parent is different ({})",
                child_node->name(),
                name(),
                current_parent
                    ? current_parent->name()
                    : "(nullptr)"
            );
            return false;
        }
        child_node->set_parent          ({}); // so that attach won't call detach
        child_node->set_parent_from_node(world_from_node);
        child_node->set_depth_recursive (0);

        // Attach to the implicit root node,
        if (
            primary_operation &&
            (root_node.get() != this)
        )
        {
            root_node->attach(child_node->shared_from_this(), false);
        }
        child_node->set_world_from_node(world_from_node);

        if (primary_operation)
        {
            child_node->on_detached_from(*this);
        }
        sanity_check();
        return true;
    }

    log->warn("Detaching {} from node {} failed - was not attached", child_node->name(), name());
    return false;
}

void Node::set_parent(const std::weak_ptr<Node>& new_parent_node)
{
    node_data.parent = new_parent_node;
}

void Node::on_attached()
{
    sanity_check();
}

void Node::set_depth_recursive(const size_t depth)
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

void Node::on_detached_from(Node& node)
{
    static_cast<void>(node);
    sanity_check();
}

void Node::on_transform_changed()
{
    ERHE_PROFILE_FUNCTION

    for (const auto& attachment : node_data.attachments)
    {
        attachment->on_node_transform_changed();
    }
}

void Node::on_visibility_mask_changed()
{
    ERHE_PROFILE_FUNCTION

    for (const auto& attachment : node_data.attachments)
    {
        attachment->on_node_visibility_mask_changed(node_data.visibility_mask);
    }
}

void Node::unparent()
{
    const auto& current_parent = parent().lock();
    const auto& root_node      = root().lock();
    if (
        (current_parent != root_node) &&
        (root_node.get() != this)
    )
    {
        current_parent->detach(this);
        root_node->attach(shared_from_this());
        //current_parent->detach(this);
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

//auto Node::root() const -> std::weak_ptr<Node>
//{
//    const auto& current_parent = parent().lock();
//    if (current_parent)
//    {
//        return std::weak_ptr<Node>(shared_from_this());
//    }
//    return current_parent->root();
//}

void Node::update_transform(const uint64_t serial)
{
    ERHE_PROFILE_FUNCTION

    //log.trace("{} update_transform\n", name());
    const auto& current_parent = parent().lock();

    if (serial != 0)
    {
        if (node_data.last_transform_update_serial == serial)
        {
            return;
        }
        if (
            current_parent &&
            (current_parent->node_data.last_transform_update_serial < serial)
        )
        {
            current_parent->update_transform(serial);
        }
    }

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

    node_data.last_transform_update_serial = (serial != 0)
        ? serial
        : current_parent
            ? current_parent->node_data.last_transform_update_serial
            : node_data.last_transform_update_serial;

    on_transform_changed();
}

void Node::update_transform_recursive(const uint64_t serial)
{
    ERHE_PROFILE_FUNCTION

    update_transform(serial);
    for (auto& child_node : node_data.children)
    {
        child_node->update_transform_recursive(serial);
    }
}

void Node::sanity_check() const
{
#if 0
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

auto Node::depth() const -> size_t
{
    return node_data.depth;
}

auto Node::children() const -> const std::vector<std::shared_ptr<Node>>&
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
    on_visibility_mask_changed();
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
    node_data.last_transform_update_serial = 0;
    on_transform_changed();
}

void Node::set_parent_from_node(const Transform& transform)
{
    ERHE_PROFILE_FUNCTION

    node_data.transforms.parent_from_node = transform;
    node_data.last_transform_update_serial = 0;
    on_transform_changed();
}

void Node::set_node_from_parent(const glm::mat4 matrix)
{
    node_data.transforms.parent_from_node.set(glm::inverse(matrix), matrix);
    node_data.last_transform_update_serial = 0;
    on_transform_changed();
}

void Node::set_node_from_parent(const Transform& transform)
{
    node_data.transforms.parent_from_node = Transform::inverse(transform);
    node_data.last_transform_update_serial = 0;
    on_transform_changed();
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

} // namespace erhe::scene
