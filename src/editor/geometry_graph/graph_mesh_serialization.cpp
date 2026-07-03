#include "geometry_graph/graph_mesh_serialization.hpp"
#include "geometry_graph/geometry_graph.hpp"
#include "geometry_graph/geometry_graph_node.hpp"
#include "geometry_graph/geometry_graph_node_factory.hpp"
#include "geometry_graph/graph_mesh.hpp"

#include "editor_log.hpp"

#include "erhe_graph/link.hpp"
#include "erhe_graph/node.hpp"
#include "erhe_graph/pin.hpp"
#include "erhe_item/item.hpp"

#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <vector>

namespace editor {

auto write_graph_mesh_graph(Graph_mesh& graph_mesh) -> nlohmann::json
{
    const std::vector<std::shared_ptr<Geometry_graph_node>>& nodes = graph_mesh.nodes();

    nlohmann::json root;
    root["version"] = 1;

    nlohmann::json nodes_json = nlohmann::json::array();
    for (const std::shared_ptr<Geometry_graph_node>& node : nodes) {
        nlohmann::json node_json;
        node_json["type"] = node->get_factory_type_name();
        nlohmann::json parameters = nlohmann::json::object();
        node->write_parameters(parameters);
        node_json["parameters"] = parameters;
        nodes_json.push_back(node_json);
    }
    root["nodes"] = nodes_json;

    const auto index_of = [&nodes](const erhe::graph::Node* owner) -> int {
        for (std::size_t i = 0, end = nodes.size(); i < end; ++i) {
            if (nodes[i].get() == owner) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };

    nlohmann::json links_json = nlohmann::json::array();
    for (const std::unique_ptr<erhe::graph::Link>& link : graph_mesh.graph().get_links()) {
        const int source_node = index_of(link->get_source()->get_owner_node());
        const int sink_node   = index_of(link->get_sink  ()->get_owner_node());
        if ((source_node < 0) || (sink_node < 0)) {
            continue;
        }
        links_json.push_back({
            {"source_node", source_node},
            {"source_slot", link->get_source()->get_slot()},
            {"sink_node",   sink_node},
            {"sink_slot",   link->get_sink()->get_slot()}
        });
    }
    root["links"] = links_json;
    return root;
}

auto read_graph_mesh_graph(Graph_mesh& graph_mesh, const nlohmann::json& root, App_context& context) -> bool
{
    if (!root.is_object()) {
        log_graph_editor->warn("Graph mesh load: root is not a JSON object");
        return false;
    }
    if (root.value("version", 0) != 1) {
        log_graph_editor->warn("Graph mesh load: unsupported version {}", root.value("version", 0));
        return false;
    }

    // Build the nodes first (not yet registered) so link validation can
    // inspect their pins without touching the live graph on failure.
    std::vector<std::shared_ptr<Geometry_graph_node>> new_nodes;
    const nlohmann::json nodes_json = root.value("nodes", nlohmann::json::array());
    for (const nlohmann::json& node_json : nodes_json) {
        const std::string type_name = node_json.value("type", "");
        const std::shared_ptr<Geometry_graph_node> node = make_geometry_graph_node(context, type_name);
        if (!node) {
            log_graph_editor->warn("Graph mesh load: unknown node type '{}'", type_name);
            return false;
        }
        if (node_json.contains("parameters") && node_json["parameters"].is_object()) {
            node->read_parameters(node_json["parameters"]);
        }
        new_nodes.push_back(node);
    }

    const int node_count = static_cast<int>(new_nodes.size());
    const nlohmann::json links_json = root.value("links", nlohmann::json::array());
    for (const nlohmann::json& link_json : links_json) {
        const int         source_node = link_json.value("source_node", -1);
        const std::size_t source_slot = link_json.value("source_slot", std::size_t{0});
        const int         sink_node   = link_json.value("sink_node",   -1);
        const std::size_t sink_slot   = link_json.value("sink_slot",   std::size_t{0});
        if ((source_node < 0) || (source_node >= node_count) || (sink_node < 0) || (sink_node >= node_count)) {
            log_graph_editor->warn("Graph mesh load: link node index out of range");
            return false;
        }
        Geometry_graph_node* source = new_nodes[static_cast<std::size_t>(source_node)].get();
        Geometry_graph_node* sink   = new_nodes[static_cast<std::size_t>(sink_node)].get();
        if ((source_slot >= source->get_output_pins().size()) || (sink_slot >= sink->get_input_pins().size())) {
            log_graph_editor->warn("Graph mesh load: link pin slot out of range");
            return false;
        }
        if (source->get_output_pins().at(source_slot).get_key() != sink->get_input_pins().at(sink_slot).get_key()) {
            log_graph_editor->warn("Graph mesh load: link pin key mismatch");
            return false;
        }
    }

    // Clear any existing content, then register the new nodes and connect links.
    Geometry_graph& graph = graph_mesh.graph();
    std::vector<std::shared_ptr<Geometry_graph_node>>& live_nodes = graph_mesh.nodes();
    const std::vector<std::shared_ptr<Geometry_graph_node>> existing = live_nodes; // copy
    for (const std::shared_ptr<Geometry_graph_node>& node : existing) {
        graph.unregister_node(node.get());
        node->on_removed_from_graph();
    }
    live_nodes.clear();

    constexpr uint64_t flags = erhe::Item_flags::visible | erhe::Item_flags::content | erhe::Item_flags::show_in_ui;
    const std::shared_ptr<Graph_mesh> owning = std::dynamic_pointer_cast<Graph_mesh>(graph_mesh.shared_from_this());
    for (const std::shared_ptr<Geometry_graph_node>& node : new_nodes) {
        node->enable_flag_bits(flags);
        node->set_owning_graph_mesh(owning);
        live_nodes.push_back(node);
        graph.register_node(node.get());
        node->mark_dirty();
    }
    // Connect links using the stored slots. Graph::connect refuses a cycle,
    // so a crafted cyclic file degrades to a dropped link rather than a
    // permanently dirty graph.
    std::size_t link_index = 0;
    for (const nlohmann::json& link_json : links_json) {
        const int         source_node = link_json.value("source_node", -1);
        const std::size_t source_slot = link_json.value("source_slot", std::size_t{0});
        const int         sink_node   = link_json.value("sink_node",   -1);
        const std::size_t sink_slot   = link_json.value("sink_slot",   std::size_t{0});
        Geometry_graph_node* source = new_nodes[static_cast<std::size_t>(source_node)].get();
        Geometry_graph_node* sink   = new_nodes[static_cast<std::size_t>(sink_node)].get();
        erhe::graph::Link* link = graph.connect(&source->get_output_pins().at(source_slot), &sink->get_input_pins().at(sink_slot));
        if (link == nullptr) {
            log_graph_editor->warn("Graph mesh load: link {} refused (cycle?)", link_index);
        }
        ++link_index;
    }
    graph.mark_dirty();
    log_graph_editor->info("Graph mesh '{}' loaded ({} nodes, {} links)", graph_mesh.get_name(), new_nodes.size(), links_json.size());
    return true;
}

} // namespace editor
