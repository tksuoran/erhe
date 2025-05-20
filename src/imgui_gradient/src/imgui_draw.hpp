#pragma once

#include "Gradient.hpp"
#include "Interpolation.hpp"
#include "Settings.hpp"
#include "internal.hpp"
#include <imgui_internal.h>

namespace ImGG {

inline void draw_border(ImRect border_rect)
{
    static constexpr float rounding{1.f};
    static constexpr float thickness{2.f};
    ImGui::GetWindowDrawList()->AddRect(border_rect.GetTL(), border_rect.GetBR(), internal::border_color(), rounding, ImDrawFlags_None, thickness);
}

void draw_gradient(
    ImDrawList&     draw_list,
    const Gradient& gradient,
    ImVec2          gradient_position,
    ImVec2          size
);

void draw_marks(
    ImDrawList&     draw_list,
    ImVec2          mark_position,
    ImU32           mark_color,
    float           gradient_height,
    bool            mark_is_selected,
    Settings const& settings
);

} // namespace ImGG