#pragma once

#include "erhe_window/window.hpp"

#include <imgui/imgui.h>

struct ImGuiIO;

namespace erhe::imgui {

enum class Item_mode : unsigned int {
    normal = 0, // normal button
    disabled,   // disabled button
    active      // button in active state
};

void begin_button_style       (Item_mode mode);
void end_button_style         (Item_mode mode);
auto make_button              (const char* label, Item_mode mode, ImVec2 size = ImVec2{0.0f, 0.0f}) -> bool;
auto make_small_button        (const char* label, Item_mode mode) -> bool;
void make_check_box           (const char* label, bool* value, Item_mode mode = Item_mode::normal);
void make_text_with_background(const char* text, float rounding, ImVec4 background_color);

// Combo whose width fits the current preview item, via the native
// ImGuiComboFlags_WidthFitPreview. Use for compact toolbar combos so they do not
// waste precious horizontal space. Mirrors ImGui::Combo's items[] form; returns
// true when the selection changed.
auto combo_fit_width(const char* label, int* current_item, const char* const items[], int items_count) -> bool;

template <typename T>
auto make_combo(
    const char*       label,
    T&                value,
    const char* const items[],
    int               items_count,
    int               popup_max_height_in_items = -1
)
{
    int int_value = static_cast<int>(value);
    ImGui::Combo(label, &int_value, items, items_count, popup_max_height_in_items);
    value = static_cast<T>(int_value);
    return ImGui::IsItemDeactivatedAfterEdit();
}

class Value_edit_state
{
public:
    void combine(const Value_edit_state& other);
    bool value_changed{false};
    bool edit_ended   {false};
    bool active       {false};
};

auto make_scalar_button(
    float*      value,
    float       value_min,
    float       value_max,
    uint32_t    text_color,
    uint32_t    background_color,
    const char* label,
    const char* imgui_label
) -> Value_edit_state;

auto make_scalar_button(
    float*      value,
    float       value_min,
    float       value_max,
    const char* imgui_label
) -> Value_edit_state;

auto make_drag_vec3(
    glm::vec3&               value,
    std::optional<glm::vec3> value_min,
    std::optional<glm::vec3> value_max,
    float                    value_speed, // 0.02f
    ImGuiSliderFlags         flags,
    const char*              imgui_label,
    const char*              format_string
) -> Value_edit_state;

auto make_drag_vec4(
    glm::vec4&               value,
    std::optional<glm::vec4> value_min,
    std::optional<glm::vec4> value_max,
    float                    value_speed, // 0.02f
    ImGuiSliderFlags         flags,
    const char*              imgui_label,
    const char*              format_string
) -> Value_edit_state;

auto make_angle_button(
    float&      radians_value,
    float       value_min,
    float       value_max,
    uint32_t    text_color,
    uint32_t    background_color,
    const char* label,
    const char* imgui_label
) -> Value_edit_state;

auto make_angle_button(
    float&      radians_value,
    float       value_min,
    float       value_max,
    const char* imgui_label
) -> Value_edit_state;

auto begin_popup_with_title_and_open(ImGuiID id, const char* name, bool* open, ImGuiWindowFlags extra_window_flags) -> bool;

// Indeterminate progress spinner: an arc of `radius` centred at `center`,
// `thickness` pixels wide, added to the current window's draw list. The start
// angle is a function of ImGui::GetTime(), so the spinner animates from the
// draws that are already happening and holds no state of its own - there is
// nothing to reset, nothing to tick and nothing to keep alive between frames.
// It emits no ImGui item either, so the caller owns the layout: it decides
// what square the spinner sits in and keeps the row height it would have had.
// `color` is an ImGui packed color (ImGui::GetColorU32).
void draw_spinner(ImVec2 center, float radius, float thickness, ImU32 color);

// True once any value widget (checkbox, slider, drag, input text, color edit,
// combo selectable, ...) has been edited during the current ImGui frame - plain
// buttons do not count. Sample before and after a UI region to detect "an edit
// happened inside this region" (false -> true); used to drive settings-autosave
// notification from the edit itself instead of per-frame change polling. The
// flag is frame-global, so an edit earlier in the same frame makes the region
// test a false positive - callers must treat a positive as "maybe edited".
auto any_item_edited_this_frame() -> bool;

auto begin_drag_drop_source(ImGuiDragDropFlags flags = ImGuiDragDropFlags_None) -> bool;

} // namespace erhe::imgui
