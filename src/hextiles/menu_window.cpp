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

#include "erhe/application/imgui_windows.hpp"
#include "erhe/application/view.hpp"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

namespace hextiles
{

Menu_window::Menu_window()
    : erhe::components::Component{c_label}
    , Imgui_window               {c_title, c_label}
{
}

Menu_window::~Menu_window() noexcept
{
}

void Menu_window::declare_required_components()
{
    require<erhe::application::Imgui_windows>();
}

void Menu_window::initialize_component()
{
    Imgui_window::initialize(*m_components);
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
}

void Menu_window::show_menu()
{
    get<Game_window                           >()->hide();
    get<Map_generator                         >()->hide();
    get<Map_tool_window                       >()->hide();
    get<Map_window                            >()->hide();
    get<New_game_window                       >()->hide();
    get<Terrain_editor_window                 >()->hide();
    get<Terrain_group_editor_window           >()->hide();
    get<Terrain_palette_window                >()->hide();
    get<Terrain_replacement_rule_editor_window>()->hide();
    get<Unit_editor_window                    >()->hide();
    show();
}

void Menu_window::imgui()
{
    constexpr ImVec2 button_size{130.0f, 0.0f};

    if (ImGui::Button("New Game", button_size))
    {
        hide();
        get<New_game_window>()->show();
    }
    //if (ImGui::Button("Load Game"))
    //{
    //    hide();
    //    get<Map_window>()->show();
    //}
    if (ImGui::Button("Map Editor", button_size))
    {
        hide();
        get<Map_window>()->set_map(
            get<Map_editor>()->get_map()
        );
        //get<Map_editor>()->get_map();
        get<Map_window            >()->show();
        get<Terrain_palette_window>()->show();
        get<Map_tool_window       >()->show();
    }
    if (ImGui::Button("Terrain Editor", button_size))
    {
        hide();
        get<Terrain_editor_window                 >()->show();
        get<Terrain_group_editor_window           >()->show();
        get<Terrain_replacement_rule_editor_window>()->show();
    }
    if (ImGui::Button("Unit Editor", button_size))
    {
        hide();
        get<Unit_editor_window>()->show();
    }
    if (ImGui::Button("Quit", button_size))
    {
        get<erhe::application::View>()->on_close();
    }
}

} // namespace hextiles
