#include "erhe/scene/node.hpp"
#include "erhe/scene/log.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::scene
{

Node::Node(std::string_view name)
    : m_name{name}
    , m_id{}
    , m_label{fmt::format("{}::{}", name, m_id.get_id())}
{
}

Node::~Node()
{
    for (const auto& attachment : attachments)
    {
        attachment->on_detach(*this);
    }
}

void Node::set_name(std::string_view name)
{
    m_name = name;
    m_label = fmt::format("{}##Node{}", name, m_id.get_id());
}

void Node::attach(const std::shared_ptr<INode_attachment>& attachment)
{
    attachments.push_back(attachment);
    attachment->on_attach(*this);
}

auto Node::detach(const std::shared_ptr<INode_attachment>& attachment) -> bool
{
    // TODO Check that attachment node points back to us?
    if (!attachment)
    {
        log.warn("No attachment to detach\n");
        return false;
    }

    const auto i = std::remove(attachments.begin(),
                               attachments.end(),
                               attachment);
    if (i != attachments.end())
    {
        log.trace("Removing attachment {} from node\n", attachment->name());
        attachments.erase(i, attachments.end());
        attachment->on_detach(*this);
        return true;
    }

    log.warn("Detaching {} from node failed - was not attached\n", attachment->name());
    return false;
}

void Node::update(const uint32_t update_serial, const bool cache_enable)
{
    if (cache_enable)
    {
        if (m_updated)
        {
            return;
        }

        if (m_last_update_serial == update_serial)
        {
            return;
        }
    }

    m_last_update_serial = update_serial;
    VERIFY(parent != this);

    if (parent != nullptr)
    {
        parent->update(update_serial, cache_enable);
        transforms.world_from_node.set(parent->world_from_node() * parent_from_node(),
                                       node_from_parent() * parent->node_from_world());
    }
    else
    {
        transforms.world_from_node.set(parent_from_node(),
                                       node_from_parent());
    }
}

auto Node::parent_from_node() const -> glm::mat4
{
    return transforms.parent_from_node.matrix();
}

auto Node::world_from_node() const -> glm::mat4
{
    return transforms.world_from_node.matrix();
}

auto Node::node_from_parent() const -> glm::mat4
{
    return transforms.parent_from_node.inverse_matrix();
}

auto Node::node_from_world() const -> glm::mat4
{
    return transforms.world_from_node.inverse_matrix();
}

auto Node::world_from_parent() const -> glm::mat4
{
    if (parent != nullptr)
    {
        return parent->world_from_node();
    }
    return glm::mat4(1);
}

auto Node::position_in_world() const -> glm::vec4
{
    return world_from_node() * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
}

auto Node::direction_in_world() const -> glm::vec4
{
    return world_from_node() * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
}

auto Node::root() -> Node*
{
    if (parent == nullptr)
    {
        return this;
    }
    return parent->root();
}

auto Node::root() const -> const Node*
{
    if (parent == nullptr)
    {
        return this;
    }
    return parent->root();
}

} // namespace erhe::scene
