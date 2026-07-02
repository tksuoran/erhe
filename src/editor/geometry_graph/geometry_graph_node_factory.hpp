#pragma once

#include <memory>
#include <string>

namespace editor {

class App_context;
class Geometry_graph_node;

// Creates a geometry graph node from its factory type name (the names
// used by graph serialization and the MCP geometry_graph_add_node tool):
// box, sphere, torus, cone, disc, subdivide, conway, transform,
// triangulate, normalize, reverse, repair, distribute, instance,
// realize, join, boolean, float, integer, vector, math, output,
// group_input, group_output, group.
//
// Shared by Geometry_graph_window (toolbar, load) and Group_node
// (loading group assets into a private subgraph). Returns nullptr for
// unknown type names; the factory type name is set on the created node.
[[nodiscard]] auto make_geometry_graph_node(App_context& context, const std::string& type_name) -> std::shared_ptr<Geometry_graph_node>;

}
