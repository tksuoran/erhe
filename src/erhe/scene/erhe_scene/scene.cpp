#include "erhe_scene/scene.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene_host.hpp"
#include "erhe_scene/scene_log.hpp"
#include "erhe_scene/scene_message_bus.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>

namespace erhe::scene {

auto Scene::get_static_type()       -> uint64_t         { return erhe::Item_type::scene; }
auto Scene::get_type       () const -> uint64_t         { return get_static_type(); }
auto Scene::get_type_name  () const -> std::string_view { return static_type_name; }

#pragma region Layers
Mesh_layer::Mesh_layer(const std::string_view name, const uint64_t flags, const Layer_id id)
    : name {name}
    , flags{flags}
    , id   {id}
{
}

auto Mesh_layer::get_mesh_by_id(const erhe::Unique_id<Node>::id_type mesh_id) const -> std::shared_ptr<Mesh>
{
    for (const auto& mesh : meshes) {
        if (mesh->get_id() == mesh_id) {
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
    if (i != meshes.end()) {
        log->error("mesh {} already in layer meshes", mesh->get_name());
    } else
#endif
    {
        meshes.push_back(mesh);
    }
}

void Mesh_layer::remove(const std::shared_ptr<Mesh>& mesh)
{
    ERHE_VERIFY(mesh);

    const auto i = std::remove(meshes.begin(), meshes.end(), mesh);
    if (i == meshes.end()) {
        log->error("mesh {} not in layer meshes", mesh->get_name());
    } else {
        meshes.erase(i, meshes.end());
    }
}

Light_layer::Light_layer(const std::string_view name, const Layer_id id)
    : name{name}
    , id  {id}
{
}

auto Light_layer::get_light_by_id(const erhe::Unique_id<Node>::id_type light_id) const -> std::shared_ptr<Light>
{
    for (const auto& light : lights) {
        if (light->get_id() == light_id) {
            return light;
        }
    }
    return {};
}

auto Light_layer::get_name() const -> const std::string&
{
    return name;
}

void Light_layer::add(const std::shared_ptr<Light>& light)
{
    ERHE_VERIFY(light);

    log->trace("add_to_light_layer(light = {})", light->get_name());

    {
#ifndef NDEBUG
        const auto i = std::find(lights.begin(), lights.end(), light);
        if (i != lights.end()) {
            log->error("light {} already in layer lights", light->get_name());
        } else
#endif
        {
            lights.push_back(light);
        }
    }
}

void Light_layer::remove(const std::shared_ptr<Light>& light)
{
    ERHE_VERIFY(light);

    log->trace("remove_from_scene_layer(light = {})`", light->get_name());

    const auto i = std::remove(lights.begin(), lights.end(), light);

    if (i == lights.end()) {
        log->error("light {} not in layer lights", light->get_name());
    } else {
        lights.erase(i, lights.end());
    }
}

#pragma endregion Layers

auto Scene::get_camera_by_id(const erhe::Unique_id<Node>::id_type id) const -> std::shared_ptr<Camera>
{
    for (const auto& camera : m_cameras) {
        if (camera->get_id() == id) {
            return camera;
        }
    }
    return {};
}

auto Scene::get_mesh_by_id(const erhe::Unique_id<Node>::id_type id) const -> std::shared_ptr<Mesh>
{
    for (const auto& layer : m_mesh_layers) {
        const auto& mesh = layer->get_mesh_by_id(id);
        if (mesh) {
            return mesh;
        }
    }
    return {};
}

auto Scene::get_light_by_id(const erhe::Unique_id<Node>::id_type id) const -> std::shared_ptr<Light>
{
    for (const auto& layer : m_light_layers) {
        const auto& light = layer->get_light_by_id(id);
        if (light) {
            return light;
        }
    }
    return {};
}

auto Scene::get_mesh_layer_by_id(const Layer_id id) const -> std::shared_ptr<Mesh_layer>
{
    for (const auto& layer : m_mesh_layers) {
        if (layer->id == id) {
            return layer;
        }
    }
    return {};
}

auto Scene::get_light_layer_by_id(const Layer_id id) const -> std::shared_ptr<Light_layer>
{
    for (const auto& layer : m_light_layers) {
        if (layer->id == id) {
            return layer;
        }
    }
    return {};
}

auto Scene::get_root_node() const -> std::shared_ptr<erhe::scene::Node>
{
    return m_root_node;
}

auto Scene::get_cameras() -> std::vector<std::shared_ptr<Camera>>&
{
    return m_cameras;
}

auto Scene::get_cameras() const -> const std::vector<std::shared_ptr<Camera>>&
{
    return m_cameras;
}

auto Scene::get_skins() -> std::vector<std::shared_ptr<Skin>>&
{
    return m_skins;
}

auto Scene::get_skins() const -> const std::vector<std::shared_ptr<Skin>>&
{
    return m_skins;
}

auto Scene::get_flat_nodes() -> std::vector<std::shared_ptr<Node>>&
{
    return m_flat_node_vector;
}

auto Scene::get_flat_nodes() const -> const std::vector<std::shared_ptr<Node>>&
{
    return m_flat_node_vector;
}

auto Scene::get_mesh_layers() -> std::vector<std::shared_ptr<Mesh_layer>>&
{
    return m_mesh_layers;
}

auto Scene::get_mesh_layers() const -> const std::vector<std::shared_ptr<Mesh_layer>>&
{
    return m_mesh_layers;
}

auto Scene::get_light_layers() -> std::vector<std::shared_ptr<Light_layer>>&
{
    return m_light_layers;
}

auto Scene::get_light_layers() const -> const std::vector<std::shared_ptr<Light_layer>>&
{
    return m_light_layers;
}

void Scene::sanity_check() const
{
#if !defined(NDEBUG)
    m_root_node->node_sanity_check();
#endif
}

void Scene::sort_transform_nodes()
{
    log->trace("sorting {} nodes", m_flat_node_vector.size());

    std::sort(
        m_flat_node_vector.begin(),
        m_flat_node_vector.end(),
        [](const auto& lhs, const auto& rhs) {
            return lhs->get_depth() < rhs->get_depth();
        }
    );
    m_nodes_sorted = true;
}

void Scene::update_node_transforms()
{
    ERHE_PROFILE_FUNCTION();

    if (!m_nodes_sorted) {
        sort_transform_nodes();
    }

    for (auto& node : m_flat_node_vector) {
        if (node->is_no_transform_update()) {
            continue;
        }
        node->update_transform(0);
    }
}

Scene::Scene(const Scene& src)
    : m_message_bus{src.m_message_bus}
{
    ERHE_FATAL("This probably won't work");
}

Scene& Scene::operator=(const Scene&)
{
    ERHE_FATAL("This probably won't work");
}

Scene::Scene(Scene_message_bus& message_bus, const std::string_view name, Scene_host* const host)
    : Item         {name}
    , m_message_bus{message_bus}
    , m_host       {host}
    , m_root_node  {std::make_shared<Node>("root")}
{
    enable_flag_bits(
        erhe::Item_flags::content             |
        erhe::Item_flags::no_transform_update |
        erhe::Item_flags::expand
    );

    // The implicit root node has a valid (identity) transform
    m_root_node->node_data.host = host;
    m_root_node->node_data.transforms.parent_from_node_serial = 1;
    m_root_node->node_data.transforms.world_from_node_serial  = 1;
}

Scene::~Scene() noexcept
{
    m_root_node->trace();
    sanity_check();

    m_root_node->recursive_remove();

    m_flat_node_vector.clear();
    m_mesh_layers.clear();
    m_light_layers.clear();
    m_cameras.clear();
    m_root_node.reset();
}

auto Scene::get_item_host() const -> erhe::Item_host*
{
    return m_host;
}

void Scene::add_mesh_layer(const std::shared_ptr<Mesh_layer>& mesh_layer)
{
    m_mesh_layers.push_back(mesh_layer);
}

void Scene::add_light_layer(const std::shared_ptr<Light_layer>& light_layer)
{
    m_light_layers.push_back(light_layer);
}

void Scene::register_node(const std::shared_ptr<erhe::scene::Node>& node)
{
    ERHE_PROFILE_FUNCTION();

#ifndef NDEBUG
    const auto i = std::find(m_flat_node_vector.begin(), m_flat_node_vector.end(), node);
    if (i != m_flat_node_vector.end()) {
        log->error("{} {} already in scene nodes", node->get_type_name(), node->get_name());
    } else
#endif
    {
        ERHE_VERIFY(node->node_data.host == nullptr);
        node->node_data.host = m_host;
        m_flat_node_vector.push_back(node);
        m_nodes_sorted = false;
    }

    ERHE_VERIFY(!node->get_parent().expired());

    if ((node->get_flag_bits() & erhe::Item_flags::no_message) == 0) {
        m_message_bus.send_message(
            Scene_message{
                .event_type = Scene_event_type::node_added_to_scene,
                .scene      = this,
                .lhs        = node
            }
        );
    }
}

void Scene::unregister_node(const std::shared_ptr<erhe::scene::Node>& node)
{
    log->trace(
        "unregister {} depth {} child count = {}",
        node->get_name(),
        node->get_depth(),
        node->get_child_count()
    );

    const auto i = std::remove(m_flat_node_vector.begin(), m_flat_node_vector.end(), node);
    if (i == m_flat_node_vector.end()) {
        log->error("Node {} not in scene nodes", node->get_name());
    } else {
        node->node_data.host = nullptr;
        m_flat_node_vector.erase(i, m_flat_node_vector.end());
    }

#if !defined(NDEBUG)
    sanity_check();
#endif

    if ((node->get_flag_bits() & erhe::Item_flags::no_message) == 0) {
        m_message_bus.send_message(
            Scene_message{
                .event_type = Scene_event_type::node_removed_from_scene,
                .scene      = this,
                .lhs        = node
            }
        );
    }
}

void Scene::register_camera(const std::shared_ptr<Camera>& camera)
{
#ifndef NDEBUG
    const auto i = std::find(m_cameras.begin(), m_cameras.end(), camera);
    if (i != m_cameras.end()) {
        log->error("camera {} already in scene cameras", camera->get_name());
    } else
#endif
    {
        m_cameras.push_back(camera);
    }
}

void Scene::unregister_camera(const std::shared_ptr<Camera>& camera)
{
    ERHE_VERIFY(camera);
    const auto i = std::remove(m_cameras.begin(), m_cameras.end(), camera);
    if (i == m_cameras.end()) {
        log->error("camera {} not in scene cameras", camera->get_name());
    } else {
        m_cameras.erase(i, m_cameras.end());
    }
}

void Scene::register_mesh(const std::shared_ptr<Mesh>& mesh)
{
    ERHE_VERIFY(mesh);
    auto mesh_layer = get_mesh_layer_by_id(mesh->layer_id);
    if (mesh_layer) {
        mesh_layer->add(mesh);
    } else {
        log->error("mesh {} layer not found", mesh->get_name());
    }
}

void Scene::unregister_mesh(const std::shared_ptr<Mesh>& mesh)
{
    ERHE_VERIFY(mesh);
    auto mesh_layer = get_mesh_layer_by_id(mesh->layer_id);
    if (mesh_layer) {
        mesh_layer->remove(mesh);
    } else {
        log->error("mesh {} layer not found", mesh->get_name());
    }
}

void Scene::register_skin(const std::shared_ptr<Skin>& skin)
{
#ifndef NDEBUG
    const auto i = std::find(m_skins.begin(), m_skins.end(), skin);
    if (i != m_skins.end()) {
        log->error("skin {} already in scene cameras", skin->get_name());
    } else
#endif
    {
        m_skins.push_back(skin);
    }
}

void Scene::unregister_skin(const std::shared_ptr<Skin>& skin)
{
    ERHE_VERIFY(skin);
    const auto i = std::remove(m_skins.begin(), m_skins.end(), skin);
    if (i == m_skins.end()) {
        log->error("skin {} not in scene cameras", skin->get_name());
    } else {
        m_skins.erase(i, m_skins.end());
    }
}

void Scene::register_light(const std::shared_ptr<Light>& light)
{
    ERHE_VERIFY(light);
    auto light_layer = get_light_layer_by_id(light->layer_id);
    if (light_layer) {
        light_layer->add(light);
    } else {
        log->error("light {} layer not found", light->get_name());
    }
}

void Scene::unregister_light(const std::shared_ptr<Light>& light)
{
    ERHE_VERIFY(light);
    auto light_layer = get_light_layer_by_id(light->layer_id);
    if (light_layer) {
        light_layer->remove(light);
    } else {
        log->error("light {} layer not found", light->get_name());
    }
}

} // namespace erhe::scene

