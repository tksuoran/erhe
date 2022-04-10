#include "hextiles/map.hpp"

#include <gsl/assert>

namespace hextiles
{

Map::Map()
{
}

auto Map::width() const -> int
{
    return m_width;
}

auto Map::height() const -> int
{
    return m_height;
}

void Map::reset(
    int width,
    int height
)
{
    m_width  = std::min(std::numeric_limits<uint16_t>::max(), static_cast<uint16_t>(width));
    m_height = std::min(std::numeric_limits<uint16_t>::max(), static_cast<uint16_t>(height));
    m_map.resize(static_cast<size_t>(m_width) * static_cast<size_t>(m_height));
    m_map.shrink_to_fit();
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
    m_map.resize(static_cast<size_t>(m_width) * static_cast<size_t>(m_height));
    m_map.shrink_to_fit();
    for (auto& cell : m_map)
    {
        stream.op(cell.terrain);
        stream.op(cell.unit_icon);
    }
}

void Map::write(File_write_stream& stream)
{
    stream.op(m_width);
    stream.op(m_height);
    for (auto& cell : m_map)
    {
        stream.op(cell.terrain);
        stream.op(cell.unit_icon);
    }
}

auto Map::get_terrain(Tile_coordinate tile_coordinate) const -> terrain_t
{
    Expects(tile_coordinate.x >= coordinate_t{0});
    Expects(tile_coordinate.y >= coordinate_t{0});
    Expects(tile_coordinate.x < m_width);
    Expects(tile_coordinate.y < m_height);
    const size_t index =
        static_cast<size_t>(tile_coordinate.x) +
        static_cast<size_t>(tile_coordinate.y) * static_cast<size_t>(m_width);
    return m_map[index].terrain;
}

void Map::set_terrain(Tile_coordinate tile_coordinate, terrain_t terrain_value)
{
    Expects(tile_coordinate.x >= coordinate_t{0});
    Expects(tile_coordinate.y >= coordinate_t{0});
    Expects(tile_coordinate.x < m_width);
    Expects(tile_coordinate.y < m_height);
    const size_t index =
        static_cast<size_t>(tile_coordinate.x) +
        static_cast<size_t>(tile_coordinate.y) * static_cast<size_t>(m_width);
    m_map[index].terrain = terrain_value;
}

auto Map::get_unit_icon(Tile_coordinate tile_coordinate) const -> uint16_t
{
    Expects(tile_coordinate.x >= coordinate_t{0});
    Expects(tile_coordinate.y >= coordinate_t{0});
    Expects(tile_coordinate.x < m_width);
    Expects(tile_coordinate.y < m_height);
    const size_t index =
        static_cast<size_t>(tile_coordinate.x) +
        static_cast<size_t>(tile_coordinate.y) * static_cast<size_t>(m_width);
    return m_map[index].unit_icon;
}

void Map::set_unit_icon(Tile_coordinate tile_coordinate, unit_t unit_icon_value)
{
    Expects(tile_coordinate.x >= coordinate_t{0});
    Expects(tile_coordinate.y >= coordinate_t{0});
    Expects(tile_coordinate.x < m_width);
    Expects(tile_coordinate.y < m_height);
    const size_t index =
        static_cast<size_t>(tile_coordinate.x) +
        static_cast<size_t>(tile_coordinate.y) * static_cast<size_t>(m_width);
    m_map[index].unit_icon = unit_icon_value;
}

auto Map::wrap(Tile_coordinate in) const -> Tile_coordinate
{
    Tile_coordinate ret = in;
    while (ret.x >= m_width)
    {
        ret.x -= static_cast<coordinate_t>(m_width);
    }
    while (ret.x < coordinate_t{0})
    {
        ret.x += static_cast<coordinate_t>(m_width);
    }
    while (ret.y >= m_height)
    {
        ret.y -= static_cast<coordinate_t>(m_height);
    }
    while (ret.y < coordinate_t{0})
    {
        ret.y += static_cast<coordinate_t>(m_height);
    }
    return ret;
}

auto Map::wrap_center(Tile_coordinate in) const -> Tile_coordinate
{
    Tile_coordinate ret = in;
    while (ret.x >= m_width / 2)
    {
        ret.x -= static_cast<coordinate_t>(m_width);
    }
    while (ret.x < -m_width / 2)
    {
        ret.x += static_cast<coordinate_t>(m_width);
    }
    while (ret.y >= m_height / 2)
    {
        ret.y -= static_cast<coordinate_t>(m_height);
    }
    while (ret.y < -m_height / 2)
    {
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

void Map::hex_circle(
    Tile_coordinate                                            center_position,
    int                                                        r0,
    int                                                        r1,
    const std::function<void(const Tile_coordinate position)>& op
)
{
    Expects(r0 >= 0);
    Expects(r1 >= r0);

    if (r0 == 0)
    {
        op(center_position);
        r0 = 1;
    }

    constexpr direction_t offset{2};

    for (int radius = r0; radius <= r1; ++radius)
    {
        auto position = center_position;
        for (int s = 0; s < radius; ++s)
        {
            position = wrap(position.neighbor(direction_north));
        }
        for (auto direction = direction_first; direction < direction_count; ++direction)
        {
            for (int s = 0; s < radius; ++s)
            {
                position = wrap(position.neighbor((direction + offset) % direction_count));
                op(position);
            }
        }
    }
}

}