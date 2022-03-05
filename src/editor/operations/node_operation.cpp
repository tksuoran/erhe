#include "operations/node_operation.hpp"
#include "tools/selection_tool.hpp"

#include "erhe/scene/scene.hpp"

#include <sstream>

namespace editor
{

Node_operation::Node_operation(Context&& context)
    : m_context{std::move(context)}
{
}

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

Node_operation::~Node_operation() = default;

void Node_operation::execute() const
{
    for (auto& entry : m_entries)
    {
        m_context.scene.sanity_check();
        entry.node->node_data = entry.after;
        m_context.scene.sanity_check();
    }
}

void Node_operation::undo() const
{
    for (const auto& entry : m_entries)
    {
        m_context.scene.sanity_check();
        entry.node->node_data = entry.before;
        m_context.scene.sanity_check();
    }
}

void Node_operation::make_entries()
{
    m_context.scene.sanity_check();

    m_selection_tool = m_context.selection_tool;
    for (auto& node : m_context.selection_tool->selection())
    {
        Entry entry{
            .node   = node,
            .before = node->node_data,
            .after  = node->node_data
        };

        add_entry(std::move(entry));
    }

    m_context.scene.sanity_check();
}

void Node_operation::add_entry(Entry&& entry)
{
    m_entries.emplace_back(entry);
}

} // namespace editor
