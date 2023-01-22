#pragma once

#include "coordinate.hpp"
#include "types.hpp"
#include "game/player.hpp"

#include "erhe/application/commands/command.hpp"
#include "erhe/components/components.hpp"

#include "etl/string.h"
#include "etl/vector.h"
//#include <string>

namespace hextiles
{

class Map;
class Unit;

struct Game_create_parameters
{
    Map* map{nullptr};
    etl::vector<
        etl::string<max_name_length>,
        max_player_count
    >                                              player_names;
    etl::vector<size_t,          max_player_count> player_starting_cities;
    etl::vector<Tile_coordinate, max_city_count>   city_positions;
};

class Move_unit_command final
    : public erhe::application::Command
{
public:
    Move_unit_command(direction_t direction)
        : Command    {"move_unit"}
        , m_direction{direction}
    {
    }
    ~Move_unit_command() noexcept final {}

    auto try_call(erhe::application::Command_context& context) -> bool override;

private:
    direction_t m_direction;
};

class Select_unit_command final
    : public erhe::application::Command
{
public:
    Select_unit_command(int direction)
        : Command    {"select_unit"}
        , m_direction{direction}
    {
    }
    ~Select_unit_command() noexcept final {}

    auto try_call(erhe::application::Command_context& context) -> bool override;

private:
    int m_direction;
};

class Game
    : public erhe::components::Component
{
public:
    static constexpr const char* c_type_name{"Game"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name, compiletime_strlen(c_type_name), {});

    Game();
    ~Game();

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;

    // Implements IUpdate_once_per_frame
    //void update_once_per_frame(const erhe::components::Time_context&) override;

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
    void add_player           (const etl::string<max_name_length>& name, Tile_coordinate start_city);
    void update_current_player();

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

extern Game* g_game;

} // namespace hextiles
