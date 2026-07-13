#include "windows/active_scene_highlight.hpp"

#include "app_context.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"

#include <imgui/imgui.h>

namespace editor {

auto push_active_scene_window_tint(App_context& context, const Scene_root* scene_root) -> int
{
    if ((scene_root == nullptr) || (context.selection == nullptr)) {
        return 0;
    }
    if (context.selection->get_active_scene_root().get() != scene_root) {
        return 0;
    }

    const auto tint = [](const ImVec4& base, const float t) -> ImVec4 {
        constexpr ImVec4 accent{0.13f, 0.42f, 0.72f, 1.0f};
        return ImVec4{
            base.x + ((accent.x - base.x) * t),
            base.y + ((accent.y - base.y) * t),
            base.z + ((accent.z - base.z) * t),
            base.w
        };
    };

    const ImVec4* const colors = ImGui::GetStyle().Colors;
    // Floating windows show the title bar; docked windows show a dock tab.
    // Tint both so the highlight reads in either layout.
    ImGui::PushStyleColor(ImGuiCol_TitleBg,           tint(colors[ImGuiCol_TitleBg],           0.6f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive,     tint(colors[ImGuiCol_TitleBgActive],     0.8f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed,  tint(colors[ImGuiCol_TitleBgCollapsed],  0.6f));
    ImGui::PushStyleColor(ImGuiCol_Tab,               tint(colors[ImGuiCol_Tab],               0.6f));
    ImGui::PushStyleColor(ImGuiCol_TabHovered,        tint(colors[ImGuiCol_TabHovered],        0.8f));
    ImGui::PushStyleColor(ImGuiCol_TabSelected,       tint(colors[ImGuiCol_TabSelected],       0.8f));
    ImGui::PushStyleColor(ImGuiCol_TabDimmed,         tint(colors[ImGuiCol_TabDimmed],         0.6f));
    ImGui::PushStyleColor(ImGuiCol_TabDimmedSelected, tint(colors[ImGuiCol_TabDimmedSelected], 0.7f));
    return 8;
}

}
