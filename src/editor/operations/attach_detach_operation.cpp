#include "operations/attach_detach_operation.hpp"
#include "tools/selection_tool.hpp"
#include "scene/helpers.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_manager.hpp"
#include "log.hpp"

#include "erhe/scene/scene.hpp"

#include <memory>
#include <sstream>

namespace editor
{

Attach_detach_operation::Attach_detach_operation(Context& context)
    : m_context(context)
{
    auto& selection = context.selection_tool->selection();
    if (selection.size() < 2)
    {
        return;
    }

    using namespace glm;

    bool first = true;
    for (auto node : selection)
    {
        if (first)
        {
            m_parent_node = node;
            first = false;
        }
        else
        {
            //auto node_physics = get_physics_node(node.get());

            if (context.attach)
            {
                if (node->parent() == nullptr)
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
                if (node->parent() == m_parent_node.get())
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
    ss << (m_context.attach ? "Attach " : "Detach ");
    bool first = true;
    for (const auto& entry : m_entries)
    {
        VERIFY(entry.node);
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
    ss << (m_context.attach ? "to " : "from ");
    VERIFY(m_parent_node);
    ss << m_parent_node->name();
    return ss.str();
}

void Attach_detach_operation::execute() const
{
    execute(m_context.attach);
}

void Attach_detach_operation::undo() const
{
    execute(!m_context.attach);
}

void Attach_detach_operation::execute(bool attach) const
{
    m_context.scene.sanity_check();

    VERIFY(m_parent_node);

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

    m_context.scene.sanity_check();
}

} // namespace editor
