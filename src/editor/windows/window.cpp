#include "windows/window.hpp"

namespace editor {

const ImVec4 Window::c_color         = ImVec4(0.3f, 0.4, 0.8f, 1.0f);
const ImVec4 Window::c_color_hovered = ImVec4(0.4f, 0.5, 0.9f, 1.0f);
const ImVec4 Window::c_color_active  = ImVec4(0.5f, 0.6, 1.0f, 1.0f); // pressed

const ImVec4 Window::c_color_disabled         = ImVec4(0.3f, 0.3, 0.3f, 1.0f);
const ImVec4 Window::c_color_disabled_hovered = ImVec4(0.3f, 0.3, 0.3f, 1.0f);
const ImVec4 Window::c_color_disabled_active  = ImVec4(0.3f, 0.3, 0.3f, 1.0f); // pressed

bool Window::make_button(const char* label, Item_mode mode, ImVec2 size)
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
    bool pressed = ImGui::Button(label, size) && (mode != Item_mode::disabled);
    if (mode != Item_mode::normal)
    {
        ImGui::PopStyleColor(3);
    }
    return pressed;
}

void Window::make_check_box(const char* label, bool* value, Item_mode mode)
{
    bool original_value = *value;
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