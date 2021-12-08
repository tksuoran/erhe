#include "erhe/scene/scene.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/log.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/toolkit/profile.hpp"

#include <algorithm>

namespace erhe::scene
{

Mesh_layer::Mesh_layer(const std::string_view name)
    : name{name}
{
}

auto Mesh_layer::get_mesh_by_id(
    const erhe::toolkit::Unique_id<Node>::id_type id
) const -> std::shared_ptr<Mesh>
{
    for (const auto& mesh : meshes)
    {
        if (mesh->get_id() == id)
        {
            return mesh;
        }
    }
    return {};
}

Light_layer::Light_layer(const std::string_view name)
    : name{name}
{
}

auto Light_layer::get_light_by_id(
    const erhe::toolkit::Unique_id<Node>::id_type id
) const -> std::shared_ptr<Light>
{
    for (const auto& light : lights)
    {
        if (light->get_id() == id)
        {
            return light;
        }
    }
    return {};
}

auto Scene::get_node_by_id(
    const erhe::toolkit::Unique_id<Node>::id_type id
) const -> std::shared_ptr<Node>
{
    for (const auto& node : nodes)
    {
        if (node->get_id() == id)
        {
            return node;
        }
    }
    return {};
}

auto Scene::get_camera_by_id(
    const erhe::toolkit::Unique_id<Node>::id_type id
) const -> std::shared_ptr<Camera>
{
    for (const auto& camera : cameras)
    {
        if (camera->get_id() == id)
        {
            return camera;
        }
    }
    return {};
}

auto Scene::get_mesh_by_id(
    const erhe::toolkit::Unique_id<Node>::id_type id
) const -> std::shared_ptr<Mesh>
{
    for (const auto& layer : mesh_layers)
    {
        const auto& mesh = layer->get_mesh_by_id(id);
        if (mesh)
        {
            return mesh;
        }
    }
    return {};
}

auto Scene::get_light_by_id(
    const erhe::toolkit::Unique_id<Node>::id_type id
) const -> std::shared_ptr<Light>
{
    for (const auto& layer : light_layers)
    {
        const auto& light = layer->get_light_by_id(id);
        if (light)
        {
            return light;
        }
    }
    return {};
}

void Scene::sanity_check() const
{
    for (auto& node : nodes)
    {
        node->sanity_check();
    }
}

void Scene::sort_transform_nodes()
{
    log.trace("sorting {} nodes\n", nodes.size());

    std::sort(
        nodes.begin(),
        nodes.end(),
        [](const auto& lhs, const auto& rhs)
        {
            return lhs->depth() < rhs->depth();
        }
    );
    nodes_sorted = true;
}

auto Scene::transform_update_serial() -> uint64_t
{
    return ++m_transform_update_serial;
}

void Scene::update_node_transforms()
{
    ERHE_PROFILE_FUNCTION

    if (!nodes_sorted)
    {
        sort_transform_nodes();
    }

    const auto serial = transform_update_serial();

    for (auto& node : nodes)
    {
        node->update_transform(serial);
    }
}

} // namespace erhe::scene

