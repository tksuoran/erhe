#include "erhe/scene/scene.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/log.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

#include <algorithm>

namespace erhe::scene
{

Mesh_layer::Mesh_layer(
    const std::string_view name,
    const uint64_t         flags
)
    : name {name}
    , flags{flags}
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

//auto Scene::get_node_by_id(
//    const erhe::toolkit::Unique_id<Node>::id_type id
//) const -> std::shared_ptr<Node>
//{
//    for (const auto& node : flat_node_vector)
//    {
//        if (node->get_id() == id)
//        {
//            return node;
//        }
//    }
//    return {};
//}

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
    //for (auto& node : nodes)
    //{
    //    node->sanity_check();
    //}
    root_node->sanity_check();
}

void Scene::sort_transform_nodes()
{
    log.trace("sorting {} nodes\n", flat_node_vector.size());

    std::sort(
        flat_node_vector.begin(),
        flat_node_vector.end(),
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

    for (auto& node : flat_node_vector)
    {
        node->update_transform(serial);
    }
}

Scene::Scene()
    : root_node{std::make_shared<erhe::scene::Node>("root")}
{
}

void Scene::add_node(
    const std::shared_ptr<erhe::scene::Node>& node
)
{
#ifndef NDEBUG
    const auto i = std::find(flat_node_vector.begin(), flat_node_vector.end(), node);
    if (i != flat_node_vector.end())
    {
        log.error("{} {} already in scene nodes\n", node->node_type(), node->name());
    }
    else
#endif
    {
        flat_node_vector.push_back(node);
        nodes_sorted = false;
    }

    if (node->parent().expired())
    {
        root_node->attach(node);
    }
}

void Scene::add_to_mesh_layer(
    Mesh_layer&                  layer,
    const std::shared_ptr<Mesh>& mesh
)
{
    ERHE_VERIFY(mesh);

    log.trace("add_to_mesh_layer(mesh = {})\n", mesh->name());

    auto& meshes = layer.meshes;
#ifndef NDEBUG
    const auto i = std::find(meshes.begin(), meshes.end(), mesh);
    if (i != meshes.end())
    {
        log.error("mesh {} already in layer meshes\n", mesh->name());
    }
    else
#endif
    {
        meshes.push_back(mesh);
    }

    add_node(mesh);
}

void Scene::add_to_light_layer(
    Light_layer&                  layer,
    const std::shared_ptr<Light>& light
)
{
    ERHE_VERIFY(light);

    log.trace("add_to_light_layer(light = {})\n", light->name());

    {
        auto& lights = layer.lights;
#ifndef NDEBUG
        const auto i = std::find(lights.begin(), lights.end(), light);
        if (i != lights.end())
        {
            log.error("light {} already in layer lights\n", light->name());
        }
        else
#endif
        {
            lights.push_back(light);
        }
    }

    add_node(light);
}

void Scene::add(
    const std::shared_ptr<erhe::scene::Camera>& camera
)
{
    ERHE_VERIFY(camera);

    log.trace("add_to_scene(camera = {})\n", camera->name());

    {
#ifndef NDEBUG
        const auto i = std::find(cameras.begin(), cameras.end(), camera);
        if (i != cameras.end())
        {
            log.error("camera {} already in scene cameras\n", camera->name());
        }
        else
#endif
        {
            cameras.push_back(camera);
        }
    }

    add_node(camera);
}

void Scene::remove_from_mesh_layer(
    Mesh_layer&                  layer,
    const std::shared_ptr<Mesh>& mesh
)
{
    ERHE_VERIFY(mesh);

    auto& meshes = layer.meshes;
    const auto i = std::remove(meshes.begin(), meshes.end(), mesh);
    if (i == meshes.end())
    {
        log.error("mesh {} not in layer meshes\n", mesh->name());
    }
    else
    {
        meshes.erase(i, meshes.end());
    }
}

void Scene::remove_node(
    const std::shared_ptr<erhe::scene::Node>& node
)
{
    const auto i = std::remove(flat_node_vector.begin(), flat_node_vector.end(), node);
    if (i == flat_node_vector.end())
    {
        log.error("{} {} not in scene nodes\n", node->node_type(), node->name());
    }
    else
    {
        flat_node_vector.erase(i, flat_node_vector.end());
    }
}

void Scene::remove(
    const std::shared_ptr<erhe::scene::Mesh>& mesh
)
{
    for (auto& layer : mesh_layers)
    {
        auto& meshes = layer->meshes;
        const auto i = std::remove(meshes.begin(), meshes.end(), mesh);
        if (i != meshes.end())
        {
            meshes.erase(i, meshes.end());
        }
    }

    remove_node(mesh);
}

void Scene::remove_from_light_layer(
    Light_layer&                  layer,
    const std::shared_ptr<Light>& light
)
{
    ERHE_VERIFY(light);

    log.trace("remove_from_scene_layer(light = {})\n", light->name());

    auto& lights = layer.lights;
    const auto i = std::remove(lights.begin(), lights.end(), light);

    if (i == lights.end())
    {
        log.error("light {} not in layer lights\n", light->name());
    }
    else
    {
        lights.erase(i, lights.end());
    }
}

void Scene::remove(const std::shared_ptr<Light>& light)
{
    for (auto& layer : light_layers)
    {
        auto& lights = layer->lights;
        const auto i = std::remove(lights.begin(), lights.end(), light);
        if (i != lights.end())
        {
            lights.erase(i, lights.end());
        }
    }

    remove_node(light);
}

void Scene::remove(
    const std::shared_ptr<erhe::scene::Camera>& camera
)
{
    ERHE_VERIFY(camera);

    log.trace("remove(camera = {})\n", camera->name());

    const auto i = std::remove(cameras.begin(), cameras.end(), camera);

    if (i == cameras.end())
    {
        log.error("camera {} not in scene cameras\n", camera->name());
    }
    else
    {
        cameras.erase(i, cameras.end());
    }

    remove_node(camera);
}

} // namespace erhe::scene

