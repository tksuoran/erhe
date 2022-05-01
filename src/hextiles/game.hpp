#pragma once

#include "coordinate.hpp"
#include "types.hpp"

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
class Rendering;
class Tiles;
class Tile_renderer;

struct Game_context
{
    Game&          game;
    Rendering&     rendering;
    Tiles&         tiles;
    Tile_renderer& tile_renderer;
};

class Unit
{
public:
    Tile_coordinate location           {0, 0};
    unit_t          type               {0};
    int             hit_points         {0};
    int             move_points        {0};
    int             fuel               {0};
    bool            ready_to_load      {false};
    int             level              {0};
    std::string     name;                   // city only
    unit_t          production         {0}; // city only
    int             production_progress{0}; // city only
};

class Player
{
public:
    void city_imgui(Game_context& context);
    void unit_imgui(Game_context& context);
    void imgui     (Game_context& context);

    int                  id;
    std::shared_ptr<Map> map;
    std::string          name;
    std::vector<Unit>    cities;
    std::vector<Unit>    units;

    void progress_production(Game_context& context);

private:
    void next_unit    ();
    void previous_unit();

    std::shared_ptr<Tiles> tiles;
    size_t                 m_current_unit{0};
    size_t                 m_city_counter{0};
};

class Tile_renderer;
class Tiles;

struct Game_create_parameters
{
    std::shared_ptr<Map>         map;
    std::vector<std::string>     player_names;
    std::vector<size_t>          player_starting_cities;
    std::vector<Tile_coordinate> city_positions;
};

class Game
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_label{"Game"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_label.data(), c_label.size(), {});

    Game ();
    ~Game() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Public API
    auto make_unit         (unit_t unit_type, Tile_coordinate location) -> Unit;
    void new_game          (const Game_create_parameters& parameters);
    void next_turn         ();
    auto get_current_player() -> Player&;
    auto get_unit_tile     (Tile_coordinate position) -> unit_tile_t;

private:
    void add_player           (const std::string& name, Tile_coordinate start_city);
    void update_current_player();
    void reveal               (Map& target_map, Tile_coordinate position, int radius) const;

    std::shared_ptr<erhe::application::Time> m_time;
    std::shared_ptr<Rendering>               m_rendering;
    std::shared_ptr<Tiles>                   m_tiles;
    std::shared_ptr<Tile_renderer>           m_tile_renderer;

    std::shared_ptr<Map>         m_map;
    int                          m_turn          {0};
    size_t                       m_current_player{0};
    std::vector<Tile_coordinate> m_cities;
    std::vector<Player>          m_players;
};

} // namespace hextiles
