#pragma once

#include "coordinate.hpp"
#include "types.hpp"

#include <string>

namespace hextiles {

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
    bool            active             {false};
};

} // namespace hextiles
