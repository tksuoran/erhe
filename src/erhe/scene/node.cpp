#include "erhe/scene/node.hpp"
#include "erhe/scene/log.hpp"
#include "erhe/toolkit/verify.hpp"
#include "erhe/toolkit/tracy_client.hpp"

namespace erhe::scene
{

Node::Node(std::string_view name)
    : m_id   {}
    , m_label{fmt::format("{}##Node{}", name, m_id.get_id())}
    , m_name {name}
{
}

Node::~Node()
{
    for (auto& child : m_children)
    {
        child->on_detached_from(*this);
    }
    unparent();
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

auto Node::child_count() const -> size_t
{
    return m_children.size();
}

void Node::attach(const std::shared_ptr<Node>& child_node)
{
    ZoneScoped;

    VERIFY(child_node);

    log.info(
        "{} ({}).attach({} ({}))\n",
        name(),
        node_type(),
        child_node->name(),
        child_node->node_type()
    );

#ifndef NDEBUG
    const auto i = std::remove(m_children.begin(), m_children.end(), child_node);
    if (i != m_children.end())
    {
        log.error("Attachment {} already attached to {}\n", child_node->name(), name());
        return;
    }
#endif

    m_children.push_back(child_node);
    child_node->on_attached_to(*this);
}

auto Node::detach(const std::shared_ptr<Node>& child_node) -> bool
{
    ZoneScoped;

    VERIFY(child_node);

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
            child_node->parent()
                ? child_node->parent()->name()
                : "(none)",
            name()
        );
        return false;
    }

    if (!child_node)
    {
        log.warn("empty child_node, cannot detach\n");
        return false;
    }

    const auto i = std::remove(m_children.begin(), m_children.end(), child_node);
    if (i != m_children.end())
    {
        log.trace("Removing attachment {} from node\n", child_node->name());
        m_children.erase(i, m_children.end());
        child_node->on_detached_from(*this);
        return true;
    }

    log.warn("Detaching {} from node {} failed - was not attached\n", child_node->name(), name());
    return false;
}

void Node::on_attached_to(Node& node)
{
    if (m_parent != nullptr)
    {
        m_parent->detach(shared_from_this());
    }
    m_parent = &node;
    m_depth = node.m_depth + 1;
    update_transform_recursive();
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
    m_depth = 0;
}

void Node::unparent()
{
    if (m_parent != nullptr)
    {
        m_parent->detach(shared_from_this());
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
    ZoneScoped;

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

auto Node::parent_from_node() const -> glm::mat4
{
    return m_transforms.parent_from_node.matrix();
}

auto Node::world_from_node_transform() const -> const Transform&
{
    return m_transforms.world_from_node;
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
