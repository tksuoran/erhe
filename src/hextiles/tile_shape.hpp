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
    static constexpr int count  = 7;
    static constexpr int width  = 8;
    static constexpr int height = 8;
};

class Base_tiles
{
public:
    static constexpr int width  =  5;
    static constexpr int height = 11;
    static constexpr int count  = width * height;
};

class Explosion_tiles
{
public:
    static constexpr int width  = 5;
    static constexpr int height = 2;
    static constexpr int count  = width * height;
};

class Unit_group
{
public:
    static constexpr int width  =  7;
    static constexpr int height = 10;
};

} // namespace hextiles

