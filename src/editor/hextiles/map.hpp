#pragma once

#include "hextiles/coordinate.hpp"
#include "hextiles/stream.hpp"
#include "hextiles/types.hpp"

#include <gsl/span>

#include <functional>
#include <vector>

namespace hextiles
{

class Map_cell
{
public:
    terrain_t terrain  {0u};
    unit_t    unit_icon{0u};
};

class Map
{
public:
    Map();

    void reset        (int width, int height);
    void read         (File_read_stream& stream);
    void write        (File_write_stream& stream);
    auto get_terrain  (Tile_coordinate tile_coordinate) const -> terrain_t;
    void set_terrain  (Tile_coordinate tile_coordinate, terrain_t terrain_value);
    auto get_unit_icon(Tile_coordinate tile_coordinate) const -> unit_t;
    void set_unit_icon(Tile_coordinate tile_coordinate, unit_t unit_icon_value);
    auto wrap         (Tile_coordinate in) const -> Tile_coordinate;
    auto wrap_center  (Tile_coordinate in) const -> Tile_coordinate;
    auto neighbor     (Tile_coordinate position, direction_t direction) const -> Tile_coordinate;
    void hex_circle(
        Tile_coordinate                                      center_position,
        int                                                  r0,
        int                                                  r1,
        const std::function<void(Tile_coordinate position)>& op
    );
    auto width () const -> int;
    auto height() const -> int;

private:
    uint16_t              m_width {0u};
    uint16_t              m_height{0u};
    std::vector<Map_cell> m_map;
};

}