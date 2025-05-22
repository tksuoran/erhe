#include "extra_widgets.hpp"
#include <imgui.h>
#include <array>
#include "imgui_internal.hpp"

namespace ImGG {

template<size_t size>
static auto selector_with_tooltip(
    const char*                          label,
    size_t*                              current_item_index,
    const std::array<const char*, size>& items,
    const char*                          longuest_item_name, // Use the longuest word to choose the selector's size
    const std::array<const char*, size>& tooltips,
    const bool                           should_show_tooltip
) -> bool
{
    const auto width{
        ImGui::CalcTextSize(longuest_item_name).x
        + ImGui::GetFrameHeightWithSpacing()
        + ImGui::GetStyle().FramePadding.x * 2.f};
    ImGui::SetNextItemWidth(width);

    auto modified{false};
    if (ImGui::BeginCombo(label, items[*current_item_index]))
    {
        for (size_t n = 0; n < items.size(); n++)
        {
            const bool is_selected{(*current_item_index == n)};
            if (ImGui::Selectable(items[n], is_selected))
            {
                *current_item_index = n;
                modified            = true;
            }

            if (is_selected)
            {
                ImGui::SetItemDefaultFocus();
            }
            if (should_show_tooltip)
            {
                ImGui::SetItemTooltip("%s", tooltips[n]);
            }
        }
        ImGui::EndCombo();
    }
    return modified;
}

auto random_mode_widget(
    const char* label,
    bool*       should_use_a_random_color_for_the_new_marks,
    const bool  should_show_tooltip
) -> bool
{
    const bool modified = ImGui::Checkbox(
        label,
        should_use_a_random_color_for_the_new_marks
    );
    if (should_show_tooltip)
    {
        ImGui::SetItemTooltip("%s", "The new marks will use a random color instead of keeping the one the gradient had at that position.");
    }
    return modified;
}

auto wrap_mode_widget(const char* label, WrapMode* wrap_mode, const bool should_show_tooltip) -> bool
{
    static constexpr std::array<const char*, 3> items = {
        "Clamp",
        "Repeat",
        "Mirror Repeat",
    };
    static constexpr std::array<const char*, 3> tooltips = {
        "If the position gets bigger than 1, maps it to 1. If it gets smaller than 0, maps it to 0.",
        "Maps the number line to a bunch of copies of [0, 1] stuck together.",
        "Like `Repeat` except that every other range is flipped.",
    };

    return selector_with_tooltip(
        label,
        reinterpret_cast<size_t*>(wrap_mode),
        items,
        "Mirror Repeat",
        tooltips,
        should_show_tooltip
    );
}

auto interpolation_mode_widget(const char* label, Interpolation* interpolation_mode, const bool should_show_tooltip) -> bool
{
    static constexpr std::array<const char*, 2> items = {
        "Linear",
        "Constant",
    };
    static constexpr std::array<const char*, 2> tooltips = {
        "Linear interpolation between two marks",
        "Constant color between two marks",
    };

    return selector_with_tooltip(
        label,
        reinterpret_cast<size_t*>(interpolation_mode),
        items,
        "Constant",
        tooltips,
        should_show_tooltip
    );
}
} // namespace ImGG