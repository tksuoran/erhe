#include "graph_editor/graph_editor_window_base.hpp"
#include "graph_editor/graph_editor_node.hpp"
#include "graph_editor/node_edge.hpp"

#include "erhe_graph/graph.hpp"
#include "erhe_graph/link.hpp"
#include "erhe_graph/pin.hpp"
#include "erhe_imgui/imgui_node_editor.h"

#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <string>

namespace editor {

namespace {

[[nodiscard]] auto to_lower_ascii(std::string text) -> std::string
{
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

// The editor-global node clipboard, shared by every graph editor window (main
// thread only). The kind tag (clipboard_kind()) keeps geometry and texture
// content apart; the JSON payload is produced by serialize_nodes_json() and
// consumed by the concrete windows' paste_nodes().
std::string    s_clipboard_kind{};
nlohmann::json s_clipboard{};

} // namespace

void Graph_editor_window_base::apply_node_resize(ax::NodeEditor::EditorContext& node_editor)
{
    ax::NodeEditor::NodeId node_id{};
    ImVec2                 position{};
    ImVec2                 size{};
    if (!node_editor.GetNodeResize(node_id, position, size)) {
        return;
    }
    const std::shared_ptr<Graph_editor_node> node = find_graph_editor_node(static_cast<std::size_t>(node_id.Get()));
    if (!node) {
        return;
    }
    node->set_ui_size(size.x, size.y);
    // Left / top edge pivots move the node; right / bottom keep Min in place.
    const ImVec2 current_position = node_editor.GetNodePosition(node_id);
    if ((current_position.x != position.x) || (current_position.y != position.y)) {
        node_editor.SetNodePosition(node_id, position);
    }
}

void Graph_editor_window_base::node_palette()
{
    build_palette();

    ImGui::SetNextItemWidth(240.0f);
    ImGui::InputTextWithHint("##palette_filter", "Filter nodes...", &m_palette_filter);
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) {
        m_palette_filter.clear();
    }

    const std::string filter    = to_lower_ascii(m_palette_filter);
    const bool        filtering = !filter.empty();

    const auto entry_matches = [&filter](const Palette_entry& entry) -> bool {
        return (to_lower_ascii(entry.label).find(filter) != std::string::npos) ||
               (to_lower_ascii(entry.type_name).find(filter) != std::string::npos);
    };

    for (const Palette_category& category : m_palette_categories) {
        bool any_match = !filtering;
        if (filtering) {
            for (const Palette_entry& entry : category.entries) {
                if (entry_matches(entry)) {
                    any_match = true;
                    break;
                }
            }
            if (!any_match) {
                continue; // hide categories with no matching entry while filtering
            }
        }
        const ImGuiTreeNodeFlags flags = filtering ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None;
        if (ImGui::CollapsingHeader(category.name.c_str(), flags)) {
            for (const Palette_entry& entry : category.entries) {
                if (filtering && !entry_matches(entry)) {
                    continue;
                }
                ImGui::PushID(entry.type_name.c_str());
                if (ImGui::Selectable(entry.label.c_str())) {
                    add_node_from_palette(entry.type_name, nullptr);
                }
                ImGui::PopID();
            }
        }
    }
}

void Graph_editor_window_base::node_background_context_menu(ax::NodeEditor::EditorContext& node_editor)
{
    // Suspend()/Resume() bracket the popup out of the node-editor's channel
    // splitter; ShowBackgroundContextMenu() reports a right-click on the canvas
    // background (using the editor's own gesture, so it does not conflict with
    // right-drag navigation). The str_id is hashed against the host window's ID
    // stack, so each graph window (incl. the issue-#252 extra instances) gets a
    // distinct popup.
    node_editor.Suspend();
    if (node_editor.ShowBackgroundContextMenu()) {
        ImGui::OpenPopup("graph_editor_background_menu");
    }
    if (ImGui::BeginPopup("graph_editor_background_menu")) {
        // Spawn the new node where the right-click opened the menu, not on the
        // auto-advancing grid. The popup outlives the click (the mouse is over
        // a menu item by the time one is chosen), so resolve the canvas
        // position from where the popup was opened. The popup runs suspended
        // (screen space), hence the ScreenToCanvas conversion.
        const ImVec2 spawn_position = node_editor.ScreenToCanvas(ImGui::GetMousePosOnOpeningCurrentPopup());
        if (ImGui::MenuItem("Paste", "Ctrl+V", false, can_paste())) {
            paste_clipboard(spawn_position);
        }
        ImGui::Separator();
        build_palette();
        for (const Palette_category& category : m_palette_categories) {
            if (ImGui::BeginMenu(category.name.c_str())) {
                for (const Palette_entry& entry : category.entries) {
                    if (ImGui::MenuItem(entry.label.c_str())) {
                        add_node_from_palette(entry.type_name, &spawn_position);
                    }
                }
                ImGui::EndMenu();
            }
        }
        ImGui::EndPopup();
    }
    node_editor.Resume();
}

void Graph_editor_window_base::node_context_menu(ax::NodeEditor::EditorContext& node_editor)
{
    // Same Suspend()/Resume() bracketing as the background menu.
    // ShowNodeContextMenu() reports a right-click on a node; a node outside
    // the current selection retargets the selection to it first, so the menu
    // always acts on the selection.
    node_editor.Suspend();
    ax::NodeEditor::NodeId context_node_id{};
    if (node_editor.ShowNodeContextMenu(&context_node_id)) {
        if (!node_editor.IsNodeSelected(context_node_id)) {
            node_editor.SelectNode(context_node_id, false);
        }
        ImGui::OpenPopup("graph_editor_node_menu");
    }
    if (ImGui::BeginPopup("graph_editor_node_menu")) {
        // Paste where the right-click opened the menu (see the background
        // menu's spawn_position note; the popup runs suspended, screen space).
        const ImVec2 paste_position = node_editor.ScreenToCanvas(ImGui::GetMousePosOnOpeningCurrentPopup());
        m_selected_nodes_scratch.clear();
        collect_selected_nodes(m_selected_nodes_scratch);
        const bool has_nodes = !m_selected_nodes_scratch.empty();
        if (ImGui::MenuItem("Cut", "Ctrl+X", false, has_nodes)) {
            copy_nodes(m_selected_nodes_scratch);
            remove_nodes(m_selected_nodes_scratch);
            node_editor.ClearSelection(); // the selection held only the cut nodes
        }
        if (ImGui::MenuItem("Copy", "Ctrl+C", false, has_nodes)) {
            copy_nodes(m_selected_nodes_scratch);
        }
        if (ImGui::MenuItem("Paste", "Ctrl+V", false, can_paste())) {
            paste_clipboard(paste_position);
        }
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, has_nodes)) {
            duplicate_nodes(m_selected_nodes_scratch);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete", "Del", false, has_nodes)) {
            remove_nodes(m_selected_nodes_scratch);
            node_editor.ClearSelection();
        }
        ImGui::EndPopup();
    }
    node_editor.Resume();
}

void Graph_editor_window_base::handle_clipboard_shortcuts(ax::NodeEditor::EditorContext& node_editor)
{
    // The node editor's built-in Ctrl+X / Ctrl+C / Ctrl+V / Ctrl+D shortcuts
    // (active while the canvas is focused). The action context for cut / copy /
    // duplicate is the selection, or the hovered node when nothing is selected.
    if (node_editor.BeginShortcut()) {
        const bool accept_cut       = node_editor.AcceptCut();
        const bool accept_copy      = node_editor.AcceptCopy();
        const bool accept_paste     = node_editor.AcceptPaste();
        const bool accept_duplicate = node_editor.AcceptDuplicate();
        if (accept_cut || accept_copy || accept_duplicate) {
            m_selected_nodes_scratch.clear();
            const int context_size = node_editor.GetActionContextSize();
            if (context_size > 0) {
                // Shortcut triggers are rare (user key presses), so this
                // temporary buffer is not a steady-state allocation.
                std::vector<ax::NodeEditor::NodeId> context_node_ids(static_cast<std::size_t>(context_size));
                const int node_count = node_editor.GetActionContextNodes(context_node_ids.data(), context_size);
                for (int i = 0; i < node_count; ++i) {
                    const std::shared_ptr<Graph_editor_node> node = find_graph_editor_node(static_cast<std::size_t>(context_node_ids[static_cast<std::size_t>(i)].Get()));
                    if (node) {
                        m_selected_nodes_scratch.push_back(node);
                    }
                }
            }
            if (!m_selected_nodes_scratch.empty()) {
                if (accept_duplicate) {
                    duplicate_nodes(m_selected_nodes_scratch);
                } else {
                    copy_nodes(m_selected_nodes_scratch);
                    if (accept_cut) {
                        remove_nodes(m_selected_nodes_scratch);
                        node_editor.ClearSelection();
                    }
                }
            }
        } else if (accept_paste) {
            paste_clipboard(node_editor.ScreenToCanvas(ImGui::GetMousePos()));
        }
    }
    node_editor.EndShortcut();
}

void Graph_editor_window_base::apply_pending_canvas_selection(ax::NodeEditor::EditorContext& node_editor)
{
    // Pasted nodes exist on the canvas only after their first draw; the paste
    // deferred this selection to the frame after it created them.
    if (m_pending_canvas_selection.empty()) {
        return;
    }
    node_editor.ClearSelection();
    for (const std::size_t node_id : m_pending_canvas_selection) {
        node_editor.SelectNode(ax::NodeEditor::NodeId{node_id}, true);
    }
    m_pending_canvas_selection.clear();
}

auto Graph_editor_window_base::serialize_nodes_json(const std::vector<std::shared_ptr<Graph_editor_node>>& nodes) -> nlohmann::json
{
    // The clipboard format is the asset serialization format (see
    // graph_serialization.hpp) plus a kind tag and per-node canvas positions;
    // links reference node indices + pin slots, links reaching outside the
    // copied set are dropped.
    erhe::graph::Graph* graph = get_current_graph();
    if ((graph == nullptr) || nodes.empty()) {
        return nlohmann::json{};
    }
    nlohmann::json root;
    root["kind"]    = clipboard_kind();
    root["version"] = 1;

    nlohmann::json nodes_json = nlohmann::json::array();
    for (const std::shared_ptr<Graph_editor_node>& node : nodes) {
        nlohmann::json node_json;
        node_json["type"] = node->get_factory_type_name();
        nlohmann::json parameters = nlohmann::json::object();
        node->write_parameters(parameters);
        node_json["parameters"] = parameters;
        if (node->get_ui_width() > 0.0f) {
            node_json["width"] = node->get_ui_width();
        }
        if (node->get_ui_height() > 0.0f) {
            node_json["height"] = node->get_ui_height();
        }
        if (node->get_input_pin_edge() != Node_edge::left) {
            node_json["input_edge"] = node->get_input_pin_edge();
        }
        if (node->get_output_pin_edge() != Node_edge::right) {
            node_json["output_edge"] = node->get_output_pin_edge();
        }
        const ImVec2 position = get_node_position(*node.get());
        node_json["x"] = position.x;
        node_json["y"] = position.y;
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
    for (const std::unique_ptr<erhe::graph::Link>& link : graph->get_links()) {
        const int source_node = index_of(link->get_source()->get_owner_node());
        const int sink_node   = index_of(link->get_sink  ()->get_owner_node());
        if ((source_node < 0) || (sink_node < 0)) {
            continue; // link reaches outside the copied set
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

void Graph_editor_window_base::copy_nodes(const std::vector<std::shared_ptr<Graph_editor_node>>& nodes)
{
    nlohmann::json data = serialize_nodes_json(nodes);
    if (data.is_null()) {
        return; // empty selection / empty state - keep the clipboard intact
    }
    s_clipboard      = std::move(data);
    s_clipboard_kind = clipboard_kind();
}

auto Graph_editor_window_base::can_paste() const -> bool
{
    return !s_clipboard_kind.empty() && (s_clipboard_kind == clipboard_kind());
}

void Graph_editor_window_base::paste_clipboard(const ImVec2& canvas_position)
{
    if (!can_paste()) {
        return;
    }
    std::vector<std::size_t> pasted = paste_nodes(s_clipboard, canvas_position);
    if (!pasted.empty()) {
        m_pending_canvas_selection = std::move(pasted);
    }
}

void Graph_editor_window_base::duplicate_nodes(const std::vector<std::shared_ptr<Graph_editor_node>>& nodes)
{
    const nlohmann::json data = serialize_nodes_json(nodes);
    if (data.is_null()) {
        return;
    }
    // Offset the duplicate block slightly down-right from the original so it
    // is visibly a copy and grabbable without deselecting.
    constexpr float duplicate_offset = 40.0f; // canvas units
    ImVec2 top_left{FLT_MAX, FLT_MAX};
    for (const std::shared_ptr<Graph_editor_node>& node : nodes) {
        const ImVec2 position = get_node_position(*node.get());
        top_left.x = std::min(top_left.x, position.x);
        top_left.y = std::min(top_left.y, position.y);
    }
    std::vector<std::size_t> pasted = paste_nodes(data, ImVec2{top_left.x + duplicate_offset, top_left.y + duplicate_offset});
    if (!pasted.empty()) {
        m_pending_canvas_selection = std::move(pasted);
    }
}

} // namespace editor
