#include "erhe_imgui/imgui_helpers.hpp"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

namespace erhe::imgui {

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
    if (mode == Item_mode::active) {
        ImGui::PushStyleColor(ImGuiCol_Button,        c_color);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, c_color_hovered);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  c_color_active);
    } else if (mode == Item_mode::disabled) {
        ImGui::PushStyleColor(ImGuiCol_Button,        c_color_disabled);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, c_color_disabled_hovered);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  c_color_disabled_active);
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{2.0f, 1.0f});
}

void end_button_style(const Item_mode mode)
{
    ImGui::PopStyleVar();
    if (mode != Item_mode::normal) {
        ImGui::PopStyleColor(3);
    }
}

auto make_button(const char* label, const Item_mode mode, const ImVec2 size) -> bool
{
    begin_button_style(mode);
    const bool pressed = ImGui::Button(label, size) && (mode != Item_mode::disabled);
    end_button_style(mode);
    return pressed;
}

auto make_small_button(const char* label, const Item_mode mode) -> bool
{
    begin_button_style(mode);
    const bool pressed = ImGui::SmallButton(label) && (mode != Item_mode::disabled);
    end_button_style(mode);
    return pressed;
}

void make_check_box(const char* label, bool* value, const Item_mode mode)
{
    const bool original_value = *value;
    if (mode == Item_mode::disabled) {
        ImGui::PushStyleColor(ImGuiCol_CheckMark, c_color_disabled);
    }
    ImGui::Checkbox(label, value);
    if (mode == Item_mode::disabled) {
        ImGui::PopStyleColor(1);
        *value = original_value;
    }
}

void Value_edit_state::combine(const Value_edit_state& other)
{
    value_changed = value_changed || other.value_changed;
    edit_ended    = edit_ended    || other.edit_ended;
    active        = active        || other.active;
}

auto make_scalar_button(
    float* const      value,
    const float       value_min,
    const float       value_max,
    const uint32_t    text_color,
    const uint32_t    background_color,
    const char* const label,
    const char* const imgui_label
) -> Value_edit_state
{
    ImGui::PushStyleColor  (ImGuiCol_Text,          text_color);
    ImGui::PushStyleColor  (ImGuiCol_Button,        background_color);
    ImGui::PushStyleColor  (ImGuiCol_ButtonHovered, background_color);
    ImGui::PushStyleColor  (ImGuiCol_ButtonActive,  background_color);
    if (label != nullptr) {
        ImGui::SetNextItemWidth(12.0f);
    }
    ImGui::PushItemFlag    (ImGuiItemFlags_NoNav, true);
    ImGui::Button          (label);
    ImGui::PopItemFlag     ();
    ImGui::SameLine        ();
    ImGui::PopStyleColor   (4);
    ImGui::SetNextItemWidth(100.0f);

    constexpr float value_speed = 0.02f;
    const auto value_changed = ImGui::DragFloat(
        imgui_label,
        value,
        value_speed,
        value_min,
        value_max,
        "%.3f",
        ImGuiSliderFlags_NoRoundToFormat
    );
    return Value_edit_state{
        .value_changed = value_changed,
        .edit_ended    = ImGui::IsItemDeactivatedAfterEdit(),
        .active        = ImGui::IsItemActive()
    };
};

auto make_scalar_button(
    float* const      value,
    const float       value_min,
    const float       value_max,
    const char* const imgui_label
) -> Value_edit_state
{
    constexpr float value_speed = 0.02f;
    const auto value_changed = ImGui::DragFloat(
        imgui_label,
        value,
        value_speed,
        value_min,
        value_max,
        "%.3f",
        ImGuiSliderFlags_NoRoundToFormat
    );
    return Value_edit_state{
        .value_changed = value_changed,
        .edit_ended    = ImGui::IsItemDeactivatedAfterEdit(),
        .active        = ImGui::IsItemActive()
    };
};

auto make_angle_button(
    float&            radians_value,
    float             value_min,
    float             value_max,
    const uint32_t    text_color,
    const uint32_t    background_color,
    const char* const label,
    const char* const imgui_label
) -> Value_edit_state
{
    ImGui::PushStyleColor  (ImGuiCol_Text,          text_color);
    ImGui::PushStyleColor  (ImGuiCol_Button,        background_color);
    ImGui::PushStyleColor  (ImGuiCol_ButtonHovered, background_color);
    ImGui::PushStyleColor  (ImGuiCol_ButtonActive,  background_color);
    ImGui::SetNextItemWidth(12.0f);
    ImGui::PushItemFlag    (ImGuiItemFlags_NoNav, true);
    ImGui::Button          (label);
    ImGui::PopItemFlag     ();
    ImGui::SameLine        ();
    ImGui::PopStyleColor   (4);
    ImGui::SetNextItemWidth(100.0f);

    float degrees_value = glm::degrees<float>(radians_value);
    constexpr float value_speed = 1.0f;

    const bool value_changed = ImGui::DragFloat(
        imgui_label,
        &degrees_value,
        value_speed,
        glm::degrees<float>(value_min),
        glm::degrees<float>(value_max),
        "%.f\xc2\xb0",
        ImGuiSliderFlags_NoRoundToFormat
    );
    if (value_changed) {
        radians_value = glm::radians<float>(degrees_value);
    }
    return Value_edit_state{
        .value_changed = value_changed,
        .edit_ended    = ImGui::IsItemDeactivatedAfterEdit(),
        .active        = ImGui::IsItemActive()
    };
};

auto make_angle_button(
    float&            radians_value,
    float             value_min,
    float             value_max,
    const char* const imgui_label
) -> Value_edit_state
{
    float degrees_value = glm::degrees<float>(radians_value);
    constexpr float value_speed = 1.0f;

    const bool value_changed = ImGui::DragFloat(
        imgui_label,
        &degrees_value,
        value_speed,
        glm::degrees<float>(value_min),
        glm::degrees<float>(value_max),
        "%.f\xc2\xb0",
        ImGuiSliderFlags_NoRoundToFormat
    );
    if (value_changed) {
        radians_value = glm::radians<float>(degrees_value);
    }
    return Value_edit_state{
        .value_changed = value_changed,
        .edit_ended    = ImGui::IsItemDeactivatedAfterEdit(),
        .active        = ImGui::IsItemActive()
    };
};

} // namespace erhe::imgui
