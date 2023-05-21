#include "erhe/application/imgui/imgui_helpers.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

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
    if (mode == Item_mode::disabled) {
        ImGui::PushStyleColor(ImGuiCol_CheckMark, c_color_disabled);
    }
    ImGui::Checkbox(label, value);
    if (mode == Item_mode::disabled) {
        ImGui::PopStyleColor(1);
        *value = original_value;
    }
}

namespace {

static const float DRAG_MOUSE_THRESHOLD_FACTOR = 0.50f;    // Multiplier for the default value of io.MouseDragThreshold to make DragFloat/DragInt react faster to mouse drags.

class Custom_drag_result
{
public:
    bool text_edited{false};
    bool drag_edited{false};
};

// Fork of DragScalar() so we can differentiate between drag and text edit
auto custom_drag_scalar(
    const char* const label,
    void* const       p_data,
    const float       v_speed = 1.0f,
    const float       min     = 0.0f,
    const float       max     = 0.0f,
    const char*       format  = "%.3f"
) -> Custom_drag_result
{
    using namespace ImGui;
    Custom_drag_result result;
    ImGuiDataType      data_type = ImGuiDataType_Float;
    ImGuiSliderFlags   flags = 0;
    const float* const p_min = &min;
    const float* const p_max = &max;

    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return result;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const float w = CalcItemWidth();

    const ImVec2 label_size = CalcTextSize(label, NULL, true);
    const ImRect frame_bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(w, label_size.y + style.FramePadding.y * 2.0f));
    const ImRect total_bb(frame_bb.Min, frame_bb.Max + ImVec2(label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f, 0.0f));

    const bool temp_input_allowed = (flags & ImGuiSliderFlags_NoInput) == 0;
    ItemSize(total_bb, style.FramePadding.y);
    if (!ItemAdd(total_bb, id, &frame_bb, temp_input_allowed ? ImGuiItemFlags_Inputable : 0))
        return result;

    // Default format string when passing NULL
    if (format == NULL)
        format = DataTypeGetInfo(data_type)->PrintFmt;

    const bool hovered = ItemHoverable(frame_bb, id);
    bool temp_input_is_active = temp_input_allowed && TempInputIsActive(id);
    if (!temp_input_is_active)
    {
        // Tabbing or CTRL-clicking on Drag turns it into an InputText
        const bool input_requested_by_tabbing = temp_input_allowed && (g.LastItemData.StatusFlags & ImGuiItemStatusFlags_FocusedByTabbing) != 0;
        const bool clicked = hovered && IsMouseClicked(0, id);
        const bool double_clicked = (hovered && g.IO.MouseClickedCount[0] == 2 && TestKeyOwner(ImGuiKey_MouseLeft, id));
        const bool make_active = (input_requested_by_tabbing || clicked || double_clicked || g.NavActivateId == id);
        if (make_active && (clicked || double_clicked))
            SetKeyOwner(ImGuiKey_MouseLeft, id);
        if (make_active && temp_input_allowed)
            if (input_requested_by_tabbing || (clicked && g.IO.KeyCtrl) || double_clicked || (g.NavActivateId == id && (g.NavActivateFlags & ImGuiActivateFlags_PreferInput)))
                temp_input_is_active = true;

        // (Optional) simple click (without moving) turns Drag into an InputText
        if (g.IO.ConfigDragClickToInputText && temp_input_allowed && !temp_input_is_active)
            if (g.ActiveId == id && hovered && g.IO.MouseReleased[0] && !IsMouseDragPastThreshold(0, g.IO.MouseDragThreshold * DRAG_MOUSE_THRESHOLD_FACTOR))
            {
                g.NavActivateId = id;
                g.NavActivateFlags = ImGuiActivateFlags_PreferInput;
                temp_input_is_active = true;
            }

        if (make_active && !temp_input_is_active)
        {
            SetActiveID(id, window);
            SetFocusID(id, window);
            FocusWindow(window);
            g.ActiveIdUsingNavDirMask = (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right);
        }
    }

    if (temp_input_is_active)
    {
        // Only clamp CTRL+Click input when ImGuiSliderFlags_AlwaysClamp is set
        const bool is_clamp_input = (flags & ImGuiSliderFlags_AlwaysClamp) != 0 && (p_min == NULL || p_max == NULL || DataTypeCompare(data_type, p_min, p_max) < 0);
        result.text_edited = TempInputScalar(frame_bb, id, label, data_type, p_data, format, is_clamp_input ? p_min : NULL, is_clamp_input ? p_max : NULL);
        return result;
    }

    // Draw frame
    const ImU32 frame_col = GetColorU32(g.ActiveId == id ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
    RenderNavHighlight(frame_bb, id);
    RenderFrame(frame_bb.Min, frame_bb.Max, frame_col, true, style.FrameRounding);

    // Drag behavior
    const bool value_changed = DragBehavior(id, data_type, p_data, v_speed, p_min, p_max, format, flags);
    if (value_changed)
        MarkItemEdited(id);
    result.drag_edited = value_changed;

    // Display value using user-provided display format so user can add prefix/suffix/decorations to the value.
    char value_buf[64];
    const char* value_buf_end = value_buf + DataTypeFormatString(value_buf, IM_ARRAYSIZE(value_buf), data_type, p_data, format);
    if (g.LogEnabled)
        LogSetNextTextDecoration("{", "}");
    RenderTextClipped(frame_bb.Min, frame_bb.Max, value_buf, value_buf_end, NULL, ImVec2(0.5f, 0.5f));

    if (label_size.x > 0.0f)
        RenderText(ImVec2(frame_bb.Max.x + style.ItemInnerSpacing.x, frame_bb.Min.y + style.FramePadding.y), label);

    IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags | (temp_input_allowed ? ImGuiItemStatusFlags_Inputable : 0));
    return result;
}

}

void Value_edit_state::combine(
    const Value_edit_state& other
)
{
    value_changed = value_changed || other.value_changed;
    edit_ended    = edit_ended    || other.edit_ended;
}

auto make_scalar_button(
    float* const      value,
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
    ImGui::Button          (label);
    ImGui::SameLine        ();
    ImGui::PopStyleColor   (4);
    ImGui::SetNextItemWidth(100.0f);

    constexpr float value_speed = 0.02f;
    const auto value_changed = custom_drag_scalar(imgui_label, value, value_speed);
    const bool edit_ended    = ImGui::IsItemDeactivatedAfterEdit();
    //const bool item_edited   = ImGui::IsItemEdited();
    //const bool item_active   = ImGui::IsItemActive();
    //if (value_changed.drag_edited) {
    //    log_node_properties->trace("drag edited for {}", label);
    //}
    //if (value_changed.text_edited) {
    //    log_node_properties->trace("text edited for {}", label);
    //}
    //if (item_edited) {
    //    log_node_properties->trace("item_edited for {}", label);
    //}
    //if (edit_ended) {
    //    log_node_properties->trace("edit_ended for {}", label);
    //}
    return Value_edit_state{
        .value_changed = value_changed.drag_edited || (value_changed.text_edited && edit_ended),
        .edit_ended    = edit_ended
    };
};

auto make_angle_button(
    float&            radians_value,
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
    ImGui::Button          (label);
    ImGui::SameLine        ();
    ImGui::PopStyleColor   (4);
    ImGui::SetNextItemWidth(100.0f);

    float degrees_value = glm::degrees<float>(radians_value);
    constexpr float value_speed = 1.0f;
    constexpr float value_min   = -360.0f;
    constexpr float value_max   =  360.0f;

    const auto drag_result = custom_drag_scalar(
        imgui_label,
        &degrees_value,
        value_speed,
        value_min,
        value_max,
        "%.f\xc2\xb0" // \xc2\xb0 is degree symbol UTF-8 encoded
    );
    const bool edit_ended    = ImGui::IsItemDeactivatedAfterEdit();
    const bool value_changed = drag_result.drag_edited || (drag_result.text_edited && edit_ended);
    if (value_changed) {
        radians_value = glm::radians<float>(degrees_value);
    }
    return Value_edit_state{
        .value_changed = value_changed,
        .edit_ended    = ImGui::IsItemDeactivatedAfterEdit()
    };
};

} // namespace erhe::application
