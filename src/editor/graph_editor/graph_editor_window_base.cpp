#include "graph_editor/graph_editor_window_base.hpp"
#include "graph_editor/graph_editor_node.hpp"
#include "graph_editor/graph_node_drag_payload.hpp"
#include "graph_editor/node_edge.hpp"

#include "app_context.hpp"
#include "geometry_graph/geometry_graph_window.hpp"
#include "texture_graph/texture_graph_window.hpp"

#include "erhe_graph/graph.hpp"
#include "erhe_graph/link.hpp"
#include "erhe_graph/pin.hpp"
#include "erhe_imgui/imgui_node_editor.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h> // BeginDragDropTargetCustom
#include <imgui/misc/cpp/imgui_stdlib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstring>
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

void Graph_editor_window_base::spawn_node_from_slot(const std::string& type_name)
{
    // Bring the editor window into view so the spawned node is visible; the
    // node lands on the window's auto-advancing spawn grid. No-op in the
    // empty state (add_node_from_palette checks the edited graph).
    show_window();
    add_node_from_palette(type_name, nullptr);
}

auto Graph_editor_window_base::find_window_by_kind(App_context& context, const char* kind) -> Graph_editor_window_base*
{
    if (kind == nullptr) {
        return nullptr;
    }
    Graph_editor_window_base* const windows[] = {
        static_cast<Graph_editor_window_base*>(context.geometry_graph_window),
        static_cast<Graph_editor_window_base*>(context.texture_graph_window)
    };
    for (Graph_editor_window_base* window : windows) {
        if ((window != nullptr) && (std::strcmp(window->clipboard_kind(), kind) == 0)) {
            return window;
        }
    }
    return nullptr;
}

void Graph_editor_window_base::accept_palette_drop(ax::NodeEditor::EditorContext& node_editor, const ImVec2& rect_min, const ImVec2& rect_max)
{
    // Peek at the payload while the drag is still in flight so a ghost of the
    // prospective node can be drawn at the cursor; IsDelivery() below tells
    // the actual drop apart. The default whole-canvas highlight is replaced
    // by that ghost.
    const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
        c_graph_node_payload_type,
        ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect
    );
    if (payload == nullptr) {
        return; // no palette node dragged over the canvas
    }
    const Graph_node_drag_payload& node_payload = *static_cast<const Graph_node_drag_payload*>(payload->Data);
    if (std::strcmp(node_payload.kind, clipboard_kind()) != 0) {
        return; // a palette entry of the other editor kind - not spawnable here
    }

    if (!payload->IsDelivery()) {
        // Typical plain-node footprint: pin columns + center column +
        // NodePadding wide; the real node is content-sized on spawn.
        draw_canvas_drop_ghost(node_editor, rect_min, rect_max, node_payload.label, ImVec2{306.0f, 150.0f});
        return;
    }

    // The drop happens outside the canvas Begin/End (screen space), so
    // convert through the editor's stored view transform.
    const ImVec2 spawn_position = node_editor.ScreenToCanvas(ImGui::GetMousePos());
    add_node_from_palette(node_payload.type_name, &spawn_position);
}

void Graph_editor_window_base::palette_drop_target(ax::NodeEditor::EditorContext& node_editor, const ImVec2& rect_min, const ImVec2& rect_max)
{
    // Custom target over the whole canvas rect (the ax::NodeEditor canvas is
    // not a single ImGui item, so BeginDragDropTarget() cannot latch onto it).
    const ImGuiID drag_target_id = ImGui::GetID(static_cast<const void*>(this));
    const ImRect  rect{rect_min, rect_max};
    if (!ImGui::BeginDragDropTargetCustom(rect, drag_target_id)) {
        return;
    }
    accept_palette_drop(node_editor, rect_min, rect_max);
    ImGui::EndDragDropTarget();
}

void Graph_editor_window_base::draw_canvas_drop_ghost(
    ax::NodeEditor::EditorContext& node_editor,
    const ImVec2&                  rect_min,
    const ImVec2&                  rect_max,
    const char*                    label,
    const ImVec2&                  footprint
)
{
    // The node spawns with its top-left corner at the cursor, so anchor the
    // ghost there; the footprint is in canvas units, scaled to the zoom.
    const float  zoom      = node_editor.GetCurrentZoom();
    const ImVec2 ghost_min = ImGui::GetMousePos();
    const ImVec2 ghost_max{ghost_min.x + (footprint.x * zoom), ghost_min.y + (footprint.y * zoom)};
    const float  rounding  = node_editor.GetStyle().NodeRounding * zoom;
    ImDrawList*  draw_list = ImGui::GetWindowDrawList();
    draw_list->PushClipRect(rect_min, rect_max, true);
    draw_list->AddRectFilled(ghost_min, ghost_max, IM_COL32(128, 128, 128, 48), rounding);
    draw_list->AddRect      (ghost_min, ghost_max, IM_COL32(204, 204, 204, 200), rounding, ImDrawFlags_RoundCornersAll, 2.0f * zoom);
    draw_list->AddText(
        ImVec2{ghost_min.x + (8.0f * zoom), ghost_min.y + (8.0f * zoom)},
        IM_COL32(230, 230, 230, 220),
        label
    );
    draw_list->PopClipRect();
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
                // Palette entries drag onto the canvas (spawn at the drop
                // position) and into inventory / hotbar slots. The payload
                // copies the strings, so it survives the per-frame palette
                // rebuild.
                if (ImGui::BeginDragDropSource()) {
                    Graph_node_drag_payload payload{};
                    snprintf(payload.kind,      sizeof(payload.kind),      "%s", clipboard_kind());
                    snprintf(payload.type_name, sizeof(payload.type_name), "%s", entry.type_name.c_str());
                    snprintf(payload.label,     sizeof(payload.label),     "%s", entry.label.c_str());
                    ImGui::SetDragDropPayload(c_graph_node_payload_type, &payload, sizeof(payload));
                    ImGui::Text("Add %s node", entry.label.c_str());
                    ImGui::EndDragDropSource();
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
    ax::NodeEditor::EditorContext* node_editor = get_node_editor();
    nlohmann::json links_json = nlohmann::json::array();
    for (const std::unique_ptr<erhe::graph::Link>& link : graph->get_links()) {
        const int source_node = index_of(link->get_source()->get_owner_node());
        const int sink_node   = index_of(link->get_sink  ()->get_owner_node());
        if ((source_node < 0) || (sink_node < 0)) {
            continue; // link reaches outside the copied set
        }
        nlohmann::json link_json{
            {"source_node", source_node},
            {"source_slot", link->get_source()->get_slot()},
            {"sink_node",   sink_node},
            {"sink_slot",   link->get_sink()->get_slot()}
        };
        // Link routing mid points (absolute canvas positions, like the node
        // "x" / "y" above - paste translates them with the block).
        const ax::NodeEditor::LinkId link_id{link.get()};
        const int mid_point_count = node_editor->GetLinkMidPointCount(link_id);
        if (mid_point_count > 0) {
            nlohmann::json mid_points_json = nlohmann::json::array();
            for (int i = 0; i < mid_point_count; ++i) {
                const ImVec2 mid_point = node_editor->GetLinkMidPoint(link_id, i);
                mid_points_json.push_back({mid_point.x, mid_point.y});
            }
            link_json["mid_points"] = mid_points_json;
        }
        links_json.push_back(link_json);
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
