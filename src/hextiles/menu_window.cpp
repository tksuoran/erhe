#include "menu_window.hpp"

#include "hextiles_log.hpp"
#include "map.hpp"
#include "new_game_window.hpp"
#include "map_window.hpp"
#include "tiles.hpp"
#include "game/game.hpp"
#include "game/game_window.hpp"
#include "map_editor/map_editor.hpp"
#include "map_editor/map_tool_window.hpp"
#include "map_editor/terrain_palette_window.hpp"
#include "map_generator/map_generator.hpp"
#include "type_editors/terrain_editor_window.hpp"
#include "type_editors/terrain_group_editor_window.hpp"
#include "type_editors/terrain_replacement_rule_editor_window.hpp"
#include "type_editors/unit_editor_window.hpp"

#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/application_view.hpp"
#include "erhe/toolkit/verify.hpp"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

namespace hextiles
{

Menu_window* g_menu_window{nullptr};

Menu_window::Menu_window()
    : erhe::components::Component{c_type_name}
    , Imgui_window               {c_title}
{
}

Menu_window::~Menu_window() noexcept
{
    ERHE_VERIFY(g_menu_window == this);
    g_menu_window = nullptr;
}

void Menu_window::declare_required_components()
{
    require<erhe::application::Imgui_windows>();
}

void Menu_window::initialize_component()
{
    ERHE_VERIFY(g_menu_window == nullptr);
    erhe::application::g_imgui_windows->register_imgui_window(this);
    g_menu_window = this;
}

void Menu_window::show_menu()
{
    g_game_window                           ->hide();
    g_map_generator                         ->hide();
    g_map_tool_window                       ->hide();
    g_map_window                            ->hide();
    g_new_game_window                       ->hide();
    g_terrain_editor_window                 ->hide();
    g_terrain_group_editor_window           ->hide();
    g_terrain_palette_window                ->hide();
    g_terrain_replacement_rule_editor_window->hide();
    g_unit_editor_window                    ->hide();
    show();
}

void Menu_window::imgui()
{
    constexpr ImVec2 button_size{130.0f, 0.0f};

    if (ImGui::Button("New Game", button_size))
    {
        hide();
        g_new_game_window->show();
    }
    //if (ImGui::Button("Load Game"))
    //{
    //    hide();
    //    get<Map_window>()->show();
    //}
    if (ImGui::Button("Map Editor", button_size))
    {
        hide();
        g_map_window->set_map(g_map_editor->get_map());
        //get<Map_editor>()->get_map();
        g_map_window            ->show();
        g_terrain_palette_window->show();
        g_map_tool_window       ->show();
    }
    if (ImGui::Button("Terrain Editor", button_size))
    {
        hide();
        g_terrain_editor_window                 ->show();
        g_terrain_group_editor_window           ->show();
        g_terrain_replacement_rule_editor_window->show();
    }
    if (ImGui::Button("Unit Editor", button_size))
    {
        hide();
        g_unit_editor_window->show();
    }
    if (ImGui::Button("Quit", button_size))
    {
        erhe::application::g_view->on_close();
    }
}

} // namespace hextiles
