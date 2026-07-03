#pragma once

#include <memory>
#include <string>

namespace editor {

class App_context;
class Texture_graph_node;

// Creates a texture graph node from its factory type name (the names used by
// the toolbar, and later by graph serialization and the MCP add-node tool):
// uniform, perlin, voronoi, bricks, shape, blend, colorize, transform,
// brightness_contrast, normal_map.
//
// Mirrors make_geometry_graph_node. Every MVP node type is a
// Texture_descriptor_node wrapping the matching immutable descriptor, so this
// factory looks the descriptor up by name. Returns nullptr for an unknown type
// name; the factory type name is set on the created node.
[[nodiscard]] auto make_texture_graph_node(App_context& context, const std::string& type_name) -> std::shared_ptr<Texture_graph_node>;

} // namespace editor
