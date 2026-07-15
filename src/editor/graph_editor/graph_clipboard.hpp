#pragma once

#include "app_context.hpp"
#include "editor_log.hpp"
#include "graph_editor/graph_editor_node.hpp"
#include "graph_editor/graph_operations.hpp"
#include "graph_editor/node_edge.hpp"
#include "operations/compound_operation.hpp"
#include "operations/operation_stack.hpp"

#include "erhe_graph/graph.hpp"
#include "erhe_graph/link.hpp"
#include "erhe_imgui/imgui_node_editor.h"

#include <imgui/imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cfloat>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace editor {

// Clipboard paste and multi-node removal shared by the graph editors,
// parameterized on the same Traits classes as graph_operations.hpp. The
// clipboard JSON is produced by Graph_editor_window_base::copy_nodes() (nodes
// with factory type + parameters + layout + canvas position, plus the links
// among them; see serialize_nodes_json).

// Rebuilds the clipboard's nodes and links through the concrete factory as ONE
// undo step, anchoring the block's top-left corner at the given canvas position
// (relative layout preserved). Validates node types, link indices, pin slots
// and keys before mutating the live graph. Returns the pasted nodes' ids (used
// to select them on the canvas), empty on refusal.
template <typename Traits, typename MakeNodeFn>
auto paste_graph_nodes(
    typename Traits::Window&                       window,
    App_context&                                   context,
    const std::shared_ptr<typename Traits::Asset>& asset,
    const nlohmann::json&                          clipboard,
    const ImVec2&                                  position,
    const MakeNodeFn&                              make_node
) -> std::vector<std::size_t>
{
    using Node    = typename Traits::Node;
    using Node_op = Graph_node_insert_remove_operation<Traits>;
    using Link_op = Graph_link_insert_remove_operation<Traits>;

    if (!clipboard.is_object() || (clipboard.value("version", 0) != 1)) {
        log_graph_editor->warn("{} paste: unsupported clipboard content", Traits::label);
        return {};
    }

    // Build the nodes first (not yet registered) so link validation can
    // inspect their pins without mutating the live graph on failure.
    std::vector<std::shared_ptr<Node>> new_nodes;
    std::vector<ImVec2>                positions;
    ImVec2                             top_left{FLT_MAX, FLT_MAX};
    const nlohmann::json nodes_json = clipboard.value("nodes", nlohmann::json::array());
    for (const nlohmann::json& node_json : nodes_json) {
        const std::string type_name = node_json.value("type", "");
        const std::shared_ptr<Node> node = make_node(context, type_name);
        if (!node) {
            log_graph_editor->warn("{} paste: unknown node type '{}'", Traits::label, type_name);
            return {};
        }
        if (node_json.contains("parameters") && node_json["parameters"].is_object()) {
            node->read_parameters(node_json["parameters"]);
        }
        node->set_ui_size(node_json.value("width", 0.0f), node_json.value("height", 0.0f));
        node->set_input_pin_edge (node_json.value("input_edge",  Node_edge::left));
        node->set_output_pin_edge(node_json.value("output_edge", Node_edge::right));
        const ImVec2 node_position{node_json.value("x", 0.0f), node_json.value("y", 0.0f)};
        top_left.x = std::min(top_left.x, node_position.x);
        top_left.y = std::min(top_left.y, node_position.y);
        positions.push_back(node_position);
        new_nodes.push_back(node);
    }
    if (new_nodes.empty()) {
        return {};
    }

    const int node_count = static_cast<int>(new_nodes.size());
    const nlohmann::json links_json = clipboard.value("links", nlohmann::json::array());
    for (const nlohmann::json& link_json : links_json) {
        const int         source_node = link_json.value("source_node", -1);
        const std::size_t source_slot = link_json.value("source_slot", std::size_t{0});
        const int         sink_node   = link_json.value("sink_node",   -1);
        const std::size_t sink_slot   = link_json.value("sink_slot",   std::size_t{0});
        if ((source_node < 0) || (source_node >= node_count) || (sink_node < 0) || (sink_node >= node_count)) {
            log_graph_editor->warn("{} paste: link node index out of range", Traits::label);
            return {};
        }
        Node* source = new_nodes[static_cast<std::size_t>(source_node)].get();
        Node* sink   = new_nodes[static_cast<std::size_t>(sink_node)].get();
        if ((source_slot >= source->get_output_pins().size()) || (sink_slot >= sink->get_input_pins().size())) {
            log_graph_editor->warn("{} paste: link pin slot out of range", Traits::label);
            return {};
        }
        if (source->get_output_pins().at(source_slot).get_key() != sink->get_input_pins().at(sink_slot).get_key()) {
            log_graph_editor->warn("{} paste: link pin key mismatch", Traits::label);
            return {};
        }
    }

    // One undo step: every node insert, then every link connect. The link
    // sinks are freshly created pins with no existing links, so there is no
    // replace-on-connect handling to do here.
    Compound_operation::Parameters parameters;
    for (const std::shared_ptr<Node>& node : new_nodes) {
        parameters.operations.push_back(std::make_shared<Node_op>(window, asset, node, Node_op::Mode::insert));
    }
    for (const nlohmann::json& link_json : links_json) {
        Node* source = new_nodes[static_cast<std::size_t>(link_json.value("source_node", -1))].get();
        Node* sink   = new_nodes[static_cast<std::size_t>(link_json.value("sink_node",   -1))].get();
        parameters.operations.push_back(
            std::make_shared<Link_op>(
                window,
                asset,
                &source->get_output_pins().at(link_json.value("source_slot", std::size_t{0})),
                &sink  ->get_input_pins ().at(link_json.value("sink_slot",   std::size_t{0})),
                Link_op::Mode::insert
            )
        );
    }
    context.operation_stack->execute_now(std::make_shared<Compound_operation>(std::move(parameters)));

    // Restore link routing mid points, translated with the pasted block. The
    // erhe link a Link_op created is found by its (freshly created, unique)
    // source / sink pin pair.
    ax::NodeEditor::EditorContext* node_editor = window.get_node_editor();
    const ImVec2 translation{position.x - top_left.x, position.y - top_left.y};
    std::vector<ImVec2> mid_points;
    for (const nlohmann::json& link_json : links_json) {
        const nlohmann::json mid_points_json = link_json.value("mid_points", nlohmann::json::array());
        if (mid_points_json.empty()) {
            continue;
        }
        const erhe::graph::Pin* source_pin =
            &new_nodes[static_cast<std::size_t>(link_json.value("source_node", -1))]
                ->get_output_pins().at(link_json.value("source_slot", std::size_t{0}));
        const erhe::graph::Pin* sink_pin =
            &new_nodes[static_cast<std::size_t>(link_json.value("sink_node", -1))]
                ->get_input_pins().at(link_json.value("sink_slot", std::size_t{0}));
        erhe::graph::Link* created_link = nullptr;
        for (const std::unique_ptr<erhe::graph::Link>& link : asset->graph().get_links()) {
            if ((link->get_source() == source_pin) && (link->get_sink() == sink_pin)) {
                created_link = link.get();
                break;
            }
        }
        if (created_link == nullptr) {
            continue;
        }
        mid_points.clear();
        for (const nlohmann::json& point_json : mid_points_json) {
            if (!point_json.is_array() || (point_json.size() != 2)) {
                mid_points.clear();
                break;
            }
            mid_points.push_back(
                ImVec2{
                    point_json.at(0).get<float>() + translation.x,
                    point_json.at(1).get<float>() + translation.y
                }
            );
        }
        if (!mid_points.empty()) {
            node_editor->SetLinkMidPoints(
                ax::NodeEditor::LinkId{created_link},
                mid_points.data(),
                static_cast<int>(mid_points.size())
            );
        }
    }

    // Anchor the pasted block's top-left corner at the paste position,
    // preserving the copied nodes' relative layout.
    std::vector<std::size_t> pasted_ids;
    pasted_ids.reserve(new_nodes.size());
    for (std::size_t i = 0, end = new_nodes.size(); i < end; ++i) {
        window.set_node_position(
            *new_nodes[i],
            ImVec2{position.x + (positions[i].x - top_left.x), position.y + (positions[i].y - top_left.y)}
        );
        pasted_ids.push_back(new_nodes[i]->get_id());
    }
    return pasted_ids;
}

// Removes the given nodes as ONE undo step (each node operation records and
// restores its links and canvas position). Used by the node context menu's
// Cut / Delete and the Ctrl+X shortcut.
template <typename Traits>
void remove_graph_nodes(
    typename Traits::Window&                               window,
    App_context&                                           context,
    const std::shared_ptr<typename Traits::Asset>&         asset,
    const std::vector<std::shared_ptr<Graph_editor_node>>& nodes
)
{
    using Node    = typename Traits::Node;
    using Node_op = Graph_node_insert_remove_operation<Traits>;
    Compound_operation::Parameters parameters;
    for (const std::shared_ptr<Graph_editor_node>& base_node : nodes) {
        const std::shared_ptr<Node> node = std::dynamic_pointer_cast<Node>(base_node);
        if (node) {
            parameters.operations.push_back(std::make_shared<Node_op>(window, asset, node, Node_op::Mode::remove));
        }
    }
    if (parameters.operations.empty()) {
        return;
    }
    context.operation_stack->execute_now(std::make_shared<Compound_operation>(std::move(parameters)));
}

} // namespace editor
