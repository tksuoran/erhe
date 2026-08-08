#include "erhe_scene/scene.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene_host.hpp"
#include "erhe_scene/scene_log.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>

namespace erhe::scene {

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

auto Scene::get_transform_update_nodes() const -> const std::vector<std::shared_ptr<Node>>&
{
    return m_transform_update_nodes;
}

auto Scene::get_no_transform_update_nodes() const -> const std::vector<std::shared_ptr<Node>>&
{
    return m_no_transform_update_nodes;
}

auto Scene::get_node_count() const -> std::size_t
{
    return m_transform_update_nodes.size() + m_no_transform_update_nodes.size();
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

void Scene::sever_host()
{
    // Detach all scene content from the host's resources -- host registration,
    // raytrace scene, physics world -- THROUGH THE NORMAL ORPHAN PATH, while
    // those resources are still alive. recursive_remove() reparents every child
    // to null, which fires Node/Mesh/Node_physics::handle_item_host_update(host,
    // nullptr): meshes detach from the raytrace scene and rigid bodies leave the
    // physics world. Ordering: remove the children first (they read the still-set
    // root-node host as their old host), then clear the root node and scene host.
    if (m_root_node) {
        m_root_node->remove_all_children_recursively();
        m_root_node->node_data.host = nullptr;
    }
    m_host = nullptr;
}

void Scene::mark_node_transform_dirty(const Node& node)
{
    if (m_updating_node_transforms) {
        // The propagation pass itself calls Node::update_transform(), whose
        // handle_transform_update() lands back here; the subtree walk already
        // covers those nodes.
        return;
    }
    if (node.node_data.transforms.scene_transform_dirty) {
        // A non-owner write upgrades an owner-dirty node: carry semantics win
        // (see scene_transform_dirty_by_owner).
        if (!m_transform_owner_writes) {
            node.node_data.transforms.scene_transform_dirty_by_owner = false;
        }
        return;
    }
    node.node_data.transforms.scene_transform_dirty          = true;
    node.node_data.transforms.scene_transform_dirty_by_owner = m_transform_owner_writes;
    m_transform_dirty_nodes.push_back(const_cast<Node*>(&node));
}

void Scene::update_node_transforms()
{
    ERHE_PROFILE_FUNCTION();

    // Worker threads (the editor's async raytrace kickoff and geometry
    // operations) mutate hosted item state under the Item_host mutex, and
    // the transform-update attachment callbacks read that state (e.g.
    // Mesh::handle_node_transform_update() iterates the raytrace primitive
    // vector that Mesh::update_rt_primitives() clears and rebuilds). Hold
    // the same mutex so the update cannot interleave with a worker.
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{
        (m_host != nullptr) ? m_host->item_host_mutex : Item_host::orphan_item_host_mutex
    };

    if (m_transform_dirty_nodes.empty()) {
        return; // nothing moved since the last pass
    }

    m_updating_node_transforms = true;
    std::swap(m_transform_dirty_nodes, m_transform_dirty_processing);

    // Ancestors first: when a dirty node lies inside another dirty node's
    // subtree, the ancestor's walk updates it (and records it in the visited
    // set), so its own entry is skipped instead of re-walking the subtree.
    std::sort(
        m_transform_dirty_processing.begin(),
        m_transform_dirty_processing.end(),
        [](const Node* lhs, const Node* rhs) {
            return lhs->get_depth() < rhs->get_depth();
        }
    );

    m_transform_update_visited.clear();
    for (Node* node : m_transform_dirty_processing) {
        node->node_data.transforms.scene_transform_dirty = false;
        const bool carry_body_driven = !node->node_data.transforms.scene_transform_dirty_by_owner;
        node->node_data.transforms.scene_transform_dirty_by_owner = false;
        if (!m_transform_update_visited.insert(node).second) {
            continue;
        }
        update_subtree_transforms(*node, carry_body_driven);
    }
    m_transform_dirty_processing.clear();
    m_updating_node_transforms = false;
}

void Scene::update_subtree_transforms(Node& node, const bool carry_body_driven)
{
    // The dirty node itself is already up to date: every write path updates
    // the node's own world transform and notifies its attachments eagerly
    // (transform setters, Node::handle_parent_update). Only descendants need
    // recomputation.
    //
    // no_transform_update children own their world transform (physics-
    // driven). When the dirt came from the transform owner itself (the
    // physics writeback; scene_transform_dirty_by_owner), their cached world
    // did not change with the parent, so their branches are skipped - the
    // owner writes every body-driven node itself, in ITS order, and a
    // recompute here could stomp a sibling-order-dependent fresh pose in a
    // chain of body-driven nodes. Any OTHER writer's dirt (tools, MCP, undo,
    // animation) CARRIES them: their parent_from_node is kept current by
    // set_world_from_node, so the recompute moves the body-driven subtree
    // with its edited ancestor; the physics writeback then teleports the
    // rigid bodies to the carried node poses (Node_physics::
    // before_physics_simulation runs node -> body for every body each frame
    // while the simulation runs, and on resume when it is paused).
    for (const auto& child : node.get_children()) {
        const auto child_node = std::dynamic_pointer_cast<Node>(child);
        if (!child_node) {
            continue;
        }
        if (!carry_body_driven && child_node->is_no_transform_update()) {
            continue;
        }
        if (!m_transform_update_visited.insert(child_node.get()).second) {
            continue;
        }
        child_node->update_transform(0);
        update_subtree_transforms(*child_node, carry_body_driven);
    }
}

Scene::Scene(const Scene&)
{
    ERHE_FATAL("This probably won't work");
}

Scene& Scene::operator=(const Scene&)
{
    ERHE_FATAL("This probably won't work");
}

Scene::Scene(const std::string_view name, Scene_host* const host)
    : Item  {name}
    , m_host{host}
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

    m_transform_dirty_nodes.clear();
    m_transform_update_nodes.clear();
    m_no_transform_update_nodes.clear();
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
    const bool already_registered =
        (std::find(m_transform_update_nodes   .begin(), m_transform_update_nodes   .end(), node) != m_transform_update_nodes   .end()) ||
        (std::find(m_no_transform_update_nodes.begin(), m_no_transform_update_nodes.end(), node) != m_no_transform_update_nodes.end());
    if (already_registered) {
        log->error("{} {} already in scene nodes", node->get_type_name(), node->get_name());
    } else
#endif
    {
        ERHE_VERIFY(node->node_data.host == nullptr);
        node->node_data.host = m_host;
        if (node->is_no_transform_update()) {
            m_no_transform_update_nodes.push_back(node);
        } else {
            m_transform_update_nodes.push_back(node);
        }
        // The bit may arrive set without list membership (clone() copies
        // node_data wholesale); reset it so the node can be enqueued here.
        node->node_data.transforms.scene_transform_dirty = false;
        mark_node_transform_dirty(*node);
    }

    ERHE_VERIFY(!node->get_parent().expired());
}

void Scene::unregister_node(const std::shared_ptr<erhe::scene::Node>& node)
{
    log->trace(
        "unregister {} depth {} child count = {}",
        node->get_name(),
        node->get_depth(),
        node->get_child_count()
    );

    if (node->node_data.transforms.scene_transform_dirty) {
        node->node_data.transforms.scene_transform_dirty = false;
        const auto dirty_i = std::remove(m_transform_dirty_nodes.begin(), m_transform_dirty_nodes.end(), node.get());
        m_transform_dirty_nodes.erase(dirty_i, m_transform_dirty_nodes.end());
    }

    // Remove from the bucket matching the node's current flag value; fall back
    // to the other bucket in case the flag was toggled while the node was not
    // hosted (the flag-change hook only notifies the hosting scene).
    auto* primary_bucket   = node->is_no_transform_update() ? &m_no_transform_update_nodes : &m_transform_update_nodes;
    auto* secondary_bucket = node->is_no_transform_update() ? &m_transform_update_nodes    : &m_no_transform_update_nodes;
    auto i = std::remove(primary_bucket->begin(), primary_bucket->end(), node);
    if (i != primary_bucket->end()) {
        node->node_data.host = nullptr;
        primary_bucket->erase(i, primary_bucket->end());
    } else {
        i = std::remove(secondary_bucket->begin(), secondary_bucket->end(), node);
        if (i != secondary_bucket->end()) {
            node->node_data.host = nullptr;
            secondary_bucket->erase(i, secondary_bucket->end());
        } else {
            log->error("Node {} not in scene nodes", node->get_name());
        }
    }

#if !defined(NDEBUG)
    sanity_check();
#endif
}

void Scene::handle_node_no_transform_update_changed(Node& node)
{
    // The node moves out of the bucket that no longer matches its flag value.
    auto* source_bucket = node.is_no_transform_update() ? &m_transform_update_nodes    : &m_no_transform_update_nodes;
    auto* target_bucket = node.is_no_transform_update() ? &m_no_transform_update_nodes : &m_transform_update_nodes;
    const auto i = std::find_if(
        source_bucket->begin(),
        source_bucket->end(),
        [&node](const std::shared_ptr<Node>& entry) {
            return entry.get() == &node;
        }
    );
    if (i == source_bucket->end()) {
        log->error("Node {} not in expected scene node bucket", node.get_name());
        return;
    }
    const std::shared_ptr<Node> moved_node = std::move(*i);
    source_bucket->erase(i);
    target_bucket->push_back(moved_node);
    if (target_bucket == &m_transform_update_nodes) {
        // Re-entering the updated set (e.g. physics body deactivated): the
        // parent may have moved while this branch was skipped, so recompute
        // the node's world transform now. update_transform() also notifies
        // attachments and queues the subtree via handle_transform_update().
        moved_node->update_transform(0);
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

