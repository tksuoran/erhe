#include "operations/insert_operation.hpp"
#include "scene/node_physics.hpp"
#include "scene/helpers.hpp"
#include "tools/selection_tool.hpp"

#include "erhe/scene/scene.hpp"

#include <sstream>

namespace editor
{

Node_transform_operation::Node_transform_operation(const Context& context)
    : m_context{context}
{
}

Node_transform_operation::~Node_transform_operation() = default;

auto Node_transform_operation::describe() const -> std::string
{
    std::stringstream ss;
    ss << "Node_transform " << m_context.node->name();
    return ss.str();
}

void Node_transform_operation::execute() const
{
    m_context.node->set_parent_from_node(m_context.parent_from_node_after);
}

void Node_transform_operation::undo() const
{
    m_context.node->set_parent_from_node(m_context.parent_from_node_before);
}

auto Mesh_insert_remove_operation::describe() const -> std::string
{
    std::stringstream ss;
    switch (m_context.mode)
    {
        using enum Mode;
        case insert: ss << "Mesh_insert "; break;
        case remove: ss << "Mesh_remove "; break;
        default: break;
    }
    ss << m_context.mesh->name();
    return ss.str();
}

Mesh_insert_remove_operation::Mesh_insert_remove_operation(const Context& context)
    : m_context{context}
{
    m_selection_before = context.selection_tool->selection();
    m_selection_after = m_selection_before;

    if (context.mode == Mode::remove)
    {
        const auto i = std::remove(m_selection_before.begin(), m_selection_before.end(), context.mesh);
        if (i != m_selection_before.end()) 
        {
            m_selection_before.erase(i, m_selection_before.end());
        }
    }
}

Mesh_insert_remove_operation::~Mesh_insert_remove_operation() = default;

void Mesh_insert_remove_operation::execute() const
{
    execute(m_context.mode);
}

void Mesh_insert_remove_operation::undo() const
{
    execute(inverse(m_context.mode));
}

void Mesh_insert_remove_operation::execute(const Mode mode) const
{
    ERHE_VERIFY(m_context.mesh);
    ERHE_VERIFY(m_context.mesh);

    m_context.scene.sanity_check();

    if (mode == Mode::insert)
    {
        add_to_scene_layer(m_context.scene, m_context.layer, m_context.mesh);
        if (m_context.parent)
        {
            m_context.parent->attach(m_context.mesh);
        }
        if (m_context.node_physics)
        {
            add_to_physics_world(m_context.physics_world, m_context.node_physics);
        }
        m_context.selection_tool->set_selection(m_selection_after);
    }
    else
    {
        m_context.selection_tool->remove_from_selection(m_context.mesh);
        remove_from_scene_layer(m_context.scene, m_context.layer, m_context.mesh);
        if (m_context.node_physics)
        {
            remove_from_physics_world(m_context.physics_world, *m_context.node_physics.get());
        }
        if (m_context.parent)
        {
            m_context.parent->detach(m_context.mesh.get());
        }
        m_context.selection_tool->set_selection(m_selection_before);
    }

    m_context.scene.sanity_check();
}

auto Light_insert_remove_operation::describe() const -> std::string
{
    std::stringstream ss;
    switch (m_context.mode)
    {
        using enum Mode;
        case insert: ss << "Light_insert "; break;
        case remove: ss << "Light_remove "; break;
        default: break;
    }
    ss << m_context.light->name();
    return ss.str();
}

Light_insert_remove_operation::Light_insert_remove_operation(const Context& context)
    : m_context{context}
{
    m_selection_before = context.selection_tool->selection();
    m_selection_after  = m_selection_before;

    if (context.mode == Mode::remove)
    {
        const auto i = std::remove(m_selection_after.begin(), m_selection_after.end(), context.light);
        if (i != m_selection_after.end()) 
        {
            m_selection_after.erase(i, m_selection_after.end());
        }
    }
}

Light_insert_remove_operation::~Light_insert_remove_operation() = default;

void Light_insert_remove_operation::execute() const
{
    execute(m_context.mode);
}

void Light_insert_remove_operation::undo() const
{
    execute(inverse(m_context.mode));
}

void Light_insert_remove_operation::execute(const Mode mode) const
{
    ERHE_VERIFY(m_context.light);
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
