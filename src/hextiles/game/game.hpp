#pragma once

#include "coordinate.hpp"
#include "types.hpp"
#include "game/player.hpp"

#include "erhe/application/commands/command.hpp"
#include "erhe/components/components.hpp"

#include <string>

namespace erhe::application
{

class Time;

}

namespace hextiles
{

class Game;
class Map;
class Map_window;
class Rendering;
class Tiles;
class Tile_renderer;

class Unit;

class Tile_renderer;
class Tiles;

struct Game_create_parameters
{
    std::shared_ptr<Map>         map;
    std::vector<std::string>     player_names;
    std::vector<size_t>          player_starting_cities;
    std::vector<Tile_coordinate> city_positions;
};

class Move_unit_command final
    : public erhe::application::Command
{
public:
    Move_unit_command(Game& game, direction_t direction)
        : Command    {"move_unit"}
        , m_game     {game}
        , m_direction{direction}
    {
    }
    ~Move_unit_command() noexcept final {}

    auto try_call(erhe::application::Command_context& context) -> bool override;

private:
    Game&       m_game;
    direction_t m_direction;
};

class Select_unit_command final
    : public erhe::application::Command
{
public:
    Select_unit_command(Game& game, int direction)
        : Command    {"select_unit"}
        , m_game     {game}
        , m_direction{direction}
    {
    }
    ~Select_unit_command() noexcept final {}

    auto try_call(erhe::application::Command_context& context) -> bool override;

private:
    Game& m_game;
    int   m_direction;
};

class Game
    : public erhe::components::Component
    , public erhe::components::IUpdate_once_per_frame
{
public:
    static constexpr std::string_view c_type_name{"Game"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Game ();
    ~Game() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements IUpdate_once_per_frame
    void update_once_per_frame(const erhe::components::Time_context&) override;

    // Public API
    [[nodiscard]] auto make_unit         (unit_t unit_type, Tile_coordinate location) -> Unit;
    [[nodiscard]] auto get_current_player() -> Player&;
    [[nodiscard]] auto get_unit_tile     (Tile_coordinate position, const Unit* ignore = nullptr) -> unit_tile_t;
    [[nodiscard]] auto get_context       () -> Game_context;
    void new_game          (const Game_create_parameters& parameters);
    void next_turn         ();

    // Commands
    auto move_unit           (direction_t direction) -> bool;
    auto select_unit         (int direction) -> bool;
    void update_map_unit_tile(Tile_coordinate position);
    void reveal              (Map& target_map, Tile_coordinate position, int radius) const;

private:
    void add_player           (const std::string& name, Tile_coordinate start_city);
    void update_current_player();

    // Component dependencies
    std::shared_ptr<erhe::application::Time> m_time;
    std::shared_ptr<Map_window>              m_map_window;
    std::shared_ptr<Rendering>               m_rendering;
    std::shared_ptr<Tiles>                   m_tiles;
    std::shared_ptr<Tile_renderer>           m_tile_renderer;

    // Commands
    Move_unit_command   m_move_unit_n_command;
    Move_unit_command   m_move_unit_ne_command;
    Move_unit_command   m_move_unit_se_command;
    Move_unit_command   m_move_unit_s_command;
    Move_unit_command   m_move_unit_sw_command;
    Move_unit_command   m_move_unit_nw_command;
    Select_unit_command m_select_previous_unit_command;
    Select_unit_command m_select_next_unit_command;

    std::shared_ptr<Map>         m_map;
    int                          m_turn          {0};
    size_t                       m_current_player{0};
    double                       m_frame_time    {0.0};
    std::vector<Tile_coordinate> m_cities;
    std::vector<Player>          m_players;
};

} // namespace hextiles
