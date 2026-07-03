#include "geometry_graph/geometry_graph_mesh.hpp"
#include "geometry_graph/graph_mesh.hpp"

#include "content_library/content_library.hpp"
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

Geometry_graph_mesh::Geometry_graph_mesh()
    : Item{"Geometry Graph Mesh"}
{
    enable_flag_bits(erhe::Item_flags::show_in_ui);
}

Geometry_graph_mesh::Geometry_graph_mesh(const std::shared_ptr<Graph_mesh>& graph_mesh)
    : Item        {"Geometry Graph Mesh"}
    , m_graph_mesh{graph_mesh}
{
    enable_flag_bits(erhe::Item_flags::show_in_ui);
}

Geometry_graph_mesh::~Geometry_graph_mesh() noexcept
{
    release_controlled_products();
    set_node(nullptr);
}

void Geometry_graph_mesh::handle_node_update(erhe::scene::Node* const old_node, erhe::scene::Node* const new_node)
{
    Node_attachment::handle_node_update(old_node, new_node);
    // Attach / move only (the base never fires this for a detach to
    // null): products following us to a different node are released from
    // the node that held them; apply_baked_products() then re-creates
    // them on the new node.
    if ((old_node != nullptr) && (old_node != new_node)) {
        release_controlled_products();
    }
}

void Geometry_graph_mesh::handle_item_host_update(erhe::Item_host* const old_item_host, erhe::Item_host* const new_item_host)
{
    static_cast<void>(old_item_host);
    if (get_node() == nullptr) {
        // This attachment was just detached from an in-scene node
        // (set_node(nullptr) nulled the node before firing this hook);
        // the controlled products must not stay behind on it. Callers
        // hold a shared_ptr to this attachment across detach (the
        // Node_attachment::set_node contract).
        release_controlled_products();
        return;
    }
    if ((new_item_host != nullptr) && m_graph_mesh && (m_graph_mesh->get_baked_revision() != m_applied_revision)) {
        m_graph_mesh->request_attachment_push();
    }
}

auto Geometry_graph_mesh::get_graph_mesh() const -> const std::shared_ptr<Graph_mesh>&
{
    return m_graph_mesh;
}

void Geometry_graph_mesh::set_graph_mesh(const std::shared_ptr<Graph_mesh>& graph_mesh)
{
    if (m_graph_mesh == graph_mesh) {
        return;
    }
    m_graph_mesh = graph_mesh;
    // Release immediately on ANY change so no stale mesh lingers: a
    // rebind target may never publish a bake (e.g. an asset without an
    // output node), in which case apply_baked_products() would keep the
    // previous asset's mesh forever. Both bind paths (Properties, MCP)
    // call apply_baked_products() right after, so a baked target shows
    // without a visible gap.
    release_controlled_products();
}

void Geometry_graph_mesh::release_controlled_products()
{
    const std::shared_ptr<erhe::scene::Node> node = m_controlled_node.lock();
    if (node) {
        if (m_node_physics) {
            node->detach(m_node_physics.get());
        }
        if (m_mesh) {
            node->detach(m_mesh.get());
        }
    }
    m_controlled_node.reset();
    m_node_physics.reset();
    m_mesh.reset();
    m_applied_revision = 0;
}

void Geometry_graph_mesh::apply_baked_products()
{
    erhe::scene::Node* node = get_node();
    if ((node == nullptr) || !m_graph_mesh) {
        return;
    }
    // If the products are still attached to a previous node (detach path
    // that fired no hook, e.g. detached from a node outside any scene),
    // reclaim them before materializing on the current node.
    if (m_mesh && (m_controlled_node.lock().get() != node)) {
        release_controlled_products();
    }
    const uint64_t revision = m_graph_mesh->get_baked_revision();
    if ((revision == 0) || (revision == m_applied_revision)) {
        return; // never baked, or latest bake already applied
    }
    erhe::Item_host* item_host = node->get_item_host();
    if (item_host == nullptr) {
        return; // not in a scene; applied on the next push once attached
    }
    Scene_root* scene_root = static_cast<Scene_root*>(item_host);
    const Graph_mesh_baked_products& products = m_graph_mesh->get_baked_products();

    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{item_host->item_host_mutex};

    if (!products.primitive) {
        // The graph evaluated to empty / disconnected: keep the node but
        // show nothing (mirrors the output node's empty handling).
        if (m_mesh) {
            m_mesh->clear_primitives();
        }
        if (m_node_physics) {
            node->detach(m_node_physics.get());
            m_node_physics.reset();
        }
        m_applied_revision = revision;
        return;
    }

    // The graph's output node may not have selected a material (it needs
    // no scene of its own); fall back to the first material of THIS
    // node's scene, the same default the legacy scratch path uses.
    std::shared_ptr<erhe::primitive::Material> material = products.material;
    if (!material) {
        const std::shared_ptr<Content_library> library = scene_root->get_content_library();
        if (library && library->materials) {
            const std::vector<std::shared_ptr<erhe::primitive::Material>>& materials = library->materials->get_all<erhe::primitive::Material>();
            if (!materials.empty()) {
                material = materials.front();
            }
        }
    }

    if (!m_mesh) {
        m_mesh = std::make_shared<erhe::scene::Mesh>(node->get_name() + " Mesh");
        m_mesh->layer_id = scene_root->layers().content()->id;
        m_mesh->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::shadow_cast);
        node->attach(m_mesh);
        m_controlled_node = node->shared_node_from_this();
    }
    m_mesh->clear_primitives();
    m_mesh->add_primitive(products.primitive, material);

    if (products.physics_enabled && products.collision_shape) {
        if (!m_node_physics) {
            const erhe::physics::IRigid_body_create_info create_info{
                .collision_shape = products.collision_shape,
                .debug_label     = node->get_name(),
                .motion_mode     = products.physics_motion_mode
            };
            m_node_physics = std::make_shared<Node_physics>(create_info);
            node->attach(m_node_physics);
        } else {
            m_node_physics->set_collision_shape(products.collision_shape);
            m_node_physics->set_motion_mode(products.physics_motion_mode);
        }
    } else if (m_node_physics) {
        node->detach(m_node_physics.get());
        m_node_physics.reset();
    }

    m_applied_revision = revision;
}

}
