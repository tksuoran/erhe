#include "map.hpp"

#include "hextiles.hpp"
#include "tiles.hpp"

namespace hextiles {

auto Map::width() const -> int
{
    return m_width;
}

auto Map::height() const -> int
{
    return m_height;
}

void Map::reset(const int width, const int height)
{
    ERHE_VERIFY(width > 0);
    ERHE_VERIFY(height > 0);
    m_width  = std::min(std::numeric_limits<uint16_t>::max(), static_cast<uint16_t>(width));
    m_height = std::min(std::numeric_limits<uint16_t>::max(), static_cast<uint16_t>(height));
    m_map.resize(static_cast<size_t>(m_width) * static_cast<size_t>(m_height));
    //m_map.shrink_to_fit();
    std::fill(
        std::begin(m_map),
        std::end(m_map),
        Map_cell{}
    );
}

void Map::read(File_read_stream& stream)
{
    stream.op(m_width);
    stream.op(m_height);
    ERHE_VERIFY(m_width > 0);
    ERHE_VERIFY(m_height > 0);
    m_map.resize(static_cast<size_t>(m_width) * static_cast<size_t>(m_height));
    //m_map.shrink_to_fit();
    for (auto& cell : m_map) {
        stream.op(cell.terrain_tile);
        stream.op(cell.unit_tile);
        /// XXX TODO FIXME
        if (cell.terrain_tile > 55) {
            ++cell.terrain_tile;
        }
    }
}

void Map::write(File_write_stream& stream)
{
    stream.op(m_width);
    stream.op(m_height);
    for (auto& cell : m_map) {
        stream.op(cell.terrain_tile);
        stream.op(cell.unit_tile);
    }
}

auto Map::get_terrain_tile(Tile_coordinate tile_coordinate) const -> terrain_tile_t
{
    ERHE_VERIFY(tile_coordinate.x >= coordinate_t{0});
    ERHE_VERIFY(tile_coordinate.y >= coordinate_t{0});
    ERHE_VERIFY(tile_coordinate.x < m_width);
    ERHE_VERIFY(tile_coordinate.y < m_height);
    const size_t index =
        static_cast<size_t>(tile_coordinate.x) +
        static_cast<size_t>(tile_coordinate.y) * static_cast<size_t>(m_width);
    return m_map[index].terrain_tile;
}

void Map::set_terrain_tile(Tile_coordinate tile_coordinate, terrain_tile_t terrain_tile)
{
    ERHE_VERIFY(tile_coordinate.x >= coordinate_t{0});
    ERHE_VERIFY(tile_coordinate.y >= coordinate_t{0});
    ERHE_VERIFY(tile_coordinate.x < m_width);
    ERHE_VERIFY(tile_coordinate.y < m_height);
    const size_t index =
        static_cast<size_t>(tile_coordinate.x) +
        static_cast<size_t>(tile_coordinate.y) * static_cast<size_t>(m_width);
    m_map[index].terrain_tile = terrain_tile;
}

auto Map::get_unit_tile(Tile_coordinate tile_coordinate) const -> unit_tile_t
{
    ERHE_VERIFY(tile_coordinate.x >= coordinate_t{0});
    ERHE_VERIFY(tile_coordinate.y >= coordinate_t{0});
    ERHE_VERIFY(tile_coordinate.x < m_width);
    ERHE_VERIFY(tile_coordinate.y < m_height);
    const size_t index =
        static_cast<size_t>(tile_coordinate.x) +
        static_cast<size_t>(tile_coordinate.y) * static_cast<size_t>(m_width);
    return m_map[index].unit_tile;
}

void Map::set_unit_tile(Tile_coordinate tile_coordinate, unit_tile_t unit_tile)
{
    ERHE_VERIFY(tile_coordinate.x >= coordinate_t{0});
    ERHE_VERIFY(tile_coordinate.y >= coordinate_t{0});
    ERHE_VERIFY(tile_coordinate.x < m_width);
    ERHE_VERIFY(tile_coordinate.y < m_height);
    const size_t index =
        static_cast<size_t>(tile_coordinate.x) +
        static_cast<size_t>(tile_coordinate.y) * static_cast<size_t>(m_width);
    m_map[index].unit_tile = unit_tile;
}

void Map::set(Tile_coordinate tile_coordinate, terrain_tile_t terrain_tile, unit_tile_t unit_tile)
{
    ERHE_VERIFY(tile_coordinate.x >= coordinate_t{0});
    ERHE_VERIFY(tile_coordinate.y >= coordinate_t{0});
    ERHE_VERIFY(tile_coordinate.x < m_width);
    ERHE_VERIFY(tile_coordinate.y < m_height);
    const size_t index =
        static_cast<size_t>(tile_coordinate.x) +
        static_cast<size_t>(tile_coordinate.y) * static_cast<size_t>(m_width);
    auto& map_cell = m_map[index];
    map_cell.terrain_tile = terrain_tile;
    map_cell.unit_tile    = unit_tile;
}

auto Map::wrap(Tile_coordinate in) const -> Tile_coordinate
{
    Tile_coordinate ret = in;
    while (ret.x >= m_width) {
        ret.x -= static_cast<coordinate_t>(m_width);
    }
    while (ret.x < coordinate_t{0}) {
        ret.x += static_cast<coordinate_t>(m_width);
    }
    while (ret.y >= m_height) {
        ret.y -= static_cast<coordinate_t>(m_height);
    }
    while (ret.y < coordinate_t{0}) {
        ret.y += static_cast<coordinate_t>(m_height);
    }
    ERHE_VERIFY(ret.x >= coordinate_t{0});
    ERHE_VERIFY(ret.y >= coordinate_t{0});
    ERHE_VERIFY(ret.x < m_width);
    ERHE_VERIFY(ret.y < m_height);

    return ret;
}

auto Map::wrap_center(Tile_coordinate in) const -> Tile_coordinate
{
    Tile_coordinate ret = in;
    while (ret.x >= m_width / 2) {
        ret.x -= static_cast<coordinate_t>(m_width);
    }
    while (ret.x < -m_width / 2) {
        ret.x += static_cast<coordinate_t>(m_width);
    }
    while (ret.y >= m_height / 2) {
        ret.y -= static_cast<coordinate_t>(m_height);
    }
    while (ret.y < -m_height / 2) {
        ret.y += static_cast<coordinate_t>(m_height);
    }
    return ret;
}

auto Map::neighbor(
    Tile_coordinate position,
    direction_t     direction
) const -> Tile_coordinate
{
    return wrap(position.neighbor(direction));
}

void Map::for_each_tile(const std::function<void(Tile_coordinate position)>& op)
{
    for (coordinate_t ty = 0; ty < m_height; ++ty) {
        for (coordinate_t tx = 0; tx < m_width; ++tx) {
            const Tile_coordinate tile_position{tx, ty};
            op(tile_position);
        }
    }
}

void Map::hex_circle(
    const Tile_coordinate                                      center_position,
    int                                                        r0,
    const int                                                  r1,
    const std::function<void(const Tile_coordinate position)>& op
)
{
    ERHE_VERIFY(r0 >= 0);
    ERHE_VERIFY(r1 >= r0);

    if (r0 == 0) {
        op(center_position);
        r0 = 1;
    }

    constexpr direction_t offset{2};

    for (int radius = r0; radius <= r1; ++radius) {
        auto position = center_position;
        for (int i = 0; i < radius; ++i) {
            position = wrap(position.neighbor(direction_north));
        }
        for (auto direction = direction_first; direction < direction_count; ++direction) {
            for (int i = 0; i < radius; ++i) {
                position = wrap(position.neighbor((direction + offset) % direction_count));
                op(position);
            }
        }
    }
}

auto Map::distance(
    const Tile_coordinate& lhs,
    const Tile_coordinate& rhs
) -> int
{
    const auto d = wrap_center(lhs - rhs);

    int yl = lhs.is_odd() ? 2 : 0;

    int dy = d.is_odd()
        ? std::abs(2 * d.y - 1 + yl) / 2
        : std::abs(2 * d.y) / 2;

    int dx = std::abs(d.x);

    return dx > dy * 2
        ? dx
        : dy + dx / 2;
}

} // namespace hextiles
