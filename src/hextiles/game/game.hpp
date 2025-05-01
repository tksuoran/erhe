#pragma once

#include "coordinate.hpp"
#include "types.hpp"
#include "game/player.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_imgui/imgui_window.hpp"

#include "etl/vector.h"

#include <string>

namespace erhe::imgui {
    class Imgui_windows;
}

namespace hextiles {

class Game;
class Map;
class Map_window;
class Menu_window;
class Tile_renderer;
class Tiles;
class Unit;

struct Game_create_parameters
{
    Map* map{nullptr};
    etl::vector<std::string, max_player_count>     player_names;
    etl::vector<size_t,          max_player_count> player_starting_cities;
    etl::vector<Tile_coordinate, max_city_count>   city_positions;
};

class Move_unit_command final : public erhe::commands::Command
{
public:
    Move_unit_command(
        erhe::commands::Commands& commands,
        Game&                     game,
        const direction_t         direction
    )
        : Command    {commands, "move_unit"}
        , m_game     {game}
        , m_direction{direction}
    {
    }
    ~Move_unit_command() noexcept final {}

    auto try_call() -> bool override;

private:
    Game&       m_game;
    direction_t m_direction;
};

class Select_unit_command final : public erhe::commands::Command
{
public:
    Select_unit_command(
        erhe::commands::Commands& commands,
        Game&                     game,
        const int                 direction
    )
        : Command    {commands, "select_unit"}
        , m_game     {game}
        , m_direction{direction}
    {
    }
    ~Select_unit_command() noexcept final {}

    auto try_call() -> bool override;

private:
    Game& m_game;
    int   m_direction;
};

class Game : public erhe::imgui::Imgui_window
{
public:
    Game(
        erhe::commands::Commands&    commands,
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Map_window&                  map_window,
        Menu_window&                 menu_window,
        Tile_renderer&               tile_renderer,
        Tiles&                       tiles
    );

    void imgui                  () override;
    void player_imgui           ();
    void city_imgui             ();
    void unit_imgui             ();
    void animate_current_unit   ();
    void update_player          ();
    void move_player_unit       (direction_t direction);
    void select_player_unit     (int direction);
    void apply_player_fog_of_war();
    void update_player_units    ();
    void update_player_cities   ();

    auto flags() -> ImGuiWindowFlags override;

    // Implements IUpdate_once_per_frame
    //void update_once_per_frame(const Time_context&) override;

    // Public API
    [[nodiscard]] auto make_unit         (unit_t unit_type, Tile_coordinate location) -> Unit;
    [[nodiscard]] auto get_current_player() -> Player&;
    [[nodiscard]] auto get_unit_tile     (Tile_coordinate position, const Unit* ignore = nullptr) -> unit_tile_t;
    [[nodiscard]] auto get_time_now      () const -> float;
    void new_game (const Game_create_parameters& parameters);
    void next_turn();

    // Commands
    auto move_unit           (direction_t direction) -> bool;
    auto select_unit         (int direction) -> bool;
    void update_map_unit_tile(Tile_coordinate position);
    void reveal              (Map& target_map, Tile_coordinate position, int radius) const;

private:
    void add_player           (const std::string& name, Tile_coordinate start_city);
    void update_current_player();

    Map_window&    m_map_window;
    Menu_window&   m_menu_window;
    Tile_renderer& m_tile_renderer;
    Tiles&         m_tiles;

    // Commands
    Move_unit_command   m_move_unit_n_command;
    Move_unit_command   m_move_unit_ne_command;
    Move_unit_command   m_move_unit_se_command;
    Move_unit_command   m_move_unit_s_command;
    Move_unit_command   m_move_unit_sw_command;
    Move_unit_command   m_move_unit_nw_command;
    Select_unit_command m_select_previous_unit_command;
    Select_unit_command m_select_next_unit_command;

    Map*                                         m_map           {nullptr};
    int                                          m_turn          {0};
    size_t                                       m_current_player{0};
    double                                       m_frame_time    {0.0};
    etl::vector<Tile_coordinate, max_city_count> m_cities;
    etl::vector<Player, max_player_count>        m_players;
};

} // namespace hextiles
