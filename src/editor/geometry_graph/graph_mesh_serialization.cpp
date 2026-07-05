#include "geometry_graph/graph_mesh_serialization.hpp"
#include "geometry_graph/geometry_graph.hpp"
#include "geometry_graph/geometry_graph_node.hpp"
#include "geometry_graph/geometry_graph_node_factory.hpp"
#include "geometry_graph/graph_mesh.hpp"

#include "graph_editor/graph_serialization.hpp"

#include <memory>

namespace editor {

auto write_graph_mesh_graph(Graph_mesh& graph_mesh) -> nlohmann::json
{
    return write_graph_asset_json<Geometry_graph_node>(graph_mesh.nodes(), graph_mesh.graph());
}

auto read_graph_mesh_graph(Graph_mesh& graph_mesh, const nlohmann::json& root, App_context& context) -> bool
{
    return read_graph_asset_json<Graph_mesh, Geometry_graph, Geometry_graph_node>(
        graph_mesh, root, context, "Graph mesh",
        &make_geometry_graph_node,
        [](Geometry_graph_node& node, const std::shared_ptr<Graph_mesh>& owning) {
            node.set_owning_graph_mesh(owning);
        }
    );
}

} // namespace editor
