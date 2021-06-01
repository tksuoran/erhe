#include "operations/insert_operation.hpp"
#include "scene/node_physics.hpp"
#include "scene/helpers.hpp"
#include "tools/selection_tool.hpp"
#include "erhe/physics/world.hpp"
#include "erhe/scene/scene.hpp"

namespace editor
{

Node_transform_operation::Node_transform_operation(const Context& context)
    : m_context{context}
{
}

Node_transform_operation::~Node_transform_operation() = default;


void Node_transform_operation::execute()
{
    m_context.node->transforms = m_context.after;
    // TODO update, handling trs tool node
}

void Node_transform_operation::undo()
{
    m_context.node->transforms = m_context.before;
}

Mesh_insert_remove_operation::Mesh_insert_remove_operation(const Context& context)
    : m_context{context}
{
}

Mesh_insert_remove_operation::~Mesh_insert_remove_operation() = default;

void Mesh_insert_remove_operation::execute()
{
    execute(m_context.mode);
}

void Mesh_insert_remove_operation::undo()
{
    execute(inverse(m_context.mode));
}

void Mesh_insert_remove_operation::execute(const Mode mode)
{
    VERIFY(m_context.mesh);
    VERIFY(m_context.node);
    if (mode == Mode::insert)
    {
        attach(m_context.layer,
               m_context.scene,
               m_context.physics_world,
               m_context.node,
               m_context.mesh, m_context.node_physics);
    }
    else
    {
        m_context.selection_tool->remove_from_selection(m_context.mesh);
        detach(m_context.layer,
               m_context.scene,
               m_context.physics_world,
               m_context.node,
               m_context.mesh,
               m_context.node_physics);
    }
}

Light_insert_remove_operation::Light_insert_remove_operation(const Context& context)
    : m_context{context}
{
}

Light_insert_remove_operation::~Light_insert_remove_operation() = default;

void Light_insert_remove_operation::execute()
{
    execute(m_context.mode);
}

void Light_insert_remove_operation::undo()
{
    execute(inverse(m_context.mode));
}

void Light_insert_remove_operation::execute(const Mode mode)
{
    VERIFY(m_context.light);
    VERIFY(m_context.node);
    if (mode == Mode::insert)
    {
        attach(m_context.layer,
               m_context.scene,
               m_context.node,
               m_context.light);
    }
    else
    {
        detach(m_context.layer,
               m_context.scene,
               m_context.node,
               m_context.light);
    }
}

}