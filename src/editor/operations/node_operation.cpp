#include "operations/node_operation.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"

#include "erhe/scene/scene.hpp"

#include <fmt/format.h>

#include <sstream>

namespace editor
{

auto Node_operation::describe() const -> std::string
{
    std::stringstream ss;
    bool first = true;
    for (const auto& entry : m_entries)
    {
        if (first)
        {
            first = false;
        }
        else
        {
            ss << ", ";
        }
        ss << entry.node->name();
    }
    return ss.str();
}

void Node_operation::execute(const Operation_context&)
{
    for (auto& entry : m_entries)
    {
        entry.node->node_data = entry.after;
    }
}

void Node_operation::undo(const Operation_context&)
{
    for (const auto& entry : m_entries)
    {
        entry.node->node_data = entry.before;
    }
}

void Node_operation::add_entry(Entry&& entry)
{
    m_entries.emplace_back(entry);
}

auto Node_attach_operation::describe() const -> std::string
{
    return fmt::format(
        "Node_attach_operation(child_node = {}, parent before = {}, parent after = {})",
        m_child_node->name(),
        m_parent_before->name(),
        m_parent_after->name()
    );
}

Node_attach_operation::Node_attach_operation() = default;

Node_attach_operation::Node_attach_operation(
    const std::shared_ptr<erhe::scene::Node>& parent,
    const std::shared_ptr<erhe::scene::Node>& child
)
    : m_child_node   {child}
    , m_parent_before{child->parent().lock()}
    , m_parent_after {parent}
{
}

Node_attach_operation::Node_attach_operation(const Node_attach_operation& other)
    : m_child_node         {other.m_child_node         }
    , m_parent_before      {other.m_parent_before      }
    , m_parent_before_index{other.m_parent_before_index}
    , m_parent_after       {other.m_parent_after       }
    , m_parent_after_index {other.m_parent_after_index }
{
}

Node_attach_operation::Node_attach_operation(Node_attach_operation&& other)
    : m_child_node         {other.m_child_node         }
    , m_parent_before      {other.m_parent_before      }
    , m_parent_before_index{other.m_parent_before_index}
    , m_parent_after       {other.m_parent_after       }
    , m_parent_after_index {other.m_parent_after_index }
{
    other.m_child_node   .reset();
    other.m_parent_before.reset();
    other.m_parent_after .reset();
}

auto Node_attach_operation::operator=(const Node_attach_operation& other) -> Node_attach_operation&
{
    m_child_node          = other.m_child_node;
    m_parent_before       = other.m_parent_before;
    m_parent_before_index = other.m_parent_before_index;
    m_parent_after        = other.m_parent_after;
    m_parent_after_index  = other.m_parent_after_index;
    return *this;
}

auto Node_attach_operation::operator=(Node_attach_operation&& other) -> Node_attach_operation&
{
    m_child_node          = other.m_child_node;
    m_parent_before       = other.m_parent_before;
    m_parent_before_index = other.m_parent_before_index;
    m_parent_after        = other.m_parent_after;
    m_parent_after_index  = other.m_parent_after_index;
    other.m_child_node   .reset();
    other.m_parent_before.reset();
    other.m_parent_after .reset();
    return *this;
}

void Node_attach_operation::execute(const Operation_context& context)
{
    ERHE_VERIFY(m_child_node->parent().lock() == m_parent_before);

    m_parent_before_index = m_child_node->get_index_in_parent();
    if (m_parent_after)
    {
        m_parent_after_index  = m_parent_after->child_count();
        m_parent_after->attach(m_child_node, m_parent_after_index);
    }
    else
    {
        m_child_node->unparent();
    }

    if (context.components != nullptr)
    {
        auto selection_tool = context.components->get<Selection_tool>();
        if (selection_tool)
        {
            selection_tool->sanity_check();
        }
    }
}

void Node_attach_operation::undo(const Operation_context& context)
{
    ERHE_VERIFY(m_child_node->parent().lock() == m_parent_after);

    if (m_parent_before)
    {
        m_parent_before->attach(m_child_node, m_parent_before_index);
    }
    else if (m_parent_after)
    {
        m_child_node->unparent();
    }

    if (context.components != nullptr)
    {
        auto selection_tool = context.components->get<Selection_tool>();
        if (selection_tool)
        {
            selection_tool->sanity_check();
        }
    }
}

} // namespace editor
