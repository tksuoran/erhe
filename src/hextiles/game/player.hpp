#pragma once

#include "coordinate.hpp"
#include "types.hpp"
#include "game/game_context.hpp"
#include "game/unit.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hextiles
{

class Map;

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

    void fog_of_war   (Game_context& context);
    void update_units (Game_context& context);
    void update_cities(Game_context& context);

    void update     (Game_context& context);
    void move_unit  (Game_context& context, direction_t direction);
    void select_unit(Game_context& context, int direction);

private:
    void animate_current_unit(Game_context& context);

    std::shared_ptr<Tiles>         tiles;
    size_t                         m_current_unit{0};
    size_t                         m_city_counter{0};

    struct Move
    {
        Tile_coordinate target;
        double          start_time;
    };
    std::optional<Move> m_move;
};


} // namespace hextiles
