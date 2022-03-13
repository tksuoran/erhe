#include "operations/attach_detach_operation.hpp"
#include "tools/selection_tool.hpp"
#include "scene/helpers.hpp"
#include "scene/node_physics.hpp"
#include "log.hpp"

#include "erhe/scene/scene.hpp"

#include <memory>
#include <sstream>

namespace editor
{

Attach_detach_operation::Attach_detach_operation(Parameters&& parameters)
    : m_parameters{std::move(parameters)}
{
    const auto& selection = parameters.selection_tool->selection();
    if (selection.size() < 2)
    {
        return;
    }

    bool first = true;
    for (auto& node : selection)
    {
        if (first)
        {
            m_parent_node = node;
            first = false;
        }
        else
        {
            //auto node_physics = get_physics_node(node.get());

            if (parameters.attach)
            {
                if (node->parent().expired())
                {
                    m_entries.emplace_back(node);
                }
                else
                {
                    log_tools.info(
                        "Node {} is already attached and thus cannot be attached to {}\n",
                        node->name(),
                        m_parent_node->name()
                    );
                }
            }
            else
            {
                if (node->parent().lock() == m_parent_node)
                {
                    m_entries.emplace_back(node);
                }
                else
                {
                    log_tools.info(
                        "Node {} is not attached to {} and thus cannot be detached from it\n",
                        node->name(),
                        m_parent_node->name()
                    );
                }
            }
        }
    }
}

auto Attach_detach_operation::describe() const -> std::string
{
    std::stringstream ss;
    ss << (m_parameters.attach ? "Attach " : "Detach ");
    bool first = true;
    for (const auto& entry : m_entries)
    {
        ERHE_VERIFY(entry.node);
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
    ss << (m_parameters.attach ? "to " : "from ");
    ERHE_VERIFY(m_parent_node);
    ss << m_parent_node->name();
    return ss.str();
}

void Attach_detach_operation::execute(const Operation_context&)
{
    execute(m_parameters.attach);
}

void Attach_detach_operation::undo(const Operation_context&)
{
    execute(!m_parameters.attach);
}

void Attach_detach_operation::execute(bool attach) const
{
    m_parameters.scene.sanity_check();

    ERHE_VERIFY(m_parent_node);

    for (auto& entry : m_entries)
    {
        if (attach)
        {
            m_parent_node->attach(entry.node);
        }
        else
        {
            entry.node->unparent();
        }
    }

    m_parameters.scene.sanity_check();
}

} // namespace editor
