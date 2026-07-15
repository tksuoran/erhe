#pragma once

#include "editor_log.hpp"

#include "erhe_graph/graph.hpp"
#include "erhe_graph/link.hpp"
#include "erhe_graph/node.hpp"
#include "erhe_graph/pin.hpp"
#include "erhe_item/item.hpp"

#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <vector>

namespace editor {

class App_context;

// Payload-agnostic (de)serialization of a graph asset's node graph, shared by
// the geometry graph (Graph_mesh) and the texture graph (Graph_texture). The
// format is v1: nodes carry their factory type + parameters, links reference
// node indices + pin slots (canvas positions live in the editor window, not the
// asset). See doc/graph-editor-shared-plan.md (C4).

// Serialize the node graph to JSON. NodeT is deduced from the node vector.
// graph is non-const because erhe::graph::Graph::get_links() is non-const.
template <typename NodeT>
[[nodiscard]] auto write_graph_asset_json(
    const std::vector<std::shared_ptr<NodeT>>& nodes,
    erhe::graph::Graph&                        graph
) -> nlohmann::json
{
    nlohmann::json root;
    root["version"] = 1;

    nlohmann::json nodes_json = nlohmann::json::array();
    for (const std::shared_ptr<NodeT>& node : nodes) {
        nlohmann::json node_json;
        node_json["type"] = node->get_factory_type_name();
        nlohmann::json parameters = nlohmann::json::object();
        node->write_parameters(parameters);
        node_json["parameters"] = parameters;
        // Node size (Node Properties "Size" scale); optional, default 1.
        if (node->get_ui_scale() != 1.0f) {
            node_json["ui_scale"] = node->get_ui_scale();
        }
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
    for (const std::unique_ptr<erhe::graph::Link>& link : graph.get_links()) {
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

// Rebuild asset's node graph from JSON produced by write_graph_asset_json (or
// the editor's file format). Clears any existing content first. Validates
// version, node types, pin slots and keys before mutating the live graph; a bad
// link is refused by the acyclic graph rather than accepted. Returns false
// (leaving the graph empty) on a structural error. Not undoable - scene load
// only.
//
// AssetT provides graph()/nodes()/get_name()/shared_from_this(); GraphT is the
// concrete graph type (its mark_dirty() is not on the erhe::graph::Graph base);
// make_node maps a type name to a new shared_ptr<NodeT> (or null for unknown);
// set_owning wires each new node back to its owning asset. log_label prefixes the
// diagnostics ("Graph texture" / "Graph mesh").
template <typename AssetT, typename GraphT, typename NodeT, typename MakeNodeFn, typename SetOwningFn>
auto read_graph_asset_json(
    AssetT&               asset,
    const nlohmann::json& root,
    App_context&          context,
    const char*           log_label,
    const MakeNodeFn&     make_node,
    const SetOwningFn&    set_owning
) -> bool
{
    if (!root.is_object()) {
        log_graph_editor->warn("{} load: root is not a JSON object", log_label);
        return false;
    }
    if (root.value("version", 0) != 1) {
        log_graph_editor->warn("{} load: unsupported version {}", log_label, root.value("version", 0));
        return false;
    }

    // Build the nodes first (not yet registered) so link validation can inspect
    // their pins without touching the live graph on failure.
    std::vector<std::shared_ptr<NodeT>> new_nodes;
    const nlohmann::json nodes_json = root.value("nodes", nlohmann::json::array());
    for (const nlohmann::json& node_json : nodes_json) {
        const std::string type_name = node_json.value("type", "");
        const std::shared_ptr<NodeT> node = make_node(context, type_name);
        if (!node) {
            log_graph_editor->warn("{} load: unknown node type '{}'", log_label, type_name);
            return false;
        }
        if (node_json.contains("parameters") && node_json["parameters"].is_object()) {
            node->read_parameters(node_json["parameters"]);
        }
        node->set_ui_scale(node_json.value("ui_scale", 1.0f));
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
            log_graph_editor->warn("{} load: link node index out of range", log_label);
            return false;
        }
        NodeT* source = new_nodes[static_cast<std::size_t>(source_node)].get();
        NodeT* sink   = new_nodes[static_cast<std::size_t>(sink_node)].get();
        if ((source_slot >= source->get_output_pins().size()) || (sink_slot >= sink->get_input_pins().size())) {
            log_graph_editor->warn("{} load: link pin slot out of range", log_label);
            return false;
        }
        if (source->get_output_pins().at(source_slot).get_key() != sink->get_input_pins().at(sink_slot).get_key()) {
            log_graph_editor->warn("{} load: link pin key mismatch", log_label);
            return false;
        }
    }

    // Clear any existing content, then register the new nodes and connect links.
    GraphT& graph = asset.graph();
    std::vector<std::shared_ptr<NodeT>>& live_nodes = asset.nodes();
    const std::vector<std::shared_ptr<NodeT>> existing = live_nodes; // copy
    for (const std::shared_ptr<NodeT>& node : existing) {
        graph.unregister_node(node.get());
        node->on_removed_from_graph();
    }
    live_nodes.clear();

    constexpr uint64_t flags = erhe::Item_flags::visible | erhe::Item_flags::content | erhe::Item_flags::show_in_ui;
    const std::shared_ptr<AssetT> owning = std::dynamic_pointer_cast<AssetT>(asset.shared_from_this());
    for (const std::shared_ptr<NodeT>& node : new_nodes) {
        node->enable_flag_bits(flags);
        set_owning(*node, owning);
        live_nodes.push_back(node);
        graph.register_node(node.get());
        node->mark_dirty();
    }
    // Connect links using the stored slots. Graph::connect refuses a cycle, so a
    // crafted cyclic file degrades to a dropped link rather than a permanently
    // dirty graph.
    std::size_t link_index = 0;
    for (const nlohmann::json& link_json : links_json) {
        const int         source_node = link_json.value("source_node", -1);
        const std::size_t source_slot = link_json.value("source_slot", std::size_t{0});
        const int         sink_node   = link_json.value("sink_node",   -1);
        const std::size_t sink_slot   = link_json.value("sink_slot",   std::size_t{0});
        NodeT* source = new_nodes[static_cast<std::size_t>(source_node)].get();
        NodeT* sink   = new_nodes[static_cast<std::size_t>(sink_node)].get();
        erhe::graph::Link* link = graph.connect(&source->get_output_pins().at(source_slot), &sink->get_input_pins().at(sink_slot));
        if (link == nullptr) {
            log_graph_editor->warn("{} load: link {} refused (cycle?)", log_label, link_index);
        }
        ++link_index;
    }
    graph.mark_dirty();
    log_graph_editor->info("{} '{}' loaded ({} nodes, {} links)", log_label, asset.get_name(), new_nodes.size(), links_json.size());
    return true;
}

} // namespace editor
