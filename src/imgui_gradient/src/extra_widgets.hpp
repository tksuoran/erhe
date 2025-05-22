#pragma once

#include "Interpolation.hpp"
#include "WrapMode.hpp"

namespace ImGG {

auto random_mode_widget(
    const char* label,
    bool*       should_use_a_random_color_for_the_new_marks,
    bool        should_show_tooltip = true
) -> bool;

auto wrap_mode_widget(
    const char* label,
    WrapMode*   wrap_mode,
    bool        should_show_tooltip = true
) -> bool;

auto interpolation_mode_widget(
    const char*    label,
    Interpolation* interpolation_mode,
    bool           should_show_tooltip = true
) -> bool;

} // namespace ImGG