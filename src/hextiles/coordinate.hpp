#pragma once

#include "types.hpp"

#include <cassert>

namespace hextiles
{

struct Coordinate_modifier
{
    coordinate_t x = 0;
    coordinate_t y = 0;
    coordinate_t l = 0; // additional y modifier in case x is odd
};

constexpr Coordinate_modifier hex_neighbors[] = {
    {  0, -1,  0 }, // north
    {  1,  0, -1 }, // north-east
    {  1,  1, -1 }, // south-east
    {  0,  1,  0 }, // south
    { -1,  1, -1 }, // south-west
    { -1,  0, -1 }  // north-west
};

constexpr direction_t direction_first     {0};
constexpr direction_t direction_north     {0};
constexpr direction_t direction_north_east{1};
constexpr direction_t direction_south_east{2};
constexpr direction_t direction_south     {3};
constexpr direction_t direction_south_west{4};
constexpr direction_t direction_north_west{5};
constexpr direction_t direction_last      {5};
constexpr direction_t direction_count     {6};

constexpr unsigned int direction_mask_all{0b00111111u};

class Tile_coordinate
{
public:
    Tile_coordinate()
        : x{0}
        , y{0}
    {
    }

    Tile_coordinate(
        const coordinate_t x,
        const coordinate_t y
    )
        : x{x}
        , y{y}
    {
    }

    auto is_even() const -> bool
    {
        return (x & 1) == 0;
    }

    auto is_odd() const -> bool
    {
        return !is_even();
    }

    auto operator*(const int scale) const -> Tile_coordinate
    {
        return Tile_coordinate{
            static_cast<coordinate_t>(x * scale),
            static_cast<coordinate_t>(y * scale)
        };
    }

    auto operator/(const int scale) const -> Tile_coordinate
    {
        return Tile_coordinate{
            static_cast<coordinate_t>(x / scale),
            static_cast<coordinate_t>(y / scale)
        };
    }

    auto operator-(const Tile_coordinate& other) const -> Tile_coordinate
    {
        return Tile_coordinate{
            static_cast<coordinate_t>(x - other.x),
            static_cast<coordinate_t>(y - other.y)
        };
    }

    auto operator+(const Tile_coordinate& other) const -> Tile_coordinate
    {
        return Tile_coordinate{
            static_cast<coordinate_t>(x + other.x),
            static_cast<coordinate_t>(y + other.y)
        };
    }

    auto neighbor(const direction_t direction) const -> Tile_coordinate
    {
        assert(direction >= direction_first);
        assert(direction <= direction_last);

        Tile_coordinate res{
            static_cast<coordinate_t>(this->x + hex_neighbors[direction].x),
            static_cast<coordinate_t>(this->y + hex_neighbors[direction].y)
        };
        if (res.is_even() && (hex_neighbors[direction].l == -1)) {
            res.y -= 1;
        }
        return res;
    }

    coordinate_t x;
    coordinate_t y;
};

inline auto operator==(const Tile_coordinate& lhs, const Tile_coordinate& rhs) -> bool
{
    return (lhs.x == rhs.x) && (lhs.y == rhs.y);
}

inline auto operator!=(const Tile_coordinate& lhs, const Tile_coordinate& rhs) -> bool
{
    return !(lhs == rhs);
}

inline auto operator<(const Tile_coordinate& lhs, const Tile_coordinate& rhs) -> bool
{
    if (lhs.x != rhs.x) {
        return lhs.x < rhs.x;
    }
    return lhs.y < rhs.y;
}

inline auto operator>(const Tile_coordinate& lhs, const Tile_coordinate& rhs) -> bool
{
    if (lhs.x != rhs.x) {
        return lhs.x > rhs.x;
    }
    return lhs.y > rhs.y;
}

inline bool is_odd(const coordinate_t x)
{
    return x % 2 != 0;
}

class Pixel_coordinate
{
public:
    Pixel_coordinate()
        : x{0}
        , y{0}
    {
    }

    Pixel_coordinate(const pixel_t x, const pixel_t y)
        : x{x}
        , y{y}
    {
    }

    auto operator*(const int scale) const -> Pixel_coordinate
    {
        return Pixel_coordinate{
            x * static_cast<pixel_t>(scale),
            y * static_cast<pixel_t>(scale)
        };
    }

    auto operator/(const int scale) const -> Pixel_coordinate
    {
        return Pixel_coordinate{
            x / static_cast<pixel_t>(scale),
            y / static_cast<pixel_t>(scale)
        };
    }

    auto operator-(const Pixel_coordinate& other) const -> Pixel_coordinate
    {
        return Pixel_coordinate{x - other.x, y - other.y};
    }

    auto operator+(const Pixel_coordinate& other) const -> Pixel_coordinate
    {
        return Pixel_coordinate{x + other.x, y + other.y};
    }

    pixel_t x;
    pixel_t y;
};

} // namespace hextiles

