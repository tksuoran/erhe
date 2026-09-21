#include "geometry_graph/geometry_graph_mesh_system.hpp"

#include "content_library/content_library.hpp"
#include "geometry_graph/geometry_graph_mesh.hpp"
#include "geometry_graph/graph_mesh.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"

#include "erhe_physics/icollision_shape.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

#include <mutex>

namespace editor {

Geometry_graph_mesh_system::~Geometry_graph_mesh_system() noexcept
{
    // Scene_root::sever_host() unregisters every node before this runs, so
    // there is normally nothing left; a record that survived is released
    // through the node it names, which is still alive while the scene is.
    for (std::pair<erhe::scene::Node* const, Geometry_graph_mesh_entry>& entry : m_entries) {
        if (entry.second.node_physics && (entry.first != nullptr)) {
            entry.first->detach(entry.second.node_physics.get());
        }
        release(entry.second);
    }
}

void Geometry_graph_mesh_system::on_node_registered(erhe::scene::Node& node)
{
    const std::optional<Geometry_graph_mesh_data> data = read_geometry_graph_mesh(node);
    if (!data.has_value()) {
        return;
    }
    m_entries.emplace(&node, Geometry_graph_mesh_entry{});
    // Applying inline would deadlock - the caller may hold item_host_mutex -
    // so a node entering a scene with a bake waiting asks the evaluation
    // engine for a push instead. A never-baked graph needs no request: it is
    // born dirty and its first evaluation pushes.
    if (data.value().graph_mesh && (data.value().graph_mesh->get_baked_revision() != 0)) {
        data.value().graph_mesh->request_node_push();
    }
}

void Geometry_graph_mesh_system::on_node_unregistered(erhe::scene::Node& node)
{
    const std::unordered_map<erhe::scene::Node*, Geometry_graph_mesh_entry>::iterator i = m_entries.find(&node);
    if (i == m_entries.end()) {
        return;
    }
    if (i->second.node_physics) {
        node.detach(i->second.node_physics.get());
    }
    release(i->second);
    m_entries.erase(i);
}

void Geometry_graph_mesh_system::on_values_changed(
    erhe::scene::Node&                         node,
    const erhe::property::Dependency_property& property
)
{
    if (&property != Geometry_graph_mesh::graph_mesh_property.get_ptr()) {
        return;
    }
    // Any write of the binding releases what the previous graph controlled at
    // once: a rebind target may never publish a bake, and a stale mesh must
    // not linger.
    const std::unordered_map<erhe::scene::Node*, Geometry_graph_mesh_entry>::iterator i = m_entries.find(&node);
    if (i != m_entries.end()) {
        if (i->second.node_physics) {
            node.detach(i->second.node_physics.get());
        }
        release(i->second);
        if (!carries_geometry_graph_mesh(node)) {
            m_entries.erase(i);
            return;
        }
        apply(node, i->second);
        return;
    }
    if (!carries_geometry_graph_mesh(node)) {
        return;
    }
    const std::pair<std::unordered_map<erhe::scene::Node*, Geometry_graph_mesh_entry>::iterator, bool> inserted =
        m_entries.emplace(&node, Geometry_graph_mesh_entry{});
    apply(node, inserted.first->second);
}

void Geometry_graph_mesh_system::on_node_active_changed(erhe::scene::Node& node)
{
    // The controlled products are ordinary prims below the node, so the
    // derived active bit reaches them the way it reaches any other child.
    static_cast<void>(node);
}

auto Geometry_graph_mesh_system::find_entry(erhe::scene::Node& node) const -> const Geometry_graph_mesh_entry*
{
    const std::unordered_map<erhe::scene::Node*, Geometry_graph_mesh_entry>::const_iterator i = m_entries.find(&node);
    return (i == m_entries.end()) ? nullptr : &i->second;
}

void Geometry_graph_mesh_system::apply_for_graph(const std::shared_ptr<Graph_mesh>& graph_mesh)
{
    if (!graph_mesh) {
        return;
    }
    for (std::pair<erhe::scene::Node* const, Geometry_graph_mesh_entry>& entry : m_entries) {
        if (entry.first == nullptr) {
            continue;
        }
        const std::optional<Geometry_graph_mesh_data> data = read_geometry_graph_mesh(*entry.first);
        if (data.has_value() && (data.value().graph_mesh == graph_mesh)) {
            apply(*entry.first, entry.second);
        }
    }
}

void Geometry_graph_mesh_system::release(Geometry_graph_mesh_entry& entry)
{
    // A Mesh is a child prim (doc/erhe/usd_compatibility_design.md C5), so it
    // is released from whatever parent holds it - a mesh left behind here
    // comes back as a second, name-suffixed sibling on the next bake.
    if (entry.mesh) {
        erhe::scene::set_mesh_parent(entry.mesh, {});
    }
    if (entry.ghost_mesh) {
        erhe::scene::set_mesh_parent(entry.ghost_mesh, {});
    }
    entry.node_physics.reset();
    entry.mesh.reset();
    entry.ghost_mesh.reset();
    entry.applied_revision = 0;
}

void Geometry_graph_mesh_system::apply(erhe::scene::Node& node, Geometry_graph_mesh_entry& entry)
{
    const std::optional<Geometry_graph_mesh_data> data = read_geometry_graph_mesh(node);
    if (!data.has_value() || !data.value().graph_mesh) {
        return;
    }
    const std::shared_ptr<Graph_mesh>& graph_mesh = data.value().graph_mesh;
    const std::uint64_t                revision   = graph_mesh->get_baked_revision();
    if ((revision == 0) || (revision == entry.applied_revision)) {
        return; // never baked, or latest bake already applied
    }
    erhe::Item_host* item_host = node.get_item_host();
    if (item_host == nullptr) {
        return; // not in a scene; applied on the next push once it is
    }
    Scene_root* scene_root = static_cast<Scene_root*>(item_host);
    const Graph_mesh_baked_products& products = graph_mesh->get_baked_products();

    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{item_host->item_host_mutex};

    // The node may already carry a Mesh child or a Node_physics attachment
    // (e.g. the graph was dropped onto an existing mesh node): adopt them as
    // the controlled products - the bake replaces the mesh's primitives and
    // dictates the physics state from here on - instead of adding duplicates.
    if (!entry.mesh) {
        entry.mesh = erhe::scene::get_mesh(&node);
    }
    if (!entry.node_physics) {
        entry.node_physics = erhe::scene::get_attachment<Node_physics>(&node);
    }

    // The graph's output node may not have selected a material (it needs no
    // scene of its own); fall back to the first material of THIS node's scene,
    // the same default the legacy scratch path uses.
    std::shared_ptr<erhe::primitive::Material> material = products.material;
    if (!material) {
        const std::shared_ptr<Content_library> library = scene_root->get_content_library();
        if (library) {
            const std::vector<std::shared_ptr<erhe::primitive::Material>>& materials = library->get_all<erhe::primitive::Material>();
            if (!materials.empty()) {
                material = materials.front();
            }
        }
    }

    // Ghost-node companion mesh (edge lines only). Applied in both the empty
    // and the regular path: the display node baking empty does not clear a
    // designated ghost, and vice versa.
    if (products.ghost_primitive) {
        if (!entry.ghost_mesh) {
            entry.ghost_mesh = std::make_shared<erhe::scene::Mesh>(node.get_name() + " Ghost Mesh");
            entry.ghost_mesh->layer_id = scene_root->layers().content()->id;
            // visible + render_wireframe only: no `content` (skipped by all
            // fill / point passes), no shadow_cast, no id (not pickable); the
            // primitive has no raytrace shape, so hover misses it too.
            entry.ghost_mesh->enable_flag_bits(erhe::Item_flags::render_wireframe);
            erhe::scene::set_mesh_parent(entry.ghost_mesh, node.shared_node_from_this());
        }
        entry.ghost_mesh->clear_primitives();
        entry.ghost_mesh->add_primitive(products.ghost_primitive, material);
    } else if (entry.ghost_mesh) {
        erhe::scene::set_mesh_parent(entry.ghost_mesh, {});
        entry.ghost_mesh.reset();
    }

    if (!products.primitive) {
        // The graph evaluated to empty / disconnected: keep the node but show
        // nothing (mirrors the output node's empty handling).
        if (entry.mesh) {
            scene_root->begin_mesh_rt_update(entry.mesh);
            entry.mesh->clear_primitives();
            scene_root->end_mesh_rt_update(entry.mesh);
        }
        if (entry.node_physics) {
            node.detach(entry.node_physics.get());
            entry.node_physics.reset();
        }
        entry.applied_revision = revision;
        return;
    }

    if (!entry.mesh) {
        entry.mesh = std::make_shared<erhe::scene::Mesh>(node.get_name() + " Mesh");
        entry.mesh->layer_id = scene_root->layers().content()->id;
        entry.mesh->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::id);
        entry.mesh->set_value(erhe::scene::Mesh::shadow_cast_property, true);
        erhe::scene::set_mesh_parent(entry.mesh, node.shared_node_from_this());
    }
    // The mesh is registered in the scene at this point, so its raytrace
    // instances are already attached to the scene's raytrace world. Swapping
    // the primitives rebuilds the instances; without the rt-update bracket the
    // new instances would never be attached (or masked) and hover / ray
    // picking would pass straight through the mesh.
    scene_root->begin_mesh_rt_update(entry.mesh);
    entry.mesh->clear_primitives();
    entry.mesh->add_primitive(products.primitive, material);
    scene_root->end_mesh_rt_update(entry.mesh);

    if (products.physics_enabled && products.collision_shape) {
        if (!entry.node_physics) {
            const erhe::physics::IRigid_body_create_info create_info{
                .collision_shape = products.collision_shape,
                .debug_label     = node.get_name(),
                .motion_mode     = products.physics_motion_mode
            };
            entry.node_physics = std::make_shared<Node_physics>(create_info);
            node.attach(entry.node_physics);
        } else {
            entry.node_physics->set_collision_shape(products.collision_shape);
            entry.node_physics->set_motion_mode(products.physics_motion_mode);
        }
    } else if (entry.node_physics) {
        node.detach(entry.node_physics.get());
        entry.node_physics.reset();
    }

    entry.applied_revision = revision;
}

auto find_geometry_graph_mesh_system(erhe::scene::Node& node) -> Geometry_graph_mesh_system*
{
    erhe::Item_host* const item_host = node.get_item_host();
    if (item_host == nullptr) {
        return nullptr;
    }
    return &static_cast<Scene_root*>(item_host)->get_geometry_graph_mesh_system();
}

} // namespace editor
