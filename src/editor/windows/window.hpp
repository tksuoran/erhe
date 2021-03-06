#pragma once

#include "erhe/components/component.hpp"
#include <memory>

#include "imgui.h"

namespace editor
{

struct Pointer_context;
class Scene_manager;

class Window
{
public:
    virtual void window(Pointer_context& pointer_context) = 0;

    enum class Item_mode : unsigned int
    {
        normal = 0, // normal button
        disabled,   // disabled button
        active      // button in active state
    };

    static const ImVec4 c_color;
    static const ImVec4 c_color_hovered;
    static const ImVec4 c_color_active;
    static const ImVec4 c_color_disabled;
    static const ImVec4 c_color_disabled_hovered;
    static const ImVec4 c_color_disabled_active;

    static bool make_button   (const char* label, Item_mode mode, ImVec2 size);
    static void make_check_box(const char* label, bool* value, Item_mode mode = Item_mode::normal);
   
    template <typename T>
    static void make_combo(const char* label, T& value, const char* const items[], int items_count, int popup_max_height_in_items = -1)
    {
        int int_value = static_cast<int>(value);
        ImGui::Combo(label, &int_value, items, items_count, popup_max_height_in_items);
        value = static_cast<T>(int_value);
    }
};

} // namespace editor
