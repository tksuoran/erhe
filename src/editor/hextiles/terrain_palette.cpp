#include "terrain_palette.hpp"
#include "game.hpp"
#include "globals.hpp"
#include "map_renderer.hpp"

namespace power_battle
{

void Terrain_palette::mouse_event(
    bool             left_mouse,
    bool             right_mouse,
    Pixel_coordinate position
)
{
    if (!left_mouse && !right_mouse)
    {
        return;
    }
    pixel_t x    = position.x - offset.x;
    pixel_t y    = position.y - offset.y;
    int     tx   = x / tile_full_width;
    int     ty   = y / tile_height;
    int     tile = tx + ty * basetiles_width;
    if (left_mouse)
    {
        m_game->m_map_editor.left_brush = static_cast<terrain_t>(tile);
    }
    else if (right_mouse)
    {
        m_game->m_map_editor.right_brush = static_cast<terrain_t>(tile);;
    }
}

void Terrain_palette::draw() const
{
    const auto& map_renderer = m_game->map_renderer();
    //const Renderer &renderer = m_game->m_renderer;
    //SDL_Texture *hex_tiles = m_game->m_hex_tiles;

    //SDL_Rect src_rect;
    //src_rect.x = 0;
    //src_rect.y = 0;
    //src_rect.w = tile_full_width;
    //src_rect.h = tile_height;

    //SDL_Rect dst_rect;
    //dst_rect.x = 0;
    //dst_rect.y = 0;
    //dst_rect.w = tile_full_width;
    //dst_rect.h = tile_height;
    for (int tx = 0; tx < basetiles_width; ++tx)
    {
        const int dst_x = offset.x + tx * tile_full_width;
        for (int ty = 0; ty < basetiles_height; ++ty)
        {
            const terrain_t         terrain = static_cast<terrain_t>(tx + ty * basetiles_width);
            const Pixel_coordinate& shape   = m_game->m_terrain_shapes[terrain];
            const int               dst_y   = offset.y + ty * tile_height;
            map_renderer->blit(shape.x, shape.y, tile_full_width, tile_height, dst_x, dst_y);
        }
    }
}

}
