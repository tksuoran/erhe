#include "graph_editor/graph_editor_window_base.hpp"
#include "graph_editor/graph_editor_node.hpp"

#include "erhe_imgui/imgui_node_editor.h"

#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

#include <cctype>
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
                    add_node_from_palette(entry.type_name);
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
        build_palette();
        for (const Palette_category& category : m_palette_categories) {
            if (ImGui::BeginMenu(category.name.c_str())) {
                for (const Palette_entry& entry : category.entries) {
                    if (ImGui::MenuItem(entry.label.c_str())) {
                        add_node_from_palette(entry.type_name);
                    }
                }
                ImGui::EndMenu();
            }
        }
        ImGui::EndPopup();
    }
    node_editor.Resume();
}

} // namespace editor
