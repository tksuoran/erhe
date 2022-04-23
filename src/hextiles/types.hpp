#pragma once

#include <cstdint>

namespace hextiles
{

using pixel_t      = int;
using coordinate_t = int16_t;
using direction_t  = int;
using terrain_t    = uint16_t;
using unit_t       = uint16_t;

constexpr terrain_t default_terrain = 2u;
constexpr unit_t    no_unit         = 0u;

} // namespace hextiles

