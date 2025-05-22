#include "HoverChecker.hpp"
#include <imgui.h>
#include <algorithm>

namespace ImGG { namespace internal {

void HoverChecker::update()
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
        _frame_since_not_hovered = 0;
    else
        _frame_since_not_hovered = std::min(_frame_since_not_hovered + 1, 3);
}

void HoverChecker::force_consider_hovered()
{
    _frame_since_not_hovered = 0;
}

auto HoverChecker::is_item_hovered() const -> bool
{
    return _frame_since_not_hovered < 3;
}

}} // namespace ImGG::internal