// Texture graph save / load / clear (Texture_graph_window member
// functions; separate translation unit to keep the window UI code and
// the file format apart).
//
// File format (JSON, version 1):
//   {
//       "version": 1,
//       "nodes": [ { "type": "perlin", "position": [x, y], "parameters": { ... } }, ... ],
//       "links": [ { "source_node": 0, "source_slot": 0, "sink_node": 1, "sink_slot": 0 }, ... ]
//   }
// Links reference nodes by index into the "nodes" array. Mirrors the
// geometry graph format (geometry_graph_serialization.cpp).

#include "texture_graph/texture_graph_window.hpp"
#include "texture_graph/texture_graph.hpp"
#include "texture_graph/texture_graph_node.hpp"
#include "texture_graph/texture_graph_operations.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "operations/operation_stack.hpp"

#include "erhe_graph/link.hpp"
#include "erhe_graph/pin.hpp"

#include <imgui/imgui.h>
#include <nlohmann/json.hpp>

#include <cfloat>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

namespace editor {

namespace {

[[nodiscard]] auto node_index_of(
    const std::vector<std::shared_ptr<Texture_graph_node>>& nodes,
    const erhe::graph::Node*                                graph_node
) -> int
{
    for (std::size_t i = 0, end = nodes.size(); i < end; ++i) {
        if (nodes[i].get() == graph_node) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// Kahn's algorithm over (source_node_index, sink_node_index) pairs:
// repeatedly remove nodes with no incoming links; leftovers mean the
// links close a cycle (self links included).
[[nodiscard]] auto links_form_cycle(const std::vector<std::pair<int, int>>& links, const int node_count) -> bool
{
    std::vector<int> in_degree(static_cast<std::size_t>(node_count), 0);
    for (const std::pair<int, int>& link : links) {
        ++in_degree[static_cast<std::size_t>(link.second)];
    }
    std::vector<int> ready;
    for (int node = 0; node < node_count; ++node) {
        if (in_degree[static_cast<std::size_t>(node)] == 0) {
            ready.push_back(node);
        }
    }
    int removed_count = 0;
    while (!ready.empty()) {
        const int node = ready.back();
        ready.pop_back();
        ++removed_count;
        for (const std::pair<int, int>& link : links) {
            if (link.first == node) {
                if (--in_degree[static_cast<std::size_t>(link.second)] == 0) {
                    ready.push_back(link.second);
                }
            }
        }
    }
    return removed_count != node_count;
}

} // anonymous namespace

auto Texture_graph_window::save_graph(const std::filesystem::path& path) -> bool
{
    nlohmann::json root;
    root["version"] = 1;

    nlohmann::json nodes_json = nlohmann::json::array();
    for (const std::shared_ptr<Texture_graph_node>& node : get_nodes()) {
        nlohmann::json node_json;
        node_json["type"] = node->get_factory_type_name();
        const ImVec2 position = get_node_position(*node.get());
        if (is_valid_texture_node_position(position)) {
            node_json["position"] = { position.x, position.y };
        }
        nlohmann::json parameters = nlohmann::json::object();
        node->write_parameters(parameters);
        node_json["parameters"] = parameters;
        nodes_json.push_back(node_json);
    }
    root["nodes"] = nodes_json;

    nlohmann::json links_json = nlohmann::json::array();
    for (const std::unique_ptr<erhe::graph::Link>& link : get_graph().get_links()) {
        const int source_node = node_index_of(get_nodes(), link->get_source()->get_owner_node());
        const int sink_node   = node_index_of(get_nodes(), link->get_sink  ()->get_owner_node());
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

    if (path.has_parent_path()) {
        std::error_code error_code;
        std::filesystem::create_directories(path.parent_path(), error_code);
    }
    std::ofstream stream{path};
    if (!stream) {
        log_graph_editor->warn("Texture graph save: cannot open '{}' for writing", path.string());
        return false;
    }
    stream << root.dump(4);
    const bool ok = stream.good();
    if (ok) {
        log_graph_editor->info("Texture graph saved to '{}' ({} nodes, {} links)", path.string(), get_nodes().size(), get_graph().get_links().size());
    }
    return ok;
}

auto Texture_graph_window::load_graph(const std::filesystem::path& path) -> bool
{
    std::ifstream stream{path};
    if (!stream) {
        log_graph_editor->warn("Texture graph load: cannot open '{}'", path.string());
        return false;
    }
    std::stringstream buffer;
    buffer << stream.rdbuf();
    const nlohmann::json root = nlohmann::json::parse(buffer.str(), nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        log_graph_editor->warn("Texture graph load: '{}' is not valid JSON", path.string());
        return false;
    }
    const int version = root.value("version", 0);
    if (version != 1) {
        log_graph_editor->warn("Texture graph load: '{}' has unsupported version {}", path.string(), version);
        return false;
    }

    Texture_graph_content content;
    const nlohmann::json nodes_json = root.value("nodes", nlohmann::json::array());
    for (const nlohmann::json& node_json : nodes_json) {
        const std::string type_name = node_json.value("type", "");
        const std::shared_ptr<Texture_graph_node> node = make_node(type_name);
        if (!node) {
            log_graph_editor->warn("Texture graph load: unknown node type '{}' in '{}'", type_name, path.string());
            return false;
        }
        if (node_json.contains("parameters") && node_json["parameters"].is_object()) {
            node->read_parameters(node_json["parameters"]);
        }
        ImVec2 position{FLT_MAX, FLT_MAX}; // invalid marker - keep default placement
        if (node_json.contains("position") && node_json["position"].is_array() && (node_json["position"].size() == 2)) {
            position = ImVec2{node_json["position"][0].get<float>(), node_json["position"][1].get<float>()};
        }
        content.nodes.push_back(node);
        content.positions.push_back(position);
    }

    const int node_count = static_cast<int>(content.nodes.size());
    const nlohmann::json links_json = root.value("links", nlohmann::json::array());
    std::vector<std::pair<int, int>> link_endpoints;
    for (const nlohmann::json& link_json : links_json) {
        const int         source_node = link_json.value("source_node", -1);
        const std::size_t source_slot = link_json.value("source_slot", std::size_t{0});
        const int         sink_node   = link_json.value("sink_node",   -1);
        const std::size_t sink_slot   = link_json.value("sink_slot",   std::size_t{0});
        if ((source_node < 0) || (source_node >= node_count) || (sink_node < 0) || (sink_node >= node_count)) {
            log_graph_editor->warn("Texture graph load: link node index out of range in '{}'", path.string());
            return false;
        }
        Texture_graph_node* source = content.nodes[static_cast<std::size_t>(source_node)].get();
        Texture_graph_node* sink   = content.nodes[static_cast<std::size_t>(sink_node)].get();
        if ((source_slot >= source->get_output_pins().size()) || (sink_slot >= sink->get_input_pins().size())) {
            log_graph_editor->warn("Texture graph load: link pin slot out of range in '{}'", path.string());
            return false;
        }
        if (source->get_output_pins().at(source_slot).get_key() != sink->get_input_pins().at(sink_slot).get_key()) {
            log_graph_editor->warn("Texture graph load: link pin key mismatch in '{}'", path.string());
            return false;
        }
        link_endpoints.emplace_back(source_node, sink_node);
        content.links.push_back(
            make_texture_link_record(&source->get_output_pins().at(source_slot), &sink->get_input_pins().at(sink_slot))
        );
    }
    // The graph must stay acyclic (Graph::connect() would refuse the
    // offending link at apply time); reject the whole file like the
    // other validation failures above.
    if (links_form_cycle(link_endpoints, node_count)) {
        log_graph_editor->warn("Texture graph load: links form a cycle in '{}'", path.string());
        return false;
    }

    const std::size_t link_count = content.links.size();
    m_app_context.operation_stack->execute_now(
        std::make_shared<Texture_graph_replace_operation>(*this, get_current_graph_texture(), std::move(content), "Texture graph load")
    );
    log_graph_editor->info("Texture graph loaded from '{}' ({} nodes, {} links)", path.string(), node_count, link_count);
    return true;
}

void Texture_graph_window::clear_graph()
{
    m_app_context.operation_stack->execute_now(
        std::make_shared<Texture_graph_replace_operation>(*this, get_current_graph_texture(), Texture_graph_content{}, "Texture graph clear")
    );
    m_spawn_count = 0; // new nodes start from the canvas origin again
}

} // namespace editor
