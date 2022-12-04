#include "erhe/scene/scene.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/scene_log.hpp"
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
    const erhe::toolkit::Unique_id<Node>::id_type mesh_id
) const -> std::shared_ptr<Mesh>
{
    for (const auto& mesh : meshes)
    {
        if (mesh->get_id() == mesh_id)
        {
            return mesh;
        }
    }
    return {};
}

auto Mesh_layer::get_name() const -> const std::string&
{
    return name;
}

void Mesh_layer::add(const std::shared_ptr<Mesh>& mesh)
{
    ERHE_VERIFY(mesh);

#ifndef NDEBUG
    const auto i = std::find(meshes.begin(), meshes.end(), mesh);
    if (i != meshes.end())
    {
        log->error("mesh {} already in layer meshes", mesh->get_name());
    }
    else
#endif
    {
        meshes.push_back(mesh);
    }
}

void Mesh_layer::remove(
    const std::shared_ptr<Mesh>& mesh
)
{
    ERHE_VERIFY(mesh);

    const auto i = std::remove(meshes.begin(), meshes.end(), mesh);
    if (i == meshes.end())
    {
        log->error("mesh {} not in layer meshes", mesh->get_name());
    }
    else
    {
        meshes.erase(i, meshes.end());
    }
}

Light_layer::Light_layer(const std::string_view name)
    : name{name}
{
}

auto Light_layer::get_light_by_id(
    const erhe::toolkit::Unique_id<Node>::id_type light_id
) const -> std::shared_ptr<Light>
{
    for (const auto& light : lights)
    {
        if (light->get_id() == light_id)
        {
            return light;
        }
    }
    return {};
}

auto Light_layer::get_name() const -> const std::string&
{
    return name;
}

void Light_layer::add(
    const std::shared_ptr<Light>& light
)
{
    ERHE_VERIFY(light);

    log->trace("add_to_light_layer(light = {})", light->get_name());

    {
#ifndef NDEBUG
        const auto i = std::find(lights.begin(), lights.end(), light);
        if (i != lights.end())
        {
            log->error("light {} already in layer lights", light->get_name());
        }
        else
#endif
        {
            lights.push_back(light);
        }
    }
}

void Light_layer::remove(
    const std::shared_ptr<Light>& light
)
{
    ERHE_VERIFY(light);

    log->trace("remove_from_scene_layer(light = {})`", light->get_name());

    const auto i = std::remove(lights.begin(), lights.end(), light);

    if (i == lights.end())
    {
        log->error("light {} not in layer lights", light->get_name());
    }
    else
    {
        lights.erase(i, lights.end());
    }
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

auto Scene::get_mesh_layer_by_id(
    const erhe::toolkit::Unique_id<Mesh_layer>::id_type id
) const -> std::shared_ptr<Mesh_layer>
{
    for (const auto& layer : mesh_layers)
    {
        if (layer->id.get_id() == id)
        {
            return layer;
        }
    }
    return {};
}

auto Scene::get_light_layer_by_id(
    const erhe::toolkit::Unique_id<Light_layer>::id_type id
) const -> std::shared_ptr<Light_layer>
{
    for (const auto& layer : light_layers)
    {
        if (layer->id.get_id() == id)
        {
            return layer;
        }
    }
    return {};
}

void Scene::sanity_check() const
{
#if !defined(NDEBUG)
    root_node->sanity_check();
#endif
}

void Scene::sort_transform_nodes()
{
    log->trace("sorting {} nodes", flat_node_vector.size());

    std::sort(
        flat_node_vector.begin(),
        flat_node_vector.end(),
        [](const auto& lhs, const auto& rhs)
        {
            return lhs->get_depth() < rhs->get_depth();
        }
    );
    nodes_sorted = true;
}

void Scene::update_node_transforms()
{
    ERHE_PROFILE_FUNCTION

    if (!nodes_sorted)
    {
        sort_transform_nodes();
    }

    for (auto& node : flat_node_vector)
    {
        node->update_transform(0);
    }
}

Scene::Scene(
    erhe::message_bus::Message_bus<Scene_message>* const message_bus,
    Scene_host* const                                    host
)
    : host     {host}
    , root_node{std::make_shared<erhe::scene::Node>("root")}
{
    // The implicit root node has a valid (identity) transform
    root_node->node_data.host = host;
    root_node->node_data.transforms.update_serial = 1;
    Message_bus_node::initialize(message_bus);
}

Scene::~Scene() = default;

void Scene::register_node(
    const std::shared_ptr<erhe::scene::Node>& node
)
{
    ERHE_PROFILE_FUNCTION

#ifndef NDEBUG
    const auto i = std::find(flat_node_vector.begin(), flat_node_vector.end(), node);
    if (i != flat_node_vector.end())
    {
        log->error("{} {} already in scene nodes", node->type_name(), node->get_name());
    }
    else
#endif
    {
        ERHE_VERIFY(node->node_data.host == nullptr);
        node->node_data.host = this->host;
        flat_node_vector.push_back(node);
        nodes_sorted = false;
    }

    ERHE_VERIFY(!node->parent().expired());

    if ((node->get_flag_bits() & Scene_item_flags::no_message) == 0)
    {
        send(
            Scene_message{
                .event_type = Scene_event_type::node_added_to_scene,
                .lhs        = node
            }
        );
    }
}

void Scene::unregister_node(
    const std::shared_ptr<erhe::scene::Node>& node
)
{
    const auto i = std::remove(flat_node_vector.begin(), flat_node_vector.end(), node);
    if (i == flat_node_vector.end())
    {
        log->error("Node {} not in scene nodes", node->get_name());
    }
    else
    {
        node->node_data.host = nullptr;
        flat_node_vector.erase(i, flat_node_vector.end());
    }

    sanity_check();

    if ((node->get_flag_bits() & Scene_item_flags::no_message) == 0)
    {
        send(
            Scene_message{
                .event_type = Scene_event_type::node_removed_from_scene,
                .lhs        = node
            }
        );
    }
}

void Scene::register_camera(const std::shared_ptr<Camera>& camera)
{
#ifndef NDEBUG
    const auto i = std::find(cameras.begin(), cameras.end(), camera);
    if (i != cameras.end())
    {
        log->error("camera {} already in scene cameras", camera->get_name());
    }
    else
#endif
    {
        cameras.push_back(camera);
    }
}

void Scene::unregister_camera(const std::shared_ptr<Camera>& camera)
{
    ERHE_VERIFY(camera);
    const auto i = std::remove(cameras.begin(), cameras.end(), camera);
    if (i == cameras.end())
    {
        log->error("camera {} not in scene cameras", camera->get_name());
    }
    else
    {
        cameras.erase(i, cameras.end());
    }
}

void Scene::register_mesh(const std::shared_ptr<Mesh>& mesh)
{
    ERHE_VERIFY(mesh);
    auto mesh_layer = get_mesh_layer_by_id(mesh->mesh_data.layer_id);
    if (mesh_layer)
    {
        mesh_layer->add(mesh);
    }
}

void Scene::unregister_mesh(const std::shared_ptr<Mesh>& mesh)
{
    ERHE_VERIFY(mesh);
    auto mesh_layer = get_mesh_layer_by_id(mesh->mesh_data.layer_id);
    if (mesh_layer)
    {
        mesh_layer->remove(mesh);
    }
}

void Scene::register_light(const std::shared_ptr<Light>& light)
{
    ERHE_VERIFY(light);
    auto light_layer = get_light_layer_by_id(light->layer_id);
    if (light_layer)
    {
        light_layer->add(light);
    }
    else
    {
        log->error("light {} layer not found", light->get_name());
    }
}

void Scene::unregister_light(const std::shared_ptr<Light>& light)
{
    ERHE_VERIFY(light);
    auto light_layer = get_light_layer_by_id(light->layer_id);
    if (light_layer)
    {
        light_layer->remove(light);
    }
    else
    {
        log->error("light {} layer not found", light->get_name());
    }
}

} // namespace erhe::scene

