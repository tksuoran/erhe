#pragma once

#include "types.hpp"

namespace hextiles
{

class Tile_shape
{
public:
    static constexpr pixel_t full_width       = 14;
    static constexpr pixel_t height           = 12;
    static constexpr pixel_t interleave_width = 11;
    static constexpr pixel_t odd_y_offset     = -6; // tile_height / 2
    static constexpr int     full_size        = (int)full_width * (int)height;
    static constexpr pixel_t center_x         = full_width / 2;
    static constexpr pixel_t center_y         = height / 2;
};

class Tile_group
{
public:
    static constexpr int width  = 8;
    static constexpr int height = 8;
    static constexpr int count  = 7;
};

class Base_tiles
{
public:
    static constexpr int width  = 8; // 5 * 11 = 55
    static constexpr int height = 7; // 8 *  7 = 56
    static constexpr int count  = width * height;
};

class Extra_tiles
{
public:
    static constexpr int width  = 8;
    static constexpr int height = 1;

    static constexpr int grid_offset       = 0;
    static constexpr int brush_size_offset = 2;
};

class Explosion_tiles
{
public:
    static constexpr int width  = 4;
    static constexpr int height = 2;
    static constexpr int count  = width * height;
};

class Special_unit_tiles
{
public:
    static constexpr unit_tile_t player_palette              = 0;
    static constexpr unit_tile_t half_fog_of_war             = 1;
    static constexpr unit_tile_t fog_of_war                  = 2;
    static constexpr unit_tile_t half_fog_of_war_dither_even = 3;
    static constexpr unit_tile_t half_fog_of_war_dither_odd  = 4;
    static constexpr unit_tile_t dot_marker                  = 5;
    static constexpr unit_tile_t corner_marker               = 6;
};

class Unit_group
{
public:
    static constexpr int width  = 8;
    static constexpr int height = 7;
};

} // namespace hextiles

