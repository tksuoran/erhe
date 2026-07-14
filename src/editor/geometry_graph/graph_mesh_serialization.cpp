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
    nlohmann::json root = write_graph_asset_json<Geometry_graph_node>(graph_mesh.nodes(), graph_mesh.graph());

    // Display / ghost designation (Houdini display / template flags), as
    // indices into the nodes array - node ids are session-local, indices
    // are the persistent node reference (links use them too). A dangling
    // designation (node deleted) simply omits the key.
    const std::vector<std::shared_ptr<Geometry_graph_node>>& nodes = graph_mesh.nodes();
    const auto index_of_id = [&nodes](const std::size_t id) -> int {
        if (id == 0) {
            return -1;
        }
        for (std::size_t i = 0, end = nodes.size(); i < end; ++i) {
            if (nodes[i]->get_id() == id) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };
    const int display_index = index_of_id(graph_mesh.graph().get_display_node_id());
    const int ghost_index   = index_of_id(graph_mesh.graph().get_ghost_node_id());
    if (display_index >= 0) {
        root["display_node"] = display_index;
    }
    if (ghost_index >= 0) {
        root["ghost_node"] = ghost_index;
    }
    return root;
}

auto read_graph_mesh_graph(Graph_mesh& graph_mesh, const nlohmann::json& root, App_context& context) -> bool
{
    const bool ok = read_graph_asset_json<Graph_mesh, Geometry_graph, Geometry_graph_node>(
        graph_mesh, root, context, "Graph mesh",
        &make_geometry_graph_node,
        [](Geometry_graph_node& node, const std::shared_ptr<Graph_mesh>& owning) {
            node.set_owning_graph_mesh(owning);
        }
    );
    if (!ok) {
        return false;
    }

    // Display / ghost designation: map the persisted node indices back to
    // the freshly created nodes' ids. Missing / out-of-range keys clear the
    // designation. The setters mark the scene outputs dirty, which is
    // harmless here - loaded nodes are all born dirty anyway.
    const std::vector<std::shared_ptr<Geometry_graph_node>>& nodes = graph_mesh.nodes();
    const auto id_of_index = [&nodes](const int index) -> std::size_t {
        if ((index < 0) || (index >= static_cast<int>(nodes.size()))) {
            return 0;
        }
        return nodes[static_cast<std::size_t>(index)]->get_id();
    };
    graph_mesh.graph().set_display_node_id(id_of_index(root.value("display_node", -1)));
    graph_mesh.graph().set_ghost_node_id  (id_of_index(root.value("ghost_node",   -1)));
    return true;
}

} // namespace editor
