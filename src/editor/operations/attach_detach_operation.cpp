#include "operations/attach_detach_operation.hpp"
#include "tools/selection_tool.hpp"
#include "scene/helpers.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_manager.hpp"
#include "log.hpp"
#include "erhe/scene/scene.hpp"

#include <memory>

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
            auto node_physics = get_physics_node(node.get());
            auto child_node = node_physics ? node_physics : node;

            if (context.attach)
            {
                if (child_node->parent() == nullptr)
                {
                    m_attachments.emplace_back(child_node);
                }
                else
                {
                    log_tools.info("Node {} is already attached and thus cannot be attached to {}\n", child_node->name(), m_parent_node->name());
                }
            }
            else
            {
                if (child_node->parent() == m_parent_node.get())
                {
                    m_attachments.emplace_back(child_node);
                }
                else
                {
                    log_tools.info("Node {} is not attached to {} and thus cannot be detached from it\n", child_node->name(), m_parent_node->name());
                }
            }
        }
    }
}

void Attach_detach_operation::execute()
{
    execute(m_context.attach);
}

void Attach_detach_operation::undo()
{
    execute(!m_context.attach);
}

void Attach_detach_operation::execute(bool attach)
{
    m_context.scene.sanity_check();

    VERIFY(m_parent_node);

    if (attach)
    {
        for (auto& attachment : m_attachments)
        {
            //const auto world_from_node = attachment.node->world_from_node_transform();
            m_parent_node->attach(attachment.node);
            //const auto parent_from_node = m_parent_node->node_from_world_transform() * world_from_node;
            //attachment.node->set_parent_from_node(parent_from_node);
        }
    }
    else
    {
        for (auto& attachment : m_attachments)
        {
            //auto world_from_node = attachment.node->world_from_node_transform();
            attachment.node->unparent();
            //attachment.node->set_parent_from_node(world_from_node);
        }
    }

    m_context.scene.sanity_check();
}

} // namespace editor
