#include "operations/insert_operation.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_manager.hpp"
#include "tools/selection_tool.hpp"
#include "erhe/physics/world.hpp"
#include "erhe/scene/scene.hpp"

namespace editor
{

Node_transform_operation::Node_transform_operation(Context context)
    : m_context{context}
{
}

void Node_transform_operation::execute()
{
    m_context.node->transforms = m_context.after;
    // TODO update, handling trs tool node
}

void Node_transform_operation::undo()
{
    m_context.node->transforms = m_context.before;
}

Mesh_insert_remove_operation::Mesh_insert_remove_operation(Context& context)
    : m_context{context}
{
}

void Mesh_insert_remove_operation::execute()
{
    execute(m_context.mode);
}

void Mesh_insert_remove_operation::undo()
{
    execute(inverse(m_context.mode));
}

void Mesh_insert_remove_operation::execute(Mode mode)
{
    // TODO Do this as compound operation (node add, mesh add)
    VERIFY(m_context.scene_manager);
    VERIFY(m_context.mesh);
    VERIFY(m_context.node);
    auto& meshes = m_context.scene_manager->content_layer()->meshes;
    auto& nodes  = m_context.scene_manager->scene().nodes;
    auto& world  = m_context.scene_manager->physics_world();
    auto  node   = m_context.node;
    if (mode == Mode::insert)
    {
        m_context.scene_manager->attach(m_context.node, m_context.mesh, m_context.node_physics);
    }
    else
    {
        m_context.selection_tool->remove_from_selection(m_context.mesh);
        m_context.scene_manager->detach(m_context.node, m_context.mesh, m_context.node_physics);
    }
}

Light_insert_remove_operation::Light_insert_remove_operation(Context& context)
    : m_context{context}
{
}

void Light_insert_remove_operation::execute()
{
    execute(m_context.mode);
}

void Light_insert_remove_operation::undo()
{
    execute(inverse(m_context.mode));
}

void Light_insert_remove_operation::execute(Mode mode)
{
    VERIFY(m_context.scene_manager);
    VERIFY(m_context.item);
    auto& lights = m_context.scene_manager->content_layer()->lights;
    auto& nodes  = m_context.scene_manager->scene().nodes;
    auto  node   = m_context.node;
    if (mode == Mode::insert)
    {
        lights.push_back(m_context.item);
        nodes.push_back(node);
    }
    else
    {
        lights.erase(std::remove(lights.begin(), lights.end(), m_context.item), lights.end());
        nodes.erase(std::remove(nodes.begin(), nodes.end(), m_context.item->node()), nodes.end());
    }
}

}