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
    const std::shared_ptr<erhe::scene::Node>& child,
    const std::shared_ptr<erhe::scene::Node>  place_before_node,
    const std::shared_ptr<erhe::scene::Node>  place_after_node
)
    : m_child_node       {child}
    , m_parent_before    {child->parent().lock()}
    , m_parent_after     {parent}
    , m_place_before_node{place_before_node}
    , m_place_after_node {place_after_node }
{
    // Only at most one place can be set
    ERHE_VERIFY(!place_before_node || !place_after_node);
}

void Node_attach_operation::execute(const Operation_context& context)
{
    ERHE_VERIFY(m_child_node->parent().lock() == m_parent_before);

    m_parent_before_index = m_child_node->get_index_in_parent();
    if (m_parent_after)
    {
        m_parent_after_index = m_place_before_node
            ? m_place_before_node->get_index_in_parent()
            : m_place_after_node
                ? m_place_after_node ->get_index_in_parent() + 1
                : m_parent_after->child_count();

        m_parent_after->attach(m_child_node, m_parent_after_index);
    }
    else
    {
        m_child_node->unparent();
    }

    if (context.components != nullptr)
    {
        const auto& selection_tool = context.components->get<Selection_tool>();
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
        const auto& selection_tool = context.components->get<Selection_tool>();
        if (selection_tool)
        {
            selection_tool->sanity_check();
        }
    }
}

//////

Node_reposition_in_parent_operation::Node_reposition_in_parent_operation() = default;

Node_reposition_in_parent_operation::Node_reposition_in_parent_operation(
    const std::shared_ptr<erhe::scene::Node>& child_node,
    const std::shared_ptr<erhe::scene::Node>  place_before_node,
    const std::shared_ptr<erhe::scene::Node>  place_after_node
)
    : m_child_node       {child_node }
    , m_place_before_node{place_before_node}
    , m_place_after_node {place_after_node }
{
    // Exactly one of before_node and after_node must be set
    ERHE_VERIFY(static_cast<bool>(place_before_node) != static_cast<bool>(place_after_node));
}

auto Node_reposition_in_parent_operation::describe() const -> std::string
{
    return fmt::format(
        "Node_reposition_in_parent_operation(child_node = {}, place_before_node = {}, place_after_node = {})",
        m_child_node->name(),
        m_place_before_node ? m_place_before_node->name() : "()",
        m_place_after_node  ? m_place_after_node ->name() : "()"
    );
}

void Node_reposition_in_parent_operation::execute(const Operation_context& context)
{
    auto parent = m_child_node->parent().lock();
    ERHE_VERIFY(parent);

    auto& parent_children = parent->mutable_children();

    m_before_index = m_child_node->get_index_in_parent();

    ERHE_VERIFY(m_before_index < parent_children.size());
    ERHE_VERIFY(parent_children[m_before_index] == m_child_node);
    parent_children.erase(parent_children.begin() + m_before_index);

    const std::size_t after_index = m_place_before_node
        ? m_place_before_node->get_index_in_parent()
        : m_place_after_node ->get_index_in_parent() + 1;
    ERHE_VERIFY(after_index <= parent_children.size());

    parent_children.insert(parent_children.begin() + after_index, m_child_node);

    if (context.components != nullptr)
    {
        const auto& selection_tool = context.components->get<Selection_tool>();
        if (selection_tool)
        {
            selection_tool->sanity_check();
        }
    }
}

void Node_reposition_in_parent_operation::undo(const Operation_context& context)
{
    auto parent = m_child_node->parent().lock();
    ERHE_VERIFY(parent);

    auto& parent_children = parent->mutable_children();

    const std::size_t after_index = m_child_node->get_index_in_parent();

    ERHE_VERIFY(m_before_index <= parent_children.size());
    ERHE_VERIFY(after_index <= parent_children.size());
    ERHE_VERIFY(parent_children[after_index] == m_child_node);
    parent_children.erase(parent_children.begin() + after_index);
    parent_children.insert(parent_children.begin() + m_before_index, m_child_node);

    if (context.components != nullptr)
    {
        const auto& selection_tool = context.components->get<Selection_tool>();
        if (selection_tool)
        {
            selection_tool->sanity_check();
        }
    }
}


} // namespace editor
