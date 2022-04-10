#include "game.hpp"
#include "globals.hpp"

#include <gsl/assert>

namespace power_battle
{

auto Game::get_terrain_shape(const uint16_t terrain) -> Pixel_coordinate
{
    Expects(terrain < m_terrain_shapes.size());
    return m_terrain_shapes[terrain];
}

void Game::init_terrain_shapes()
{
    // First row below multihexes has
    pixel_t ty_offset = group_count * group_height;
    for (int tx = 0; tx < group_width; ++tx)
    {
        m_extra_shapes.emplace_back(
            tx * tile_full_width,
            ty_offset * tile_height
        );
    }
    ty_offset += 1;

    // Edge shapes
    for (int tx = 0; tx < group_width; ++tx)
    {
        m_edge_shapes.emplace_back(
            tx * tile_full_width,
            ty_offset * tile_height
        );
    }
    ty_offset += 1;

    // Basetiles
    for (int ty = 0; ty < basetiles_height; ++ty)
    {
        for (int tx = 0; tx < basetiles_width; ++tx)
        {
            m_terrain_shapes.emplace_back(
                tx * tile_full_width,
                (ty + ty_offset) * tile_height
            );
        }
    }
    ty_offset += basetiles_height;

    // Grid shapes
    for (int tx = 0; tx < group_width; ++tx)
    {
        m_grid_shapes.emplace_back(
            tx * tile_full_width,
            ty_offset * tile_height
        );
    }

    m_multigroup_terrain_offset = static_cast<terrain_t>(m_terrain_shapes.size());
    for (int ty = 0; ty < group_count * group_height; ++ty)
    {
        for (int tx = 0; tx < group_width; ++tx)
        {
            m_terrain_shapes.emplace_back(
                tx * tile_full_width,
                ty * tile_height
            );
        }
    }
}

void Game::init_unit_shapes()
{
    for (int ty = 0; ty < unit_group_height; ++ty)
    {
        for (int tx = 0; tx < unit_group_width; ++tx)
        {
            m_unit_shapes.emplace_back(
                tx * tile_full_width,
                ty * tile_height
            );
        }
    }
}

}
