#pragma once

#include "coordinate.hpp"
#include "stream.hpp"
#include "types.hpp"

#include "etl/vector.h"

namespace hextiles {

class Tiles;

class Map_cell
{
public:
    terrain_tile_t terrain_tile{0u};
    unit_tile_t    unit_tile   {0u};
};

class Map
{
public:
    void reset           (int width, int height);
    void read            (File_read_stream& stream);
    void write           (File_write_stream& stream);
    auto get_terrain_tile(Tile_coordinate tile_coordinate) const -> terrain_tile_t;
    void set_terrain_tile(Tile_coordinate tile_coordinate, terrain_tile_t terrain_tile);
    auto get_unit_tile   (Tile_coordinate tile_coordinate) const -> unit_tile_t;
    void set_unit_tile   (Tile_coordinate tile_coordinate, unit_tile_t unit_tile);
    void set             (Tile_coordinate tile_coordinate, terrain_tile_t terrain_tile, unit_tile_t unit_tile);
    auto wrap            (Tile_coordinate in) const -> Tile_coordinate;
    auto wrap_center     (Tile_coordinate in) const -> Tile_coordinate;
    auto neighbor        (Tile_coordinate position, direction_t direction) const -> Tile_coordinate;
    void for_each_tile   (const std::function<void(Tile_coordinate position)>& op);
    void hex_circle(
        Tile_coordinate                                      center_position,
        int                                                  r0,
        int                                                  r1,
        const std::function<void(Tile_coordinate position)>& op
    );
    auto width               () const -> int;
    auto height              () const -> int;
    auto distance            (const Tile_coordinate& lhs, const Tile_coordinate& rhs) -> int;

private:
    uint16_t                         m_width {0u};
    uint16_t                         m_height{0u};
    etl::vector<Map_cell, 160 * 160> m_map; // ~100 kB
};

} // namespace hextiles
