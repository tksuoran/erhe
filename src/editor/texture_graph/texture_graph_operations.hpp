#pragma once

#include "graph_editor/graph_operations.hpp"

namespace editor {

class Graph_texture;
class Texture_graph_node;
class Texture_graph_window;

// Binds the shared graph undo / redo operations to the texture graph. The
// operation bodies live in graph_editor/graph_operations.hpp; distinct Traits
// give distinct instantiations, so the geometry and texture graphs no longer
// need identically-shaped copies kept apart by renaming.
class Texture_graph_op_traits
{
public:
    using Window = Texture_graph_window;
    using Asset  = Graph_texture;
    using Node   = Texture_graph_node;
    static constexpr const char* label = "Texture graph";
};

using Texture_graph_link_record                  = Graph_link_record<Texture_graph_node>;
using Texture_graph_node_insert_remove_operation = Graph_node_insert_remove_operation<Texture_graph_op_traits>;
using Texture_graph_parameter_operation          = Graph_parameter_operation<Texture_graph_op_traits>;
using Texture_graph_link_insert_remove_operation = Graph_link_insert_remove_operation<Texture_graph_op_traits>;

} // namespace editor
