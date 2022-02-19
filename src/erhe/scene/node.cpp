#include "erhe/scene/node.hpp"
#include "erhe/scene/log.hpp"
#include "erhe/toolkit/verify.hpp"
#include "erhe/toolkit/profile.hpp"

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
    : m_id   {}
    , m_name {name}
    , m_label{fmt::format("{}##Node{}", name, m_id.get_id())}
{
}

Node::~Node()
{
    sanity_check();

    for (auto& child : m_children)
    {
        child->on_detached_from(*this);
    }
    for (auto& attachment : m_attachments)
    {
        attachment->on_detached_from(this);
    }
    unparent();

    sanity_check();
}

auto Node::node_type() const -> const char*
{
    return "Node";
}

auto Node::is_selected() const -> bool
{
    return (m_visibility_mask & Node::c_visibility_selected) == Node::c_visibility_selected;
}

auto Node::name() const -> const std::string&
{
    return m_name;
}

auto Node::label() const -> const std::string&
{
    return m_label;
}

void Node::set_name(const std::string_view name)
{
    m_name = name;
    m_label = fmt::format("{}##Node{}", name, m_id.get_id());
}

void Node::attach(const std::shared_ptr<INode_attachment>& attachment)
{
    ERHE_PROFILE_FUNCTION

    ERHE_VERIFY(attachment);

    log.info(
        "{} ({}).attach({})\n",
        name(),
        node_type(),
        attachment->node_attachment_type()
    );

#ifndef NDEBUG
    const auto i = std::find(m_attachments.begin(), m_attachments.end(), attachment);
    if (i != m_attachments.end())
    {
        log.error("Attachment {} already attached to {}\n", attachment->node_attachment_type(), name());
        return;
    }
#endif

    m_attachments.push_back(attachment);
    attachment->on_attached_to(this);
    sanity_check();
    attachment->on_node_transform_changed();
    sanity_check();
}

auto Node::detach(INode_attachment* attachment) -> bool
{
    ERHE_PROFILE_FUNCTION

    if (!attachment)
    {
        log.warn("empty attachment, cannot detach\n");
        return false;
    }

    log.info(
        "{} ({}).detach({})\n",
        name(),
        node_type(),
        attachment->node_attachment_type()
    );

    auto* node = attachment->get_node();
    if (node != this)
    {
        log.warn(
            "Attachment {} node {} != this {}\n",
            attachment->node_attachment_type(),
            node
                ? node->name()
                : "(none)",
            name()
        );
        return false;
    }

    const auto i = std::remove_if(
        m_attachments.begin(),
        m_attachments.end(),
        [attachment](const std::shared_ptr<INode_attachment>& node_attachment)
        {
            return node_attachment.get() == attachment;
        }
    );
    if (i != m_attachments.end())
    {
        log.trace(
            "Removing {} attachment from node\n",
            attachment->node_attachment_type(),
            name()
        );
        m_attachments.erase(i, m_attachments.end());
        attachment->on_detached_from(this);
        sanity_check();
        return true;
    }

    log.warn(
        "Detaching {} from node {} failed - was not attached\n",
        attachment->node_attachment_type(),
        name()
    );

    return false;
}

auto Node::child_count() const -> size_t
{
    return m_children.size();
}

auto Node::get_id() const -> erhe::toolkit::Unique_id<Node>::id_type
{
    return m_id.get_id();
}

void Node::attach(const std::shared_ptr<Node>& child_node)
{
    if (child_node.get() == this)
    {
        log.error("Cannot attach node to itself");
        return;
    }

    ERHE_PROFILE_FUNCTION

    ERHE_VERIFY(child_node);

    log.info(
        "{} ({}).attach({} ({}))\n",
        name(),
        node_type(),
        child_node->name(),
        child_node->node_type()
    );

#ifndef NDEBUG
    const auto i = std::find(m_children.begin(), m_children.end(), child_node);
    if (i != m_children.end())
    {
        log.error("Attachment {} already attached to {}\n", child_node->name(), name());
        return;
    }
#endif

    m_children.push_back(child_node);
    child_node->on_attached_to(*this);
    sanity_check();
}

auto Node::detach(Node* child_node) -> bool
{
    ERHE_PROFILE_FUNCTION

    if (!child_node)
    {
        log.warn("empty child_node, cannot detach\n");
        return false;
    }

    log.info(
        "{} ({}).detach({} ({}))\n",
        name(),
        node_type(),
        child_node->name(),
        child_node->node_type()
    );

    if (child_node->parent() != this)
    {
        log.warn(
            "Child node {} parent {} != this {}\n",
            child_node->name(),
            child_node->parent()
                ? child_node->parent()->name()
                : "(none)",
            name()
        );
        return false;
    }

    const auto i = std::remove_if(
        m_children.begin(),
        m_children.end(),
        [child_node](const std::shared_ptr<Node>& node)
        {
            return node.get() == child_node;
        }
    );
    if (i != m_children.end())
    {
        log.trace("Removing attachment {} from node\n", child_node->name());
        m_children.erase(i, m_children.end());
        child_node->on_detached_from(*this);
        sanity_check();
        return true;
    }

    log.warn("Detaching {} from node {} failed - was not attached\n", child_node->name(), name());
    return false;
}

void Node::on_attached_to(Node& parent_node)
{
    if (m_parent == &parent_node)
    {
        return;
    }

    const auto& world_from_node = world_from_node_transform();
    //m_parent_node->attach(attachment.node);

    if (m_parent != nullptr)
    {
        m_parent->detach(this);
    }
    m_parent = &parent_node;
    set_depth_recursive(parent_node.depth() + 1);

    const auto parent_from_node = parent_node.node_from_world_transform() * world_from_node;
    set_parent_from_node(parent_from_node);
    update_transform_recursive();
    sanity_check();
}

void Node::set_depth_recursive(const size_t depth)
{
    if (m_depth == depth)
    {
        return;
    }
    m_depth = depth;
    for (const auto& child : m_children)
    {
        child->set_depth_recursive(depth + 1);
    }
}

void Node::on_detached_from(Node& node)
{
    if (m_parent != &node)
    {
        log.error(
            "Cannot detach node {} from {} - parent is different ({})\n",
            name(),
            node.name(),
            (m_parent != nullptr) ? m_parent->name() : "(nullptr)"
        );
        return;
    }
    m_parent = nullptr;
    set_depth_recursive(0);

    const auto& world_from_node = world_from_node_transform();
    set_parent_from_node(world_from_node);
    sanity_check();
}

void Node::on_transform_changed()
{
    for (const auto& attachment : m_attachments)
    {
        attachment->on_node_transform_changed();
    }
}

void Node::unparent()
{
    if (m_parent != nullptr)
    {
        m_parent->detach(this);
    }
}

auto Node::root() -> Node*
{
    if (m_parent == nullptr)
    {
        return this;
    }
    return m_parent->root();
}

auto Node::root() const -> const Node*
{
    if (m_parent == nullptr)
    {
        return this;
    }
    return m_parent->root();
}

void Node::update_transform(const uint64_t serial)
{
    ERHE_PROFILE_FUNCTION

    //log.trace("{} update_transform\n", name());
    if (serial != 0)
    {
        if (m_last_transform_update_serial == serial)
        {
            return;
        }
        if ((m_parent != nullptr) && (m_parent->m_last_transform_update_serial < serial)) {
            m_parent->update_transform(serial);
        }
    }

    if (m_parent != nullptr)
    {
        m_transforms.world_from_node.set(
            m_parent->world_from_node() * parent_from_node(),
            node_from_parent() * m_parent->node_from_world()
        );
    }
    else
    {
        m_transforms.world_from_node.set(
            parent_from_node(),
            node_from_parent()
        );
    }

    m_last_transform_update_serial = (serial != 0)
        ? serial
        : (m_parent != nullptr)
            ? m_parent->m_last_transform_update_serial
            : m_last_transform_update_serial;
}

void Node::update_transform_recursive(const uint64_t serial)
{
    update_transform(serial);
    for (auto& child_node : m_children)
    {
        child_node->update_transform_recursive(serial);
    }
}

void Node::sanity_check() const
{
    sanity_check_root_path(this);

    if (m_parent != nullptr)
    {
        bool child_found_in_parent = false;
        for (const auto& child : m_parent->children())
        {
            if (child.get() == this)
            {
                child_found_in_parent = true;
                break;
            }
        }
        if (!child_found_in_parent)
        {
            log.error("Node {} parent {} does not have node as child\n", name(), m_parent->name());
        }
    }

    for (const auto& child : m_children)
    {
        if (child->parent() != this)
        {
            log.error(
                "Node {} child {} parent == {}\n",
                name(),
                child->name(),
                (child->parent() != nullptr)
                    ? child->parent()->name()
                    : "(none)"
            );
        }
        if (child->depth() != depth() + 1)
        {
            log.error(
                "Node {} depth = {}, child {} depth = {}\n",
                name(),
                depth(),
                child->name(),
                child->depth()
            );
        }
        child->sanity_check();
    }

    for (const auto& attachment : m_attachments)
    {
        auto* node = attachment->get_node();
        if (node != this)
        {
            log.error(
                "Node {} attachment {} node == {}\n",
                name(),
                attachment->node_attachment_type(),
                (node != nullptr)
                    ? node->name()
                    : "(none)"
            );
        }
    }
}

void Node::sanity_check_root_path(const Node* node) const
{
    if (parent() == node)
    {
        log.error("Node {} has itself as an ancestor\n", node->name());
    }
    if (parent())
    {
        parent()->sanity_check_root_path(node);
    }
}

auto Node::parent() const -> Node*
{
    return m_parent;
}

auto Node::depth() const -> size_t
{
    return m_depth;
}

auto Node::children() const -> const std::vector<std::shared_ptr<Node>>&
{
    return m_children;
}

auto Node::attachments() const -> const std::vector<std::shared_ptr<INode_attachment>>&
{
    return m_attachments;
}

auto Node::visibility_mask() const -> uint64_t
{
    return m_visibility_mask;
}

auto Node::visibility_mask() -> uint64_t&
{
    return m_visibility_mask;
}

auto Node::flag_bits() const -> uint64_t
{
    return m_flag_bits;
}

auto Node::flag_bits() -> uint64_t&
{
    return m_flag_bits;
}

auto Node::parent_from_node_transform() const -> const Transform&
{
    return m_transforms.parent_from_node;
}

auto Node::node_from_parent_transform() const -> const Transform
{
    return Transform::inverse(m_transforms.parent_from_node);
}

auto Node::parent_from_node() const -> glm::mat4
{
    return m_transforms.parent_from_node.matrix();
}

auto Node::world_from_node_transform() const -> const Transform&
{
    return m_transforms.world_from_node;
}

auto Node::node_from_world_transform() const -> const Transform
{
    return Transform::inverse(m_transforms.world_from_node);
}

auto Node::world_from_node() const -> glm::mat4
{
    return m_transforms.world_from_node.matrix();
}

auto Node::node_from_parent() const -> glm::mat4
{
    return m_transforms.parent_from_node.inverse_matrix();
}

auto Node::node_from_world() const -> glm::mat4
{
    return m_transforms.world_from_node.inverse_matrix();
}

auto Node::world_from_parent() const -> glm::mat4
{
    if (m_parent != nullptr)
    {
        return m_parent->world_from_node();
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
    m_transforms.parent_from_node.set(m);
    m_last_transform_update_serial = 0;
    on_transform_changed();
}

void Node::set_parent_from_node(const Transform& transform)
{
    m_transforms.parent_from_node = transform;
    m_last_transform_update_serial = 0;
    on_transform_changed();
}

void Node::set_node_from_parent(const glm::mat4 matrix)
{
    m_transforms.parent_from_node.set(glm::inverse(matrix), matrix);
    m_last_transform_update_serial = 0;
    on_transform_changed();
}

void Node::set_node_from_parent(const Transform& transform)
{
    m_transforms.parent_from_node = Transform::inverse(transform);
    m_last_transform_update_serial = 0;
    on_transform_changed();
}

void Node::set_world_from_node(const glm::mat4 matrix)
{
    if (m_parent == nullptr)
    {
        set_parent_from_node(matrix);
    }
    else
    {
        set_parent_from_node(m_parent->node_from_world() * matrix);
    }
}

void Node::set_world_from_node(const Transform& transform)
{
    if (m_parent == nullptr)
    {
        set_parent_from_node(transform);
    }
    else
    {
        set_parent_from_node(m_parent->node_from_world_transform() * transform);
    }
}

auto is_empty(const Node* const node) -> bool
{
    return (node->flag_bits() & Node::c_flag_bit_is_empty) == Node::c_flag_bit_is_empty;
}

auto is_empty(const std::shared_ptr<Node>& node) -> bool
{
    return is_empty(node.get());
}

auto is_transform(Node* const node) -> bool
{
    return (node->flag_bits() & Node::c_flag_bit_is_transform) == Node::c_flag_bit_is_transform;
}

auto is_transform(const std::shared_ptr<Node>& node) -> bool
{
    return is_transform(node.get());
}

} // namespace erhe::scene
