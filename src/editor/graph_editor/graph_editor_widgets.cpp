#include "graph_editor/graph_editor_widgets.hpp"

#include <imgui/imgui.h>

#include <cstddef>

namespace editor {

namespace {

// Bounds-checked entry, for the combo preview and the list rows. See the header
// note: an unchecked names[index] crashed the #251 Phase 5 pilot when the enum
// was driven out of range through a graph file / MCP set_parameter.
[[nodiscard]] auto safe_name(const char* const* names, const int count, const int index) -> const char*
{
    if ((names == nullptr) || (index < 0) || (index >= count) || (names[index] == nullptr)) {
        return "?";
    }
    return names[index];
}

} // anonymous namespace

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

auto imgui_enum_combo(const char* id, int& index, const char* const* names, const int count, const float scale) -> bool
{
    bool changed = false;
    ImGui::PushID(id);
    ImGui::SetNextItemWidth(140.0f * scale);
    if (ImGui::BeginCombo("##combo", safe_name(names, count, index))) {
        for (int i = 0; i < count; ++i) {
            const bool is_selected = (i == index);
            ImGui::PushID(i);
            if (ImGui::Selectable(safe_name(names, count, i), is_selected) && (i != index)) {
                index   = i;
                changed = true;
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    return changed;
}

auto imgui_enum_combo(const char* id, int& index, const std::vector<std::string>& names, const float scale) -> bool
{
    const int   count   = static_cast<int>(names.size());
    const char* preview = ((index >= 0) && (index < count)) ? names[static_cast<std::size_t>(index)].c_str() : "?";
    bool        changed = false;
    ImGui::PushID(id);
    ImGui::SetNextItemWidth(140.0f * scale);
    if (ImGui::BeginCombo("##combo", preview)) {
        for (int i = 0; i < count; ++i) {
            const bool is_selected = (i == index);
            ImGui::PushID(i);
            if (ImGui::Selectable(names[static_cast<std::size_t>(i)].c_str(), is_selected) && (i != index)) {
                index   = i;
                changed = true;
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    return changed;
}

auto imgui_color_edit(const char* id, float color[4], const float scale) -> bool
{
    ImGui::PushID(id);
    ImGui::SetNextItemWidth(140.0f * scale);
    // NoInputs leaves just the swatch, which opens ImGui's picker popup on
    // click - the whole point here. AlphaPreviewHalf shows the alpha in the
    // swatch itself, since these colors are RGBA.
    const bool changed = ImGui::ColorEdit4(
        "##color",
        color,
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf
    );
    ImGui::PopID();
    return changed;
}

} // namespace editor
