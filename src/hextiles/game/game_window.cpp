#include "game/game_window.hpp"
#include "map_window.hpp"
#include "menu_window.hpp"
#include "rendering.hpp"
#include "tiles.hpp"
#include "tile_renderer.hpp"
#include "game/game.hpp"

#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/application_view.hpp"
#include "erhe/toolkit/verify.hpp"

#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

namespace hextiles
{

Game_window* g_game_window{nullptr};

Game_window::Game_window()
    : Imgui_window{c_title}
    , Component{c_type_name}
{
}

Game_window::~Game_window() noexcept
{
    ERHE_VERIFY(g_game_window == this);
    g_game_window = nullptr;
}

/// void Game_window::declare_required_components()
/// {
///     require<erhe::application::Imgui_windows>();
/// }

void Game_window::initialize_component()
{
    ERHE_VERIFY(g_game_window == nullptr);
    erhe::application::g_imgui_windows->register_imgui_window(this, "game");
    hide();
    g_game_window = this;
}

auto Game_window::flags() -> ImGuiWindowFlags
{
    return ImGuiWindowFlags_NoNavInputs;
}

void Game_window::imgui()
{
    constexpr ImVec2 button_size{110.0f, 0.0f};

    Player& player = g_game->get_current_player();
    ImGui::Text("Player: %s", player.name.c_str());
    if (ImGui::Button("End Turn", button_size)) {
        g_game->next_turn();
    }
    if (ImGui::Button("Back to Menu", button_size)) {
        hide();
        g_map_window ->hide();
        g_menu_window->show();
    }

    player.imgui();

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
