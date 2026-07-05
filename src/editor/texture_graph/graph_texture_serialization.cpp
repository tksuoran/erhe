#include "texture_graph/graph_texture_serialization.hpp"
#include "texture_graph/graph_texture.hpp"
#include "texture_graph/texture_graph.hpp"
#include "texture_graph/texture_graph_node.hpp"
#include "texture_graph/texture_graph_node_factory.hpp"

#include "graph_editor/graph_serialization.hpp"

#include <memory>

namespace editor {

auto write_graph_texture_graph(Graph_texture& graph_texture) -> nlohmann::json
{
    return write_graph_asset_json<Texture_graph_node>(graph_texture.nodes(), graph_texture.graph());
}

auto read_graph_texture_graph(Graph_texture& graph_texture, const nlohmann::json& root, App_context& context) -> bool
{
    return read_graph_asset_json<Graph_texture, Texture_graph, Texture_graph_node>(
        graph_texture, root, context, "Graph texture",
        &make_texture_graph_node,
        [](Texture_graph_node& node, const std::shared_ptr<Graph_texture>& owning) {
            node.set_owning_graph_texture(owning);
        }
    );
}

} // namespace editor
