#include "game_window.hpp"
#include "map_window.hpp"
#include "menu_window.hpp"

#include "erhe/application/imgui_windows.hpp"
#include "erhe/application/view.hpp"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

namespace hextiles
{

Game_window::Game_window()
    : erhe::components::Component{c_name}
    , Imgui_window               {c_name, c_title}
{
}

Game_window::~Game_window()
{
}

void Game_window::connect()
{
    require<erhe::application::Imgui_windows>();
}

void Game_window::initialize_component()
{
    Imgui_window::initialize(*m_components);
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
}

void Game_window::imgui()
{
    if (ImGui::Button("End Turn"))
    {
    }
    if (ImGui::Button("Back to Menu"))
    {
        hide();
        get<Map_window >()->hide();
        get<Menu_window>()->show();
    }

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
