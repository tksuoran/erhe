#include "windows/imgui_window.hpp"

namespace editor {

const ImVec4 Imgui_window::c_color         = ImVec4(0.3f, 0.4, 0.8f, 1.0f);
const ImVec4 Imgui_window::c_color_hovered = ImVec4(0.4f, 0.5, 0.9f, 1.0f);
const ImVec4 Imgui_window::c_color_active  = ImVec4(0.5f, 0.6, 1.0f, 1.0f); // pressed

const ImVec4 Imgui_window::c_color_disabled         = ImVec4(0.3f, 0.3, 0.3f, 1.0f);
const ImVec4 Imgui_window::c_color_disabled_hovered = ImVec4(0.3f, 0.3, 0.3f, 1.0f);
const ImVec4 Imgui_window::c_color_disabled_active  = ImVec4(0.3f, 0.3, 0.3f, 1.0f); // pressed

bool Imgui_window::make_button(const char* label, const Item_mode mode, const ImVec2 size)
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
    const bool pressed = ImGui::Button(label, size) && (mode != Item_mode::disabled);
    if (mode != Item_mode::normal)
    {
        ImGui::PopStyleColor(3);
    }
    return pressed;
}

void Imgui_window::make_check_box(const char* label, bool* value, const Item_mode mode)
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

}