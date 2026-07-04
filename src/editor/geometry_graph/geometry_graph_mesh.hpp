#pragma once

#include "erhe_scene/node_attachment.hpp"

#include <memory>

namespace erhe::scene { class Mesh; }

namespace editor {

class Graph_mesh;
class Node_physics;

// Node attachment sourcing the node's mesh from a Graph_mesh asset.
//
// The scene-side back-link of the geometry graph feature (the analogue of
// Material::texture_source -> Graph_texture): a scene Node carrying this
// attachment gets its renderable Mesh from the referenced Graph_mesh's
// baked products, and the attachment points back at the graph asset that
// produced the geometry. The attachment CONTROLS a sibling Mesh attachment
// (a pre-existing Mesh on the node is ADOPTED - a node has exactly one
// attachment of each type - else one is created on first apply; primitives
// swapped on every re-bake, detached when this attachment leaves the node)
// and keeps an optional Node_physics in sync with the graph's baked
// collision shape (likewise adopting a pre-existing one).
//
// Updates are push-based: Geometry_graph_window's evaluation engine calls
// apply_baked_products() on every attachment bound to a graph after that
// graph's background evaluation finishes; interactive bind paths
// (Properties, MCP) call it once at bind time so a late binder picks up
// the latest bake immediately. Scene load attaches without applying -
// loaded graphs are born dirty, so the first evaluation pushes.
// apply_baked_products() is main-thread only.
//
// Intentionally not_clonable for the MVP (like Frame_controller /
// Brush_placement): cloning a node skips this attachment; re-bind the
// clone explicitly when needed.
class Geometry_graph_mesh
    : public erhe::Item<
        erhe::Item_base,
        erhe::scene::Node_attachment,
        Geometry_graph_mesh,
        erhe::Item_kind::not_clonable
    >
{
public:
    Geometry_graph_mesh();
    explicit Geometry_graph_mesh(const std::shared_ptr<Graph_mesh>& graph_mesh);
    ~Geometry_graph_mesh() noexcept override;

    // Implements Item_base
    static constexpr std::string_view static_type_name{"Geometry_graph_mesh"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t
    {
        return erhe::Item_type::node_attachment | erhe::Item_type::geometry_graph_mesh;
    }

    // Overrides Node_attachment: moving to a different node releases the
    // controlled mesh / physics from the node that held them. (Only fires
    // for attach / move; plain detach is handled through
    // handle_item_host_update and the m_controlled_node bookkeeping.)
    void handle_node_update(erhe::scene::Node* old_node, erhe::scene::Node* new_node) override;

    // Overrides Node_attachment. Detach from an in-scene node (get_node()
    // is already null when this fires) releases the controlled products
    // from the node that held them; a node (re)entering a scene after
    // missing a push requests one from the evaluation engine (applying
    // inline here could deadlock - the caller may hold item_host_mutex).
    void handle_item_host_update(erhe::Item_host* old_item_host, erhe::Item_host* new_item_host) override;

    [[nodiscard]] auto get_graph_mesh() const -> const std::shared_ptr<Graph_mesh>&;
    // Rebinding resets the applied revision; call apply_baked_products()
    // afterwards (main thread) to materialize the new asset's bake.
    void set_graph_mesh(const std::shared_ptr<Graph_mesh>& graph_mesh);

    // Applies the referenced asset's current baked products to the node:
    // creates / updates the controlled sibling Mesh and Node_physics under
    // the scene item_host_mutex. No-op when the node is detached from a
    // scene, the asset is unset / never baked, or the latest bake was
    // already applied (revision check). Main thread only.
    void apply_baked_products();

    // The products this attachment currently controls on its node. Scene
    // serialization uses these to EXCLUDE them from the ordinary mesh /
    // node-physics passes - they are baked artifacts the graph rebuilds
    // on load, not authored content (persisting them would duplicate the
    // mesh and rigid body on every save/load round-trip).
    [[nodiscard]] auto get_controlled_mesh() const -> const std::shared_ptr<erhe::scene::Mesh>&;
    [[nodiscard]] auto get_controlled_node_physics() const -> const std::shared_ptr<Node_physics>&;

private:
    // Detaches the controlled Mesh / Node_physics from the node that
    // holds them (m_controlled_node; may differ from get_node() after a
    // detach or move) and drops them. Safe during node teardown (~Node
    // drains attachments with a while-not-empty loop, not an iterator),
    // and a no-op when that node is already gone (weak_ptr expired - the
    // dying node drained the controlled attachments itself).
    void release_controlled_products();

    std::shared_ptr<Graph_mesh>        m_graph_mesh;
    std::shared_ptr<erhe::scene::Mesh> m_mesh;
    std::shared_ptr<Node_physics>      m_node_physics;
    // The node the controlled products are currently attached to; kept
    // beside get_node() because release must reach the OLD node after
    // this attachment was detached or moved.
    std::weak_ptr<erhe::scene::Node>   m_controlled_node;
    uint64_t                           m_applied_revision{0};
};

}
