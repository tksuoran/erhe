#include "map.hpp"

#include "tiles.hpp"

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
    Expects(width > 0);
    Expects(height > 0);
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
    Expects(m_width > 0);
    Expects(m_height > 0);
    m_map.resize(static_cast<size_t>(m_width) * static_cast<size_t>(m_height));
    //m_map.shrink_to_fit();
    for (auto& cell : m_map)
    {
        stream.op(cell.terrain_tile);
        stream.op(cell.unit_tile);
        /// XXX TODO FIXME
        if (cell.terrain_tile > 55)
        {
            ++cell.terrain_tile;
        }
    }
}

void Map::write(File_write_stream& stream)
{
    stream.op(m_width);
    stream.op(m_height);
    for (auto& cell : m_map)
    {
        stream.op(cell.terrain_tile);
        stream.op(cell.unit_tile);
    }
}

auto Map::get_terrain_tile(Tile_coordinate tile_coordinate) const -> terrain_tile_t
{
    Expects(tile_coordinate.x >= coordinate_t{0});
    Expects(tile_coordinate.y >= coordinate_t{0});
    Expects(tile_coordinate.x < m_width);
    Expects(tile_coordinate.y < m_height);
    const size_t index =
        static_cast<size_t>(tile_coordinate.x) +
        static_cast<size_t>(tile_coordinate.y) * static_cast<size_t>(m_width);
    return m_map[index].terrain_tile;
}

void Map::set_terrain_tile(Tile_coordinate tile_coordinate, terrain_tile_t terrain_tile)
{
    Expects(tile_coordinate.x >= coordinate_t{0});
    Expects(tile_coordinate.y >= coordinate_t{0});
    Expects(tile_coordinate.x < m_width);
    Expects(tile_coordinate.y < m_height);
    const size_t index =
        static_cast<size_t>(tile_coordinate.x) +
        static_cast<size_t>(tile_coordinate.y) * static_cast<size_t>(m_width);
    m_map[index].terrain_tile = terrain_tile;
}

auto Map::get_unit_tile(Tile_coordinate tile_coordinate) const -> unit_tile_t
{
    Expects(tile_coordinate.x >= coordinate_t{0});
    Expects(tile_coordinate.y >= coordinate_t{0});
    Expects(tile_coordinate.x < m_width);
    Expects(tile_coordinate.y < m_height);
    const size_t index =
        static_cast<size_t>(tile_coordinate.x) +
        static_cast<size_t>(tile_coordinate.y) * static_cast<size_t>(m_width);
    return m_map[index].unit_tile;
}

void Map::set_unit_tile(Tile_coordinate tile_coordinate, unit_tile_t unit_tile)
{
    Expects(tile_coordinate.x >= coordinate_t{0});
    Expects(tile_coordinate.y >= coordinate_t{0});
    Expects(tile_coordinate.x < m_width);
    Expects(tile_coordinate.y < m_height);
    const size_t index =
        static_cast<size_t>(tile_coordinate.x) +
        static_cast<size_t>(tile_coordinate.y) * static_cast<size_t>(m_width);
    m_map[index].unit_tile = unit_tile;
}

void Map::set(Tile_coordinate tile_coordinate, terrain_tile_t terrain_tile, unit_tile_t unit_tile)
{
    Expects(tile_coordinate.x >= coordinate_t{0});
    Expects(tile_coordinate.y >= coordinate_t{0});
    Expects(tile_coordinate.x < m_width);
    Expects(tile_coordinate.y < m_height);
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
    Expects(ret.x >= coordinate_t{0});
    Expects(ret.y >= coordinate_t{0});
    Expects(ret.x < m_width);
    Expects(ret.y < m_height);

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

void Map::for_each_tile(const std::function<void(Tile_coordinate position)>& op)
{
    for (coordinate_t ty = 0; ty < m_height; ++ty)
    {
        for (coordinate_t tx = 0; tx < m_width; ++tx)
        {
            const Tile_coordinate tile_position{tx, ty};
            op(tile_position);
        }
    }
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

auto Map::distance(
    const Tile_coordinate& lhs,
    const Tile_coordinate& rhs
) -> int
{
    const auto d = wrap_center(lhs - rhs);

    int yl = lhs.is_odd()
        ? 2
        : 0;

    int dy = d.is_odd()
        ? std::abs(2 * d.y - 1 + yl) / 2
        : std::abs(2 * d.y) / 2;

    int dx = std::abs(d.x);

    return dx > dy * 2
        ? dx
        : dy + dx / 2;
}

void Map::update_group_terrain(Tile_coordinate position)
{
    const terrain_tile_t terrain_tile = get_terrain_tile(position);
    const terrain_t      terrain      = g_tiles->get_terrain_from_tile(terrain_tile);
    const auto&          terrain_type = g_tiles->get_terrain_type(terrain);
    int                  group        = terrain_type.group;
    if (group < 0)
    {
        return;
    }

    uint32_t neighbor_mask;
    bool promote;
    bool demote;
    size_t counter = 0u;
    do
    {
        promote = false;
        demote  = false;
        const Terrain_group& terrain_group = g_tiles->get_terrain_group(group);

        neighbor_mask = 0u;
        for (
            direction_t direction = direction_first;
            direction <= direction_last;
            ++direction
        )
        {
            const Tile_coordinate neighbor_position     = neighbor(position, direction);
            const terrain_tile_t  neighbor_terrain_tile = get_terrain_tile(neighbor_position);
            const terrain_t       neighbor_terrain      = g_tiles->get_terrain_from_tile(neighbor_terrain_tile);
            const int neighbor_group = g_tiles->get_terrain_type(neighbor_terrain).group;
            if (
                (neighbor_group == group) ||
                (
                    (terrain_group.link_group[0] >= 0) &&
                    (neighbor_group == terrain_group.link_group[0])
                ) ||
                (
                    (terrain_group.link_group[1] >= 0) &&
                    (neighbor_group == terrain_group.link_group[1])
                ) ||
                (
                    (terrain_group.link_first[0] > 0) &&
                    (terrain_group.link_last [0] > 0) &&
                    (neighbor_terrain >= terrain_group.link_first[0]) &&
                    (neighbor_terrain <= terrain_group.link_last [0])
                ) ||
                (
                    (terrain_group.link_first[1] > 0) &&
                    (terrain_group.link_last [1] > 0) &&
                    (neighbor_terrain >= terrain_group.link_first[1]) &&
                    (neighbor_terrain <= terrain_group.link_last [1])
                )
            )
            {
                neighbor_mask = neighbor_mask | (1u << direction);
            }
            if (
                (terrain_group.demoted >= 0) &&
                (neighbor_group != group) &&
                (neighbor_group != terrain_group.demoted))
            {
                demote = true;
            }
        }
        promote = (neighbor_mask == direction_mask_all) && (terrain_group.promoted > 0);
        Expects((promote && demote) == false);
        if (promote)
        {
            group = terrain_group.promoted;
        }
        else if (demote)
        {
            group = terrain_group.demoted;
        }
    }
    while (
        (promote || demote) &&
        (++counter < 2U)
    );

    const terrain_tile_t updated_terrain_tile = g_tiles->get_terrain_group_tile(group, neighbor_mask);
    set_terrain_tile(position, updated_terrain_tile);
}

} // namespace hextiles
