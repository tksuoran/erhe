#pragma once

#include <nlohmann/json_fwd.hpp>

namespace editor {

class App_context;
class Graph_mesh;

// Serialize a Graph_mesh's node graph to JSON (geometry-graph v1 format:
// nodes with factory type + parameters, links by node index + pin slot).
// Canvas positions are not included (they live in the editor window's
// ax::NodeEditor context, not the asset); a scene-loaded graph lays its
// nodes out on the spawn grid when first opened. Used by scene
// serialization to persist a Graph_mesh.
[[nodiscard]] auto write_graph_mesh_graph(Graph_mesh& graph_mesh) -> nlohmann::json;

// Rebuild graph_mesh's node graph from JSON produced by
// write_graph_mesh_graph (or the editor's file format). Clears any existing
// content first. Validates version, node types, pin slots and keys; a bad
// link is refused by the acyclic graph rather than accepted. Returns false
// (leaving the graph empty) on a structural error. Not undoable - for
// scene load only.
auto read_graph_mesh_graph(Graph_mesh& graph_mesh, const nlohmann::json& root, App_context& context) -> bool;

} // namespace editor
