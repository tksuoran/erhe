#pragma once

#include "hextiles.hpp"
#include "coordinate.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_imgui/imgui_window.hpp"

#include "etl/vector.h"

#include <string>

namespace erhe::imgui {
    class Imgui_windows;
}

namespace hextiles {

class Game;
class Map_editor;
class Map_window;
class Menu_window;
class Tile_renderer;
class Tiles;

class New_game_window : public erhe::imgui::Imgui_window
{
public:
    New_game_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Game&                        game,
        Map_editor&                  map_editor,
        Map_window&                  map_window,
        Menu_window&                 menu_window,
        Tile_renderer&               tile_renderer,
        Tiles&                       tiles
    );

    // Implements Imgui_window
    void imgui() override;

private:
    auto is_too_close_to_city      (Tile_coordinate location) const -> bool;
    auto try_create_city           (uint32_t flags_match) -> bool;
    void place_cities              ();
    void select_player_start_cities();
    void create                    ();

    Game&          m_game;
    Map_editor&    m_map_editor;
    Map_window&    m_map_window;
    Menu_window&   m_menu_window;
    Tile_renderer& m_tile_renderer;
    Tiles&         m_tiles;

    etl::vector<std::string, max_player_count> m_player_names;
    etl::vector<size_t, max_city_count>        m_start_cities;

    struct Create_parameters {
        int  number_of_coastal_cities {10};
        int  number_of_land_cities    {10};
        int  minimum_city_distance    {10};
        int  number_of_starting_cities{1};
        int  protection_turn_count    {2};
        int  starting_technology_level{1};
        bool reveal_map{false};
    };

    Create_parameters                            m_create_parameters;
    etl::vector<Tile_coordinate, max_city_count> m_cities;
    int                                          m_number_of_coastal_cities{0};
    int                                          m_number_of_land_cities   {0};

    etl::vector<size_t, max_player_count> m_player_start_cities;
    int                                   m_minimum_player_start_city_distance{10};
};

} // namespace hextiles
