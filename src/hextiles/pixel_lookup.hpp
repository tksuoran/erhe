#pragma once

#include "coordinate.hpp"
#include "tile_shape.hpp"

#include <array>

namespace hextiles {

class Pixel_lookup
{
public:
    Pixel_lookup();

    auto lut_xy       (Pixel_coordinate pixel_coordinate) const -> Pixel_coordinate;
    auto lut_value    (Pixel_coordinate pixel_coordinate) const -> Tile_coordinate;
    auto pixel_to_tile(Pixel_coordinate pixel_coordinate) const -> Tile_coordinate;

private:
    static const int lut_width  = Tile_shape::interleave_width * 2;
    static const int lut_height = Tile_shape::height;
    static const int lut_size   = lut_width * lut_height;

    using lut_t = std::array<Tile_coordinate, lut_size>;

    lut_t m_lut;
};

} // namespace hextiles
