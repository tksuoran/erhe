#include "operations/insert_operation.hpp"
#include "scene/node_physics.hpp"
#include "scene/helpers.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"

#include "erhe/scene/scene.hpp"

#include <sstream>

namespace editor
{

Node_transform_operation::Node_transform_operation(const Parameters& parameters)
    : m_parameters{parameters}
{
}

Node_transform_operation::~Node_transform_operation() noexcept
{
}

auto Node_transform_operation::describe() const -> std::string
{
    std::stringstream ss;
    ss << "Node_transform " << m_parameters.node->name();
    return ss.str();
}

void Node_transform_operation::execute(const Operation_context&)
{
    m_parameters.node->set_parent_from_node(m_parameters.parent_from_node_after);
}

void Node_transform_operation::undo(const Operation_context&)
{
    m_parameters.node->set_parent_from_node(m_parameters.parent_from_node_before);
}

//
auto Node_insert_remove_operation::describe() const -> std::string
{
    std::stringstream ss;
    switch (m_parameters.mode)
    {
        //using enum Mode;
        case Mode::insert: ss << "Node_insert "; break;
        case Mode::remove: ss << "Node_remove "; break;
        default: break;
    }
    ss << m_parameters.node->name();
    return ss.str();
}

Node_insert_remove_operation::Node_insert_remove_operation(const Parameters& parameters)
    : m_parameters{parameters}
{
}

Node_insert_remove_operation::~Node_insert_remove_operation() noexcept
{
}

void Node_insert_remove_operation::execute(const Operation_context& context)
{
    execute(context, m_parameters.mode);
}

void Node_insert_remove_operation::undo(const Operation_context& context)
{
    execute(context, inverse(m_parameters.mode));
}

void Node_insert_remove_operation::execute(
    const Operation_context& context,
    const Mode               mode
) const
{
    ERHE_VERIFY(m_parameters.node);

    auto* scene_root = m_parameters.scene_root;
    auto& scene      = scene_root->scene();
    scene.sanity_check();

    if (mode == Mode::insert)
    {
        if (m_parameters.parent)
        {
            m_parameters.parent->attach(m_parameters.node);
        }
    }
    else
    {
        //m_context.selection_tool->remove_from_selection(m_context.mesh);
        scene.remove_node(m_parameters.node);
        if (m_parameters.parent)
        {
            m_parameters.parent->detach(m_parameters.node.get());
        }
    }

    auto selection_tool = context.components->get<Selection_tool>();
    if (selection_tool)
    {
        selection_tool->update_selection_from_node(m_parameters.node, mode == Mode::insert);
    }
    scene.sanity_check();
}

//

auto Mesh_insert_remove_operation::describe() const -> std::string
{
    std::stringstream ss;
    switch (m_parameters.mode)
    {
        //using enum Mode;
        case Mode::insert: ss << "Mesh_insert "; break;
        case Mode::remove: ss << "Mesh_remove "; break;
        default: break;
    }
    ss << m_parameters.mesh->name();
    return ss.str();
}

Mesh_insert_remove_operation::Mesh_insert_remove_operation(const Parameters& parameters)
    : m_parameters{parameters}
{
}

Mesh_insert_remove_operation::~Mesh_insert_remove_operation() noexcept
{
}

void Mesh_insert_remove_operation::execute(const Operation_context& context)
{
    execute(context, m_parameters.mode);
}

void Mesh_insert_remove_operation::undo(const Operation_context& context)
{
    execute(context, inverse(m_parameters.mode));
}

void Mesh_insert_remove_operation::execute(
    const Operation_context& context,
    const Mode               mode
) const
{
    ERHE_VERIFY(m_parameters.mesh);

    auto* scene_root     = m_parameters.scene_root;
    auto& scene          = scene_root->scene();
    auto& physics_world  = scene_root->physics_world();
    auto& raytrace_scene = scene_root->raytrace_scene();
    scene.sanity_check();

    if (mode == Mode::insert)
    {
        auto& layer = *scene_root->layers().content();
        scene.add_to_mesh_layer(layer, m_parameters.mesh);
        if (m_parameters.parent)
        {
            m_parameters.parent->attach(m_parameters.mesh);
        }
        if (m_parameters.node_physics)
        {
            add_to_physics_world(physics_world, m_parameters.node_physics);
        }
        if (m_parameters.node_raytrace)
        {
            add_to_raytrace_scene(raytrace_scene, m_parameters.node_raytrace);
        }
    }
    else
    {
        scene.remove(m_parameters.mesh);
        //m_context.selection_tool->remove_from_selection(m_context.mesh);
        if (m_parameters.node_physics)
        {
            remove_from_physics_world(physics_world, *m_parameters.node_physics.get());
        }
        if (m_parameters.node_raytrace)
        {
            remove_from_raytrace_scene(raytrace_scene, m_parameters.node_raytrace);
        }
        if (m_parameters.parent)
        {
            m_parameters.parent->detach(m_parameters.mesh.get(), true, true);
        }
    }

    auto selection_tool = context.components->get<Selection_tool>();
    if (selection_tool)
    {
        selection_tool->update_selection_from_node(m_parameters.mesh, mode == Mode::insert);
    }
    scene.sanity_check();
}

//
//

auto Light_insert_remove_operation::describe() const -> std::string
{
    std::stringstream ss;
    switch (m_parameters.mode)
    {
        //using enum Mode;
        case Mode::insert: ss << "Light_insert "; break;
        case Mode::remove: ss << "Light_remove "; break;
        default: break;
    }
    ss << m_parameters.light->name();
    return ss.str();
}

Light_insert_remove_operation::Light_insert_remove_operation(const Parameters& parameters)
    : m_parameters{parameters}
{
}

Light_insert_remove_operation::~Light_insert_remove_operation() noexcept
{
}

void Light_insert_remove_operation::execute(const Operation_context& context)
{
    execute(context, m_parameters.mode);
}

void Light_insert_remove_operation::undo(const Operation_context& context)
{
    execute(context, inverse(m_parameters.mode));
}

void Light_insert_remove_operation::execute(
    const Operation_context& context,
    const Mode               mode
)
{
    ERHE_VERIFY(m_parameters.light);

    auto* scene_root = m_parameters.scene_root;
    auto& scene      = scene_root->scene();
    scene.sanity_check();

    if (mode == Mode::insert)
    {
        auto& layer = *scene_root->layers().light();
        scene.add_to_light_layer(layer, m_parameters.light);
        if (m_parameters.parent)
        {
            m_parameters.parent->attach(m_parameters.light);
        }
    }
    else
    {
        scene.remove(m_parameters.light);
        if (m_parameters.parent)
        {
            m_parameters.parent->detach(m_parameters.light.get());
        }
    }

    auto selection_tool = context.components->get<Selection_tool>();
    if (selection_tool)
    {
        selection_tool->update_selection_from_node(m_parameters.light, mode == Mode::insert);
    }

    scene.sanity_check();
}

//
//

auto Camera_insert_remove_operation::describe() const -> std::string
{
    std::stringstream ss;
    switch (m_parameters.mode)
    {
        //using enum Mode;
        case Mode::insert: ss << "Camera_insert "; break;
        case Mode::remove: ss << "Camera_remove "; break;
        default: break;
    }
    ss << m_parameters.camera->name();
    return ss.str();
}

Camera_insert_remove_operation::Camera_insert_remove_operation(const Parameters& parameters)
    : m_parameters{parameters}
{
}

Camera_insert_remove_operation::~Camera_insert_remove_operation() noexcept
{
}

void Camera_insert_remove_operation::execute(const Operation_context& context)
{
    execute(context, m_parameters.mode);
}

void Camera_insert_remove_operation::undo(const Operation_context& context)
{
    execute(context, inverse(m_parameters.mode));
}

void Camera_insert_remove_operation::execute(
    const Operation_context& context,
    const Mode               mode
) const
{
    ERHE_VERIFY(m_parameters.camera);

    auto* scene_root = m_parameters.scene_root;
    auto& scene      = scene_root->scene();
    scene.sanity_check();

    if (mode == Mode::insert)
    {
        scene.add(m_parameters.camera);
        if (m_parameters.parent)
        {
            m_parameters.parent->attach(m_parameters.camera);
        }
    }
    else
    {
        scene.remove(m_parameters.camera);
        if (m_parameters.parent)
        {
            m_parameters.parent->detach(m_parameters.camera.get());
        }
    }

    auto selection_tool = context.components->get<Selection_tool>();
    if (selection_tool)
    {
        selection_tool->update_selection_from_node(m_parameters.camera, mode == Mode::insert);
    }
    scene.sanity_check();
}


} // namespace editor

