#include "type_editors/terrain_replacement_rule_editor_window.hpp"

#include "menu_window.hpp"
#include "tiles.hpp"
#include "type_editors/type_editor.hpp"

#include "erhe_imgui/imgui_windows.hpp"

#include <imgui/imgui.h>

namespace hextiles
{

Terrain_replacement_rule_editor_window::Terrain_replacement_rule_editor_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Menu_window&                 menu_window,
    Tiles&                       tiles,
    Type_editor&                 type_editor
)
    : Imgui_window {imgui_renderer, imgui_windows, "Terrain Replacement Rules", "terrain_replacement_rules"}
    , m_menu_window{menu_window}
    , m_tiles      {tiles}
    , m_type_editor{type_editor}
{
    hide();
}

void Terrain_replacement_rule_editor_window::imgui()
{
    constexpr ImVec2 button_size{110.0f, 0.0f};

    if (ImGui::Button("Back to Menu", button_size)) {
        m_menu_window.show_menu();
    }
    ImGui::SameLine();

    if (ImGui::Button("Load", button_size)) {
        m_tiles.load_terrain_replacement_rule_defs();
    }
    ImGui::SameLine();

    if (ImGui::Button("Save", button_size)) {
        m_tiles.save_terrain_replacement_rule_defs();
    }

    m_type_editor.terrain_replacement_rule_editor_imgui();
}

} // namespace hextiles
