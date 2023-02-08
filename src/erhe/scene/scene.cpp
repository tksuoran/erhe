#include "erhe/scene/scene.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/scene/scene_log.hpp"
#include "erhe/scene/scene_message_bus.hpp"
#include "erhe/toolkit/bit_helpers.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

#include <algorithm>

namespace erhe::scene
{

Mesh_layer::Mesh_layer(
    const std::string_view name,
    const uint64_t         flags,
    const Layer_id         id
)
    : name {name}
    , flags{flags}
    , id   {id}
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

Light_layer::Light_layer(
    const std::string_view name,
    const Layer_id         id
)
    : name{name}
    , id  {id}
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
    for (const auto& camera : m_cameras)
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
    for (const auto& layer : m_mesh_layers)
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
    for (const auto& layer : m_light_layers)
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
    const Layer_id id
) const -> std::shared_ptr<Mesh_layer>
{
    for (const auto& layer : m_mesh_layers)
    {
        if (layer->id == id)
        {
            return layer;
        }
    }
    return {};
}

auto Scene::get_light_layer_by_id(
    const Layer_id id
) const -> std::shared_ptr<Light_layer>
{
    for (const auto& layer : m_light_layers)
    {
        if (layer->id == id)
        {
            return layer;
        }
    }
    return {};
}

auto Scene::get_root_node() const -> std::shared_ptr<erhe::scene::Node>
{
    return m_root_node;
}

[[nodiscard]] auto Scene::get_cameras() -> std::vector<std::shared_ptr<Camera>>&
{
    return m_cameras;
}

[[nodiscard]] auto Scene::get_cameras() const -> const std::vector<std::shared_ptr<Camera>>&
{
    return m_cameras;
}

[[nodiscard]] auto Scene::get_flat_nodes() -> std::vector<std::shared_ptr<Node>>&
{
    return m_flat_node_vector;
}

[[nodiscard]] auto Scene::get_flat_nodes() const -> const std::vector<std::shared_ptr<Node>>&
{
    return m_flat_node_vector;
}

[[nodiscard]] auto Scene::get_mesh_layers() -> std::vector<std::shared_ptr<Mesh_layer>>&
{
    return m_mesh_layers;
}

[[nodiscard]] auto Scene::get_mesh_layers() const -> const std::vector<std::shared_ptr<Mesh_layer>>&
{
    return m_mesh_layers;
}

[[nodiscard]] auto Scene::get_light_layers() -> std::vector<std::shared_ptr<Light_layer>>&
{
    return m_light_layers;
}

[[nodiscard]] auto Scene::get_light_layers() const -> const std::vector<std::shared_ptr<Light_layer>>&
{
    return m_light_layers;
}

void Scene::sanity_check() const
{
#if !defined(NDEBUG)
    m_root_node->sanity_check();
#endif
}

void Scene::sort_transform_nodes()
{
    log->trace("sorting {} nodes", m_flat_node_vector.size());

    std::sort(
        m_flat_node_vector.begin(),
        m_flat_node_vector.end(),
        [](const auto& lhs, const auto& rhs)
        {
            return lhs->get_depth() < rhs->get_depth();
        }
    );
    m_nodes_sorted = true;
}

void Scene::update_node_transforms()
{
    ERHE_PROFILE_FUNCTION

    if (!m_nodes_sorted)
    {
        sort_transform_nodes();
    }

    for (auto& node : m_flat_node_vector)
    {
        node->update_transform(0);
    }
}

Scene::Scene(
    const std::string_view name,
    Scene_host* const      host
)
    : Item       {name}
    , m_host     {host}
    , m_root_node{std::make_shared<erhe::scene::Node>("root")}
{
    enable_flag_bits(Item_flags::content);
    // The implicit root node has a valid (identity) transform
    m_root_node->node_data.host = host;
    m_root_node->node_data.transforms.update_serial = 1;
}

Scene::~Scene()
{
    sanity_check();

    m_root_node->recursive_remove();

    m_flat_node_vector.clear();
    m_mesh_layers.clear();
    m_light_layers.clear();
    m_cameras.clear();
    m_root_node.reset();
}

auto Scene::get_type() const -> uint64_t
{
    return Item_type::scene;
}

auto Scene::type_name() const -> const char*
{
    return "Scene";
}

void Scene::add_mesh_layer(const std::shared_ptr<Mesh_layer>& mesh_layer)
{
    m_mesh_layers.push_back(mesh_layer);
}

void Scene::add_light_layer(const std::shared_ptr<Light_layer>& light_layer)
{
    m_light_layers.push_back(light_layer);
}

void Scene::register_node(
    const std::shared_ptr<erhe::scene::Node>& node
)
{
    ERHE_PROFILE_FUNCTION

#ifndef NDEBUG
    const auto i = std::find(m_flat_node_vector.begin(), m_flat_node_vector.end(), node);
    if (i != m_flat_node_vector.end())
    {
        log->error("{} {} already in scene nodes", node->type_name(), node->get_name());
    }
    else
#endif
    {
        ERHE_VERIFY(node->node_data.host == nullptr);
        node->node_data.host = m_host;
        m_flat_node_vector.push_back(node);
        m_nodes_sorted = false;
    }

    ERHE_VERIFY(!node->parent().expired());

    if ((node->get_flag_bits() & Item_flags::no_message) == 0)
    {
        if (erhe::scene::g_scene_message_bus != nullptr)
        {
            g_scene_message_bus->send_message(
                Scene_message{
                    .event_type = Scene_event_type::node_added_to_scene,
                    .scene      = this,
                    .lhs        = node
                }
            );
        }
    }
}

void Scene::unregister_node(
    const std::shared_ptr<erhe::scene::Node>& node
)
{
    log->trace(
        "unregister {} depth {} child count = {}",
        node->get_name(),
        node->get_depth(),
        node->node_data.children.size()
    );

    const auto i = std::remove(m_flat_node_vector.begin(), m_flat_node_vector.end(), node);
    if (i == m_flat_node_vector.end())
    {
        log->error("Node {} not in scene nodes", node->get_name());
    }
    else
    {
        node->node_data.host = nullptr;
        m_flat_node_vector.erase(i, m_flat_node_vector.end());
    }

    sanity_check();

    if ((node->get_flag_bits() & Item_flags::no_message) == 0)
    {
        if (erhe::scene::g_scene_message_bus != nullptr)
        {
            g_scene_message_bus->send_message(
                Scene_message{
                    .event_type = Scene_event_type::node_removed_from_scene,
                    .scene      = this,
                    .lhs        = node
                }
            );
        }
    }
}

void Scene::register_camera(const std::shared_ptr<Camera>& camera)
{
#ifndef NDEBUG
    const auto i = std::find(m_cameras.begin(), m_cameras.end(), camera);
    if (i != m_cameras.end())
    {
        log->error("camera {} already in scene cameras", camera->get_name());
    }
    else
#endif
    {
        m_cameras.push_back(camera);
    }
}

void Scene::unregister_camera(const std::shared_ptr<Camera>& camera)
{
    ERHE_VERIFY(camera);
    const auto i = std::remove(m_cameras.begin(), m_cameras.end(), camera);
    if (i == m_cameras.end())
    {
        log->error("camera {} not in scene cameras", camera->get_name());
    }
    else
    {
        m_cameras.erase(i, m_cameras.end());
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
    else
    {
        log->error("mesh {} layer not found", mesh->get_name());
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
    else
    {
        log->error("mesh {} layer not found", mesh->get_name());
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

auto is_scene(const Item* const item) -> bool
{
    if (item == nullptr)
    {
        return false;
    }
    using namespace erhe::toolkit;
    return test_all_rhs_bits_set(item->get_type(), Item_type::scene);
}

auto is_scene(const std::shared_ptr<Item>& item) -> bool
{
    return is_scene(item.get());
}

auto as_scene(Item* const item) -> Scene*
{
    if (item == nullptr)
    {
        return nullptr;
    }
    using namespace erhe::toolkit;
    if (!test_all_rhs_bits_set(item->get_type(), Item_type::scene))
    {
        return nullptr;
    }
    return reinterpret_cast<Scene*>(item);
}

auto as_scene(const std::shared_ptr<Item>& item) -> std::shared_ptr<Scene>
{
    if (!item)
    {
        return {};
    }
    using namespace erhe::toolkit;
    if (!test_all_rhs_bits_set(item->get_type(), Item_type::scene))
    {
        return {};
    }
    return std::static_pointer_cast<Scene>(item);
}

} // namespace erhe::scene

