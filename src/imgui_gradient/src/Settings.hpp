#pragma once

#include <imgui.h>
#include <functional>
#include "Flags.hpp"
#include "internal.hpp"

namespace ImGG {

inline auto fully_opaque(ImU32 col) -> ImU32
{
    return col | 0xFF000000;
}

struct Settings {
    float gradient_width{500.f};
    float gradient_height{40.f}; // Must be strictly positive
    float horizontal_margin{10.f};

    /// Distance under the gradient bar to delete a mark by dragging it down.
    /// This behaviour can also be disabled with the Flag::NoDragDowntoDelete.
    float distance_to_delete_mark_by_dragging_down{80.f};

    ImGG::Flags flags{ImGG::Flag::None};

    ImGuiColorEditFlags color_edit_flags{ImGuiColorEditFlags_None};

    /// Controls how the new mark color is chosen.
    /// If true, the new mark color will be a random color,
    /// otherwise it will be the one that the gradient already has at the new mark position.
    bool should_use_a_random_color_for_the_new_marks{false};

    std::function<bool()> plus_button_widget = []() {
        return ImGui::Button("+", internal::button_size());
    };
    std::function<bool()> minus_button_widget = []() {
        return ImGui::Button("-", internal::button_size());
    };

    ImU32 mark_color                  = fully_opaque(ImGui::GetColorU32(ImGuiCol_Button));
    ImU32 mark_hovered_color          = fully_opaque(ImGui::GetColorU32(ImGuiCol_ButtonHovered));
    ImU32 mark_selected_color         = fully_opaque(ImGui::GetColorU32(ImGuiCol_ButtonActive));
    ImU32 mark_selected_hovered_color = fully_opaque(ImGui::GetColorU32(ImGuiCol_ButtonActive));
};

} // namespace ImGG