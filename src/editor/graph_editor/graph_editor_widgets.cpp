#include "graph_editor/graph_editor_widgets.hpp"

#include <imgui/imgui.h>

namespace editor {

auto imgui_index_stepper(const char* id, int& index, const int count) -> bool
{
    bool changed = false;
    ImGui::PushID(id);
    if (ImGui::ArrowButton("##prev", ImGuiDir_Left) && (count > 0)) {
        index = ((index + count) - 1) % count;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::ArrowButton("##next", ImGuiDir_Right) && (count > 0)) {
        index = (index + 1) % count;
        changed = true;
    }
    ImGui::PopID();
    return changed;
}

auto imgui_enum_stepper(const char* id, int& index, const char* const* names, const int count) -> bool
{
    const bool changed = imgui_index_stepper(id, index, count);
    ImGui::SameLine();
    ImGui::TextUnformatted(((index >= 0) && (index < count)) ? names[index] : "?");
    return changed;
}

} // namespace editor
