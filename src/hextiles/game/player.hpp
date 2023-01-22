#pragma once

#include "coordinate.hpp"
#include "types.hpp"
#include "map.hpp"
#include "game/game_context.hpp"
#include "game/unit.hpp"

#include <memory>
#include <optional>
#include "etl/string.h"
#include "etl/vector.h"

namespace hextiles
{

class Player
{
public:
    void city_imgui();
    void unit_imgui();
    void imgui     ();

    int                               id;
    Map                               map;
    etl::string<max_name_length>      name;
    etl::vector<Unit, max_city_count> cities;
    etl::vector<Unit, max_unit_count> units;

    void fog_of_war   ();
    void update_units ();
    void update_cities();

    void update     ();
    void move_unit  (direction_t direction);
    void select_unit(int direction);

private:
    void animate_current_unit();

    size_t m_current_unit{0};
    size_t m_city_counter{0};

    struct Move
    {
        Tile_coordinate target;
        double          start_time;
    };
    std::optional<Move> m_move;
};


} // namespace hextiles
