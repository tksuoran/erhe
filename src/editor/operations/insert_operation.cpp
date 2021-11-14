#include "operations/insert_operation.hpp"
#include "scene/node_physics.hpp"
#include "scene/helpers.hpp"
#include "tools/selection_tool.hpp"
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
    m_context.node->set_parent_from_node(m_context.parent_from_node_after);
}

void Node_transform_operation::undo()
{
    m_context.node->set_parent_from_node(m_context.parent_from_node_before);
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
    VERIFY(m_context.mesh);

    m_context.scene.sanity_check();

    if (mode == Mode::insert)
    {
        add_to_scene_layer(m_context.scene, m_context.layer, m_context.mesh);
        if (m_context.node_physics)
        {
            add_to_physics_world(m_context.scene, m_context.physics_world, m_context.node_physics);
            if (m_context.parent)
            {
                m_context.parent->attach(m_context.node_physics);
            }
        }
        else
        {
            if (m_context.parent)
            {
                m_context.parent->attach(m_context.mesh);
            }
        }
    }
    else
    {
        m_context.selection_tool->remove_from_selection(m_context.mesh);
        remove_from_scene_layer(m_context.scene, m_context.layer, m_context.mesh);
        if (m_context.node_physics)
        {
            remove_from_physics_world(m_context.scene, m_context.physics_world, m_context.node_physics);
            if (m_context.parent)
            {
                m_context.parent->detach(m_context.node_physics.get());
            }
        }
        else
        {
            if (m_context.parent)
            {
                m_context.parent->detach(m_context.mesh.get());
            }
        }
    }

    m_context.scene.sanity_check();
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
    if (mode == Mode::insert)
    {
        add_to_scene_layer(m_context.scene, m_context.layer, m_context.light);
        if (m_context.parent)
        {
            m_context.parent->attach(m_context.light);
        }
    }
    else
    {
        remove_from_scene_layer(m_context.scene, m_context.layer, m_context.light);
        if (m_context.parent)
        {
            m_context.parent->detach(m_context.light.get());
        }
    }
}

}