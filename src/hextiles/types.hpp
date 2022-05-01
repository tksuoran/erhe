#pragma once

#include <cstdint>

namespace hextiles
{

using pixel_t        = int;
using coordinate_t   = int16_t;
using direction_t    = int;
using terrain_t      = int;
using unit_t         = int;
using terrain_tile_t = uint16_t;
using unit_tile_t    = uint16_t;
//using tile_t         = unsigned int;

//constexpr terrain_t    default_terrain  = 2;
//constexpr unit_t       no_unit          = 0;
//constexpr unit_shape_t no_
constexpr int          max_player_count = 4;

} // namespace hextiles

