#pragma once

#include "erhe_scene/node_system.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace erhe::scene { class Mesh; }

namespace editor {

class Graph_mesh;

// The runtime state one node's geometry-graph binding implies: the products
// the graph controls on the node, and the bake revision already applied.
//
// The mesh is ADOPTED when the node already holds one (a graph dropped onto an
// existing mesh node) and created otherwise; the ghost mesh is always created
// and owned here. The bake dictates the primitives of both from then on.
class Geometry_graph_mesh_entry
{
public:
    std::shared_ptr<erhe::scene::Mesh> mesh;
    // The graph's ghost node baked as an edge-lines-only companion mesh
    // (Houdini template flag): flags visible | render_wireframe, NOT content /
    // shadow_cast / id, so only the dedicated ghost edge-lines composition
    // pass draws it.
    std::shared_ptr<erhe::scene::Mesh> ghost_mesh;
    // True while the mesh is one the system created for the node, so
    // releasing the binding knows to take it back out of the scene. A mesh
    // adopted from the node - including the node itself, which is a Mesh prim
    // whenever it was made by a shape or a brush - is the user's, and stays
    // where it is with the geometry the last bake gave it.
    bool                               owns_mesh{false};
    // True while the bake gave the node a rigid body, so releasing the
    // binding knows to take the body's values back off the node.
    bool                               owns_rigid_body{false};
    std::uint64_t                      applied_revision{0};
};

// The per-scene owner of the `Geometry_graph_mesh` value group's runtime state
// (doc/erhe/scene.md "Node systems"). One of these is owned by
// each Scene_root and added to its scene, which drives it from the three
// change sites; it keeps one Geometry_graph_mesh_entry per node bound to a
// graph, keyed by a raw Node*, and releases the entry's products when the node
// leaves the scene, so a scene close keeps nothing of it alive.
//
// Updates are push-based: Geometry_graph_window's evaluation engine calls
// apply_for_graph() on every scene after a graph's background evaluation
// finishes, and a binding write applies the latest bake at once through
// on_values_changed, so a late binder needs no evaluation. Applying is main
// thread only.
class Geometry_graph_mesh_system : public erhe::scene::INode_system
{
public:
    ~Geometry_graph_mesh_system() noexcept override;

    // Implements INode_system
    void on_node_registered    (erhe::scene::Node& node) override;
    void on_node_unregistered  (erhe::scene::Node& node) override;
    void on_values_changed     (erhe::scene::Node& node, const erhe::property::Dependency_property& property) override;
    void on_node_active_changed(erhe::scene::Node& node) override;

    // The products the graph controls on `node`, or nullptr when this scene
    // holds no binding for it. The exporters ask, to EXCLUDE them from the
    // ordinary mesh / rigid-body passes: they are baked artifacts the graph
    // rebuilds on load, not authored content.
    [[nodiscard]] auto find_entry(erhe::scene::Node& node) const -> const Geometry_graph_mesh_entry*;

    // Applies `graph_mesh`'s current baked products to every node of this
    // scene bound to it. Main thread.
    void apply_for_graph(const std::shared_ptr<Graph_mesh>& graph_mesh);

private:
    // Materializes the node's graph's current bake on it: creates or updates
    // the controlled mesh, ghost mesh and rigid body under the scene
    // item_host_mutex. A no-op when the graph is unset / never baked or its
    // latest bake was already applied (revision check). Main thread only.
    void apply(erhe::scene::Node& node, Geometry_graph_mesh_entry& entry);

    // Takes the rigid body the bake gave the node back off it.
    void release_rigid_body(erhe::scene::Node* node, Geometry_graph_mesh_entry& entry);

    // Detaches the products from the node that holds them and drops them.
    void release(Geometry_graph_mesh_entry& entry);

    std::unordered_map<erhe::scene::Node*, Geometry_graph_mesh_entry> m_entries;
};

// The geometry-graph-mesh system of the scene holding `node`, or nullptr when
// the node is in no scene.
[[nodiscard]] auto find_geometry_graph_mesh_system(erhe::scene::Node& node) -> Geometry_graph_mesh_system*;

} // namespace editor
