#pragma once

#if defined(ERHE_GUI_LIBRARY_IMGUI)

#include <imgui.h>

namespace erhe::application
{

enum class Item_mode : unsigned int
{
    normal = 0, // normal button
    disabled,   // disabled button
    active      // button in active state
};

bool make_button   (const char* label, const Item_mode mode, const ImVec2 size);
void make_check_box(const char* label, bool* value, const Item_mode mode = Item_mode::normal);

template <typename T>
void make_combo(
    const char*       label,
    T&                value,
    const char* const items[],
    const int         items_count,
    const int         popup_max_height_in_items = -1
)
{
    int int_value = static_cast<int>(value);
    ImGui::Combo(label, &int_value, items, items_count, popup_max_height_in_items);
    value = static_cast<T>(int_value);
}

} // namespace editor

#endif