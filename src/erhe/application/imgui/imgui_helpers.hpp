#pragma once

#if defined(ERHE_GUI_LIBRARY_IMGUI)

#include "erhe/toolkit/window.hpp"

#include <imgui.h>

struct ImGuiIO;

namespace erhe::application
{

enum class Item_mode : unsigned int
{
    normal = 0, // normal button
    disabled,   // disabled button
    active      // button in active state
};

bool make_button              (const char* label, Item_mode mode, const ImVec2 size = ImVec2{0.0f, 0.0f}, bool small = false);
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

auto from_erhe           (const erhe::toolkit::Keycode keycode) -> ImGuiKey;
void update_key_modifiers(::ImGuiIO& io, uint32_t modifier_mask);

} // namespace editor

#endif