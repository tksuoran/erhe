#include "erhe/application/imgui/imgui_helpers.hpp"

namespace erhe::application
{

const ImVec4 c_color                  = ImVec4{0.30f, 0.40f, 0.80f, 1.0f};
const ImVec4 c_color_hovered          = ImVec4{0.40f, 0.50f, 0.90f, 1.0f};
const ImVec4 c_color_active           = ImVec4{0.50f, 0.60f, 1.00f, 1.0f}; // pressed
const ImVec4 c_color_disabled         = ImVec4{0.28f, 0.28f, 0.28f, 1.0f};
const ImVec4 c_color_disabled_hovered = ImVec4{0.28f, 0.28f, 0.28f, 1.0f};
const ImVec4 c_color_disabled_active  = ImVec4{0.28f, 0.28f, 0.28f, 1.0f}; // pressed

void make_text_with_background(const char* text, float rounding, const ImVec4 background_color)
{
    ImGui::PushStyleVar  (ImGuiStyleVar_FrameRounding, rounding);
    ImGui::PushStyleColor(ImGuiCol_Button,        background_color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, background_color);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  background_color);
    ImGui::Button        (text);
    ImGui::PopStyleColor (3);
    ImGui::PopStyleVar   ();
}

void begin_button_style(const Item_mode mode)
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
}

void end_button_style(const Item_mode mode)
{
    ImGui::PopStyleVar();
    if (mode != Item_mode::normal)
    {
        ImGui::PopStyleColor(3);
    }
}

bool make_button(
    const char*     label,
    const Item_mode mode,
    const ImVec2    size,
    const bool      small
)
{
    begin_button_style(mode);
    const bool pressed = small
        ? ImGui::SmallButton(label)
        : ImGui::Button(label, size) && (mode != Item_mode::disabled);
    end_button_style(mode);
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
