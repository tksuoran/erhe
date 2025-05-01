#pragma once

#include "coordinate.hpp"
#include "types.hpp"
#include "map.hpp"
#include "game/unit.hpp"

#include "etl/vector.h"

#include <memory>
#include <optional>
#include <string>

namespace hextiles {

class Player
{
public:
    //void city_imgui();
    //void unit_imgui();
    //void imgui     ();

    int                               id{0};
    Map                               map;
    std::string                       name;
    etl::vector<Unit, max_city_count> cities;
    etl::vector<Unit, max_unit_count> units;
    size_t                            current_unit{0};

    class Move
    {
    public:
        Tile_coordinate target;
        double          start_time{0.0};
    };

    size_t              city_counter{0};
    std::optional<Move> move;
};


} // namespace hextiles
