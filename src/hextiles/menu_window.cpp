#include "menu_window.hpp"

#include "hextiles_log.hpp"
#include "map.hpp"
#include "new_game_window.hpp"
#include "map_window.hpp"
#include "tiles.hpp"
#include "game/game.hpp"
#include "map_editor/map_editor.hpp"
#include "map_editor/map_tool_window.hpp"
#include "map_editor/terrain_palette_window.hpp"
#include "map_generator/map_generator.hpp"
#include "type_editors/terrain_editor_window.hpp"
#include "type_editors/terrain_group_editor_window.hpp"
#include "type_editors/terrain_replacement_rule_editor_window.hpp"
#include "type_editors/unit_editor_window.hpp"

#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_verify/verify.hpp"

#include <imgui/imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

namespace hextiles {

Menu_window::Menu_window(
    erhe::commands::Commands&          commands,
    erhe::imgui::Imgui_renderer&       imgui_renderer,
    erhe::imgui::Imgui_windows&        imgui_windows,
    erhe::window::Input_event_handler& input_event_handler,
    Map_window&                        map_window,
    Tiles&                             tiles,
    Tile_renderer&                     tile_renderer
)
    : Imgui_window         {imgui_renderer, imgui_windows, "Menu", "menu"}
    , m_input_event_handler{input_event_handler}
    , m_tiles              {tiles}
    , m_tile_renderer      {tile_renderer}
    , m_map_window         {map_window}
    , m_game               {commands, imgui_renderer, imgui_windows, m_map_window, *this, m_tile_renderer, m_tiles}
    , m_map_editor         {commands, imgui_renderer, imgui_windows, m_map_window, *this, m_tile_renderer, m_tiles}
    , m_new_game_window    {imgui_renderer, imgui_windows, m_game, m_map_editor, m_map_window, *this, m_tile_renderer, m_tiles}
    , m_type_editor        {imgui_renderer, imgui_windows, *this, m_tile_renderer, m_tiles}

{
    show_window();
}

void Menu_window::show_menu()
{
    m_game           .hide_window();
    m_map_editor     .hide_windows();
    m_map_window     .hide_window();
    m_new_game_window.hide_window();
    m_type_editor    .hide_windows();
    show_window();
}

void Menu_window::imgui()
{
    constexpr ImVec2 button_size{130.0f, 0.0f};

    if (ImGui::Button("New Game", button_size)) {
        hide_window();
        m_new_game_window.show_window();
    }
    //if (ImGui::Button("Load Game")) {
    //    hide();
    //    get<Map_window>()->show();
    //}
    if (ImGui::Button("Map Editor", button_size)) {
        hide_window();
        m_map_window.set_map(m_map_editor.get_map());
        //get<Map_editor>()->get_map();
        m_map_window.show_window();
        m_map_editor.show_windows();
    }
    if (ImGui::Button("Terrain Editor", button_size)) {
        hide_window();
        m_type_editor.terrain_editor_window                 .show_window();
        m_type_editor.terrain_group_editor_window           .show_window();
        m_type_editor.terrain_replacement_rule_editor_window.show_window();
    }
    if (ImGui::Button("Unit Editor", button_size)) {
        hide_window();
        m_type_editor.unit_editor_window.show_window();
    }
    if (ImGui::Button("Quit", button_size)) {
        m_input_event_handler.on_window_close_event(
            erhe::window::Input_event{
                .type      = erhe::window::Input_event_type::window_close_event,
                .timestamp = std::chrono::steady_clock::now(),
                .u         = { .dummy = false }
            }
        ); // hacky(ish)?
    }
}

} // namespace hextiles
