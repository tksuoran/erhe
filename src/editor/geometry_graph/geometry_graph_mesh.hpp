#pragma once

#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_value.hpp"

#include <memory>
#include <optional>
#include <vector>

namespace erhe::scene { class Xformable; using Node = Xformable; }

namespace editor {

class Graph_mesh;

// The effective geometry-graph values of one node: the graph asset whose bake
// the node shows. A plain record, read with read_geometry_graph_mesh().
class Geometry_graph_mesh_data
{
public:
    std::shared_ptr<Graph_mesh> graph_mesh;
};

// The geometry graph a node sources its mesh from, as an attached value group
// of the node itself (doc/erhe/property_system.md section 4.25,
// doc/plans/node_attachments_to_properties.md D1). The same shape USD gives
// `material:binding`: a relationship from the prim to a resource prim.
//
// Geometry_graph_mesh is a registration holder with static members only, not
// an item and not a Dependency_object: it owns the property registration
// (owner type Geometry_graph_mesh, so the qualified name is
// Geometry_graph_mesh.graph_mesh) and the holder of the value is an
// erhe::scene::Node.
//
// Geometry_graph_mesh.graph_mesh is the group's KEY property, with the null
// reference as its default: the node sources its mesh from a graph exactly
// while something names a graph on it. The runtime state the group implies -
// the controlled mesh, the ghost mesh, the controlled rigid body and the bake
// revision already applied - is owned by Geometry_graph_mesh_system, one per
// scene (doc/editor/geometry_graph_mesh.md).
//
// The value carries no serialize flag: the binding's file carriers are the
// native ones, ERHE_node_graphs `node_bindings` in glTF and the `erhe:scene`
// block's `graph_meshes.bound_prims` in USD (D8), and both are fed from
// read_geometry_graph_mesh().
class Geometry_graph_mesh
{
public:
    Geometry_graph_mesh() = delete;

    // The registering class's owner type id. Geometry_graph_mesh has no
    // instances, so it is allocated directly under the root rather than by
    // Item<>.
    [[nodiscard]] static auto property_owner_type() -> erhe::property::Owner_type;

    // The key property. A strong object reference, like every other reference
    // naming a content-library resource: the node's reference is an ordinary
    // resource usership and it dies with the node. Validated to null or a
    // Graph_mesh.
    static const erhe::property::Property<erhe::property::Object_reference> graph_mesh_property;

    // Every Geometry_graph_mesh.* property, registration order, for generic
    // walks.
    [[nodiscard]] static auto all_properties() -> const std::vector<const erhe::property::Dependency_property*>&;
};

// True while the node carries the group: the key property's effective value
// differs from its default (erhe::property::carries_attached_group).
[[nodiscard]] auto carries_geometry_graph_mesh(const erhe::scene::Node& node) -> bool;

// The effective values of one node, or nothing when the node names no graph.
[[nodiscard]] auto read_geometry_graph_mesh(const erhe::scene::Node& node) -> std::optional<Geometry_graph_mesh_data>;

// Binds the node to a graph, or unbinds it when the graph is null. Writing the
// value reaches the scene's Geometry_graph_mesh_system, which releases what the
// previous graph controlled and applies the new graph's latest bake; main
// thread, like every other write of a node value.
void set_geometry_graph_mesh(erhe::scene::Node& node, const std::shared_ptr<Graph_mesh>& graph_mesh);

} // namespace editor
