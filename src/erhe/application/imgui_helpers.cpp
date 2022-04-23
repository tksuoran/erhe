#include "erhe/application/imgui_helpers.hpp"

namespace erhe::application
{

const ImVec4 c_color                  = ImVec4{0.30f, 0.40f, 0.80f, 1.0f};
const ImVec4 c_color_hovered          = ImVec4{0.40f, 0.50f, 0.90f, 1.0f};
const ImVec4 c_color_active           = ImVec4{0.50f, 0.60f, 1.00f, 1.0f}; // pressed
const ImVec4 c_color_disabled         = ImVec4{0.28f, 0.28f, 0.28f, 1.0f};
const ImVec4 c_color_disabled_hovered = ImVec4{0.28f, 0.28f, 0.28f, 1.0f};
const ImVec4 c_color_disabled_active  = ImVec4{0.28f, 0.28f, 0.28f, 1.0f}; // pressed

bool make_button(const char* label, const Item_mode mode, const ImVec2 size)
{
    if (mode == Item_mode::active)
    {
        ImGui::PushStyleColor(ImGuiCol_Button,        c_color);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, c_color_hovered);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  c_color_active);
    }
    else if (mode == Item_mode::disabled)
    {
        ImGui::PushStyleColor(ImGuiCol_Button,        c_color_disabled);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, c_color_disabled_hovered);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  c_color_disabled_active);
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{2.0f, 1.0f});
    const bool pressed = ImGui::Button(label, size) && (mode != Item_mode::disabled);
    ImGui::PopStyleVar();
    if (mode != Item_mode::normal)
    {
        ImGui::PopStyleColor(3);
    }
    return pressed;
}

void make_check_box(const char* label, bool* value, const Item_mode mode)
{
    const bool original_value = *value;
    if (mode == Item_mode::disabled)
    {
        ImGui::PushStyleColor(ImGuiCol_CheckMark, c_color_disabled);
    }
    ImGui::Checkbox(label, value);
    if (mode == Item_mode::disabled)
    {
        ImGui::PopStyleColor(1);
        *value = original_value;
    }
}

} // namespace erhe::application
