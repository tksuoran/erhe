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
auto make_button              (const char* label, Item_mode mode, const ImVec2 size = ImVec2{0.0f, 0.0f}) -> bool;
auto make_small_button        (const char* label, Item_mode mode) -> bool;
void make_check_box           (const char* label, bool* value, Item_mode mode = Item_mode::normal);
void make_text_with_background(const char* text, float rounding, const ImVec4 background_color);

template <typename T>
void make_combo(
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

} // namespace erhe::imgui
