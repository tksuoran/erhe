#include "game/game_window.hpp"
#include "map_window.hpp"
#include "menu_window.hpp"
#include "rendering.hpp"
#include "tiles.hpp"
#include "tile_renderer.hpp"
#include "game/game.hpp"

#include "erhe/application/imgui_windows.hpp"
#include "erhe/application/view.hpp"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

namespace hextiles
{

Game_window::Game_window()
    : erhe::components::Component{c_label}
    , Imgui_window               {c_title, c_label}
{
}

Game_window::~Game_window()
{
}

void Game_window::connect()
{
    m_game          = get<Game         >();
    m_rendering     = get<Rendering    >();
    m_tiles         = get<Tiles        >();
    m_tile_renderer = get<Tile_renderer>();
    require<erhe::application::Imgui_windows>();
}

void Game_window::initialize_component()
{
    Imgui_window::initialize(*m_components);
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
}

void Game_window::imgui()
{
    constexpr ImVec2 button_size{110.0f, 0.0f};

    auto game = get<Game>();
    Player& player = game->get_current_player();
    ImGui::Text("Player: %s", player.name.c_str());
    if (ImGui::Button("End Turn", button_size))
    {
        game->next_turn();
    }
    if (ImGui::Button("Back to Menu", button_size))
    {
        hide();
        get<Map_window >()->hide();
        get<Menu_window>()->show();
    }

    Game_context context = m_game->get_context();
    player.imgui(context);

    // Blink (highlight) currently selected unit
    // Advance production for all cities
    // Select city production next/prev unit type
    // Select next unit
    // Set unit on hold
    // Move selected unit
    //      attack unit
    //      capture city
    //      rename city
    // Build - Fortify
    // Build - Road
    // Build - Bridge
    // Build - Plain
    // Build - Field
    // Kill unit (suicide)
    // Set/unset unit ready for loading
    // Ranged attack
    // Cycle grid modes
    // Toggle battletype (surface/submerged)

    // unit statistics window
    // unit production window
}

} // namespace hextiles
