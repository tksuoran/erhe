#pragma once

#include <cstdint>

namespace hextiles
{

constexpr int Input_context_type_map = 0;

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
constexpr int max_name_length  =  32;
constexpr int max_city_count   = 100;
constexpr int max_unit_count   = 200;
constexpr int max_player_count = 4;
constexpr int max_biome_count  = 32;

} // namespace hextiles

