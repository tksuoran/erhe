#include "geometry_graph/nodes/group_nodes.hpp"

#include "geometry_graph/geometry_graph_node_factory.hpp"
#include "geometry_graph/nodes/geometry_output_node.hpp"
#include "editor_log.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_graph/pin.hpp"

#include <geogram/mesh/mesh.h>

#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>
#include <nlohmann/json.hpp>

#include <cfloat>
#include <fstream>
#include <sstream>

namespace editor {

namespace {

// Breaks group reference cycles (a group asset containing a group node
// that leads back to the same asset) and runaway nesting.
constexpr int max_group_evaluation_depth = 8;
thread_local int g_group_evaluation_depth = 0;

}

// Group_input_node

Group_input_node::Group_input_node()
    : Geometry_graph_node{"Group Input"}
{
    make_output_pin(Geometry_pin_key::geometry, "in");
}

void Group_input_node::evaluate(Geometry_graph&)
{
    // The output payload is set externally by the enclosing Group_node;
    // when edited standalone in the graph window it stays empty.
}

void Group_input_node::imgui()
{
    ImGui::TextUnformatted("(group interface)");
}

// Group_output_node

Group_output_node::Group_output_node()
    : Geometry_graph_node{"Group Output"}
{
    make_input_pin(Geometry_pin_key::geometry, "out");
}

void Group_output_node::evaluate(Geometry_graph&)
{
    pull_inputs();
}

void Group_output_node::imgui()
{
    const std::shared_ptr<erhe::geometry::Geometry> geometry = get_input(0).get_geometry();
    if (geometry) {
        const GEO::Mesh& mesh = geometry->get_mesh();
        ImGui::Text("Vertices: %u Facets: %u", mesh.vertices.nb(), mesh.facets.nb());
    } else {
        ImGui::TextUnformatted("(group interface)");
    }
}

// Group_node

Group_node::Group_node(App_context& context)
    : Geometry_graph_node{"Group"}
    , m_context          {context}
{
    make_input_pin(Geometry_pin_key::geometry,  "in");
    make_output_pin(Geometry_pin_key::geometry, "out");
}

void Group_node::on_removed_from_graph()
{
    Geometry_graph_node::on_removed_from_graph();
    // Release internal side effects (an output node inside the asset
    // owns a scene mesh) and force a reload when re-inserted (undo).
    unload_subgraph();
}

void Group_node::unload_subgraph()
{
    for (const std::shared_ptr<Geometry_graph_node>& node : m_subgraph_nodes) {
        m_subgraph.unregister_node(node.get());
        node->on_removed_from_graph();
    }
    m_subgraph_nodes.clear();
    m_input_node     = nullptr;
    m_output_node    = nullptr;
    m_loaded_path.clear();
    m_load_attempted = false;
}

auto Group_node::load_subgraph(const std::filesystem::path& path) -> bool
{
    std::ifstream stream{path};
    if (!stream) {
        log_graph_editor->warn("Group node: cannot open group asset '{}'", path.string());
        return false;
    }
    std::stringstream buffer;
    buffer << stream.rdbuf();
    const nlohmann::json root = nlohmann::json::parse(buffer.str(), nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        log_graph_editor->warn("Group node: group asset '{}' is not valid JSON", path.string());
        return false;
    }
    const int version = root.value("version", 0);
    if (version != 1) {
        log_graph_editor->warn("Group node: group asset '{}' has unsupported version {}", path.string(), version);
        return false;
    }

    const nlohmann::json nodes_json = root.value("nodes", nlohmann::json::array());
    for (const nlohmann::json& node_json : nodes_json) {
        const std::string type_name = node_json.value("type", "");
        const std::shared_ptr<Geometry_graph_node> node = make_geometry_graph_node(m_context, type_name);
        if (!node) {
            log_graph_editor->warn("Group node: unknown node type '{}' in group asset '{}'", type_name, path.string());
            unload_subgraph();
            return false;
        }
        if (node_json.contains("parameters") && node_json["parameters"].is_object()) {
            node->read_parameters(node_json["parameters"]);
        }
        m_subgraph_nodes.push_back(node);
        m_subgraph.register_node(node.get());
    }

    const int node_count = static_cast<int>(m_subgraph_nodes.size());
    const nlohmann::json links_json = root.value("links", nlohmann::json::array());
    for (const nlohmann::json& link_json : links_json) {
        const int         source_node = link_json.value("source_node", -1);
        const std::size_t source_slot = link_json.value("source_slot", std::size_t{0});
        const int         sink_node   = link_json.value("sink_node",   -1);
        const std::size_t sink_slot   = link_json.value("sink_slot",   std::size_t{0});
        if ((source_node < 0) || (source_node >= node_count) || (sink_node < 0) || (sink_node >= node_count)) {
            log_graph_editor->warn("Group node: link node index out of range in group asset '{}'", path.string());
            unload_subgraph();
            return false;
        }
        Geometry_graph_node* source = m_subgraph_nodes[static_cast<std::size_t>(source_node)].get();
        Geometry_graph_node* sink   = m_subgraph_nodes[static_cast<std::size_t>(sink_node)].get();
        if ((source_slot >= source->get_output_pins().size()) || (sink_slot >= sink->get_input_pins().size())) {
            log_graph_editor->warn("Group node: link pin slot out of range in group asset '{}'", path.string());
            unload_subgraph();
            return false;
        }
        erhe::graph::Link* link = m_subgraph.connect(&source->get_output_pins().at(source_slot), &sink->get_input_pins().at(sink_slot));
        if (link == nullptr) {
            log_graph_editor->warn("Group node: link rejected (pin key mismatch or cycle) in group asset '{}'", path.string());
            unload_subgraph();
            return false;
        }
    }

    for (const std::shared_ptr<Geometry_graph_node>& node : m_subgraph_nodes) {
        if (m_input_node == nullptr) {
            m_input_node = dynamic_cast<Group_input_node*>(node.get());
            if (m_input_node != nullptr) {
                continue;
            }
        }
        if (m_output_node == nullptr) {
            m_output_node = dynamic_cast<Group_output_node*>(node.get());
        }
    }
    if (m_output_node == nullptr) {
        log_graph_editor->warn("Group node: group asset '{}' has no Group Output node; the group produces no output", path.string());
    }
    log_graph_editor->info("Group node: loaded group asset '{}' ({} nodes)", path.string(), node_count);
    return true;
}

void Group_node::ensure_loaded()
{
    if (m_load_attempted && (m_path == m_loaded_path)) {
        return;
    }
    unload_subgraph();
    m_load_attempted = true;
    m_loaded_path    = m_path;
    if (m_path.empty()) {
        return;
    }
    if (!load_subgraph(std::filesystem::path{m_path})) {
        unload_subgraph();
        // Remember the failed path so the load is not retried every
        // evaluation; committing the path again retries.
        m_load_attempted = true;
        m_loaded_path    = m_path;
    }
}

void Group_node::evaluate(Geometry_graph&)
{
    pull_inputs();
    ensure_loaded();
    if (m_subgraph_nodes.empty()) {
        set_output(0, Geometry_payload{});
        return;
    }
    if (g_group_evaluation_depth >= max_group_evaluation_depth) {
        log_graph_editor->warn("Group node: group nesting depth exceeds {} (reference cycle?); '{}' evaluates to empty", max_group_evaluation_depth, m_path);
        set_output(0, Geometry_payload{});
        return;
    }
    ++g_group_evaluation_depth;

    if (m_input_node != nullptr) {
        m_input_node->set_output(0, get_input(0));
    }
    m_subgraph.sort();
    for (erhe::graph::Node* node : m_subgraph.get_nodes()) {
        Geometry_graph_node* geometry_graph_node = dynamic_cast<Geometry_graph_node*>(node);
        if (geometry_graph_node != nullptr) {
            geometry_graph_node->evaluate(m_subgraph);
            geometry_graph_node->clear_dirty();
        }
    }
    set_output(0, (m_output_node != nullptr) ? m_output_node->get_input(0) : Geometry_payload{});

    --g_group_evaluation_depth;
}

void Group_node::adopt_subgraph_outputs(Group_node& shadow, const int depth)
{
    if (depth >= max_group_evaluation_depth) {
        return;
    }
    if (shadow.m_subgraph_nodes.empty()) {
        return; // shadow never evaluated (clean at snapshot) or failed to load
    }
    ensure_loaded();
    if ((m_loaded_path != shadow.m_loaded_path) || (m_subgraph_nodes.size() != shadow.m_subgraph_nodes.size())) {
        return; // asset changed between snapshot and finish; a re-evaluation is already pending
    }
    for (std::size_t i = 0, end = m_subgraph_nodes.size(); i < end; ++i) {
        Geometry_graph_node* live_node   = m_subgraph_nodes[i].get();
        Geometry_graph_node* shadow_node = shadow.m_subgraph_nodes[i].get();
        Geometry_output_node* live_output   = dynamic_cast<Geometry_output_node*>(live_node);
        Geometry_output_node* shadow_output = dynamic_cast<Geometry_output_node*>(shadow_node);
        if ((live_output != nullptr) && (shadow_output != nullptr)) {
            live_output->take_evaluated(*shadow_output);
            live_output->apply_evaluated_to_scene();
            continue;
        }
        Group_node* live_group   = dynamic_cast<Group_node*>(live_node);
        Group_node* shadow_group = dynamic_cast<Group_node*>(shadow_node);
        if ((live_group != nullptr) && (shadow_group != nullptr)) {
            live_group->adopt_subgraph_outputs(*shadow_group, depth + 1);
        }
    }
}

void Group_node::imgui()
{
    // Group asset path; committed (and made undoable via the parameter
    // edit gesture) when the input defocuses, not on every keystroke.
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##group_path", &m_path);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        m_load_attempted = false; // retry even when the path text is unchanged
        mark_dirty();
    }
    if (m_subgraph_nodes.empty()) {
        ImGui::TextUnformatted("(no group loaded)");
    } else {
        ImGui::Text("Nodes: %zu", m_subgraph_nodes.size());
    }

    const std::shared_ptr<erhe::geometry::Geometry> geometry = get_output(0).get_geometry();
    if (geometry) {
        const GEO::Mesh& mesh = geometry->get_mesh();
        ImGui::Text("Vertices: %u Facets: %u", mesh.vertices.nb(), mesh.facets.nb());
    }
}

void Group_node::write_parameters(nlohmann::json& out) const
{
    out["path"] = m_path;
}

void Group_node::read_parameters(const nlohmann::json& in)
{
    const std::string path = in.value("path", m_path);
    if (path != m_path) {
        m_path = path;
        m_load_attempted = false;
    }
    mark_dirty();
}

}
