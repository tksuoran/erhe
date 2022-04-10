#pragma once

#include "coordinate.hpp"
#include "terrain_type.hpp"
#include "types.hpp"

namespace power_battle
{

class Game;

class Map_editor
{
public:
    void mouse(
        bool             left_mouse,
        bool             right_mouse,
        Pixel_coordinate position
    );

    Game*     m_game     {nullptr};
    terrain_t left_brush {Terrain_plain};
    terrain_t right_brush{Terrain_water_low};
    terrain_t last_brush {Terrain_plain};
    terrain_t brush_size {1u};
    int       offset     {0};
};

}
