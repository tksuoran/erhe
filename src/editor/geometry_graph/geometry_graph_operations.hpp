#pragma once

#include "graph_editor/graph_operations.hpp"

namespace editor {

class Geometry_graph_node;
class Geometry_graph_window;
class Graph_mesh;

// Binds the shared graph undo / redo operations to the geometry graph. The
// operation bodies live in graph_editor/graph_operations.hpp; distinct Traits
// give distinct instantiations, so the geometry and texture graphs no longer
// need identically-shaped copies kept apart by renaming.
class Geometry_graph_op_traits
{
public:
    using Window = Geometry_graph_window;
    using Asset  = Graph_mesh;
    using Node   = Geometry_graph_node;
    static constexpr const char* label = "Geometry graph";
};

using Geometry_graph_link_record                  = Graph_link_record<Geometry_graph_node>;
using Geometry_graph_node_insert_remove_operation = Graph_node_insert_remove_operation<Geometry_graph_op_traits>;
using Geometry_graph_parameter_operation          = Graph_parameter_operation<Geometry_graph_op_traits>;
using Geometry_graph_link_insert_remove_operation = Graph_link_insert_remove_operation<Geometry_graph_op_traits>;

} // namespace editor
