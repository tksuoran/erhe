#include "hextiles/tiles.hpp"
#include "hextiles/file_util.hpp"
#include "hextiles/tile_shape.hpp"
#include "hextiles/types.hpp"

#include <nlohmann/json.hpp>

#include <gsl/assert>
#include <gsl/util>

#include <cassert>
#include <sstream>

namespace hextiles
{

Tiles::Tiles()
    : Component{c_name}
{
}

Tiles::~Tiles()
{
}

void Tiles::initialize_component()
{
    init_terrain_types();
    load_terrain_defs();
    //load_unit_defs();

    init_terrain_shapes();
    //init_unit_shapes();

    //{
    //    FileReadStream f("data/maps/default.new");
    //    m_map = std::make_unique<Map>(f);
    //}
    //
    //m_map_editor.m_game = this;
    //m_map_view.m_game = this;
    //m_terrain_palette.m_game = this;
    //m_terrain_palette.hide();
}

#if 0
auto Tiles::get_map() const -> Map*
{
    return m_map.get();
}

auto Tiles::get_map_view() -> Map_view&
{
    return m_map_view;
}
#endif

using json = nlohmann::json;

void Tiles::init_terrain_types()
{
    // { 20, 11, 22, -1, -1, -1, -1 }, // 0 Road
    // { 28, 28, 29, -1, -1, -1, -1 }, // 1 Peak
    // { 41, 41, 44, -1, -1, -1, -1 }, // 2 Forest
    // { 45, 45, 49,  1, -1, 28, 29 }, // 3 Mountain
    // {  5,  5, 18,  5,  6, -1, -1 }, // 4 Water Low
    // {  3,  3,  4,  6, -1, -1, -1 }, // 5 Water Medium
    // {  1,  1,  2, -1, -1, -1, -1 }, // 6 Water Deep

    constexpr terrain_t terrain_road        {20u};
    constexpr terrain_t terrain_peak        {28u};
    constexpr terrain_t terrain_forest      {41u};
    constexpr terrain_t terrain_mountain    {45u};
    constexpr terrain_t terrain_water_low   { 5u};
    constexpr terrain_t terrain_water_medium{ 3u};
    constexpr terrain_t terrain_water_deep  { 1u};

    constexpr int16_t group_road        {0u};
    constexpr int16_t group_peak        {1u};
    constexpr int16_t group_forest      {2u};
    constexpr int16_t group_mountain    {3u};
    constexpr int16_t group_water_low   {4u};
    constexpr int16_t group_water_medium{5u};
    constexpr int16_t group_water_deep  {6u};

    m_terrain_types.resize(Base_tiles::count);
    m_terrain_types[terrain_road        ].group = group_road;
    m_terrain_types[terrain_peak        ].group = group_peak;
    m_terrain_types[terrain_forest      ].group = group_forest;
    m_terrain_types[terrain_mountain    ].group = group_mountain;
    m_terrain_types[terrain_water_low   ].group = group_water_low;
    m_terrain_types[terrain_water_medium].group = group_water_medium;
    m_terrain_types[terrain_water_deep  ].group = group_water_deep;
}

auto Tiles::get_terrain_type(uint16_t terrain) const -> const Terrain_type&
{
    uint16_t base_terrain = get_base_terrain(terrain);
    return m_terrain_types[base_terrain];
}

#if 0
auto Tiles::map_renderer() -> const std::shared_ptr<Map_renderer>&
{
    return m_map_renderer;
}
#endif

auto Tiles::get_terrain_shapes() const -> const std::vector<Pixel_coordinate>&
{
    return m_terrain_shapes;
}

auto Tiles::get_grid_shape(int grid) const -> Pixel_coordinate
{
    return m_grid_shapes.at(grid);
}

auto Tiles::get_base_terrain(terrain_t terrain) const -> terrain_t
{
    if (terrain < m_multigroup_terrain_offset)
    {
        return terrain;
    }

    const int group = (terrain - m_multigroup_terrain_offset) / (Tile_group::width * Tile_group::height);
    Expects(group < Tile_group::count);
    const auto& multishape = gsl::at(multishapes, group);
    Expects(m_terrain_types[multishape.base_terrain_type].group == group);
    return multishape.base_terrain_type;
}

auto Tiles::get_multishape_terrain(int group, unsigned int neighbor_mask) -> terrain_t
{
    return static_cast<terrain_t>(
        m_multigroup_terrain_offset +
        group * Tile_group::width * Tile_group::height + neighbor_mask
    );
}

void Tiles::load_terrain_defs()
{
    const std::string def_data = read_file_string("res/hextiles/terrain_types.json");
    if (def_data.empty())
    {
        return;
    }
    const auto  j = json::parse(def_data);
    const auto& terrain_types = j["terrain_types"];
    m_terrain_types.resize(terrain_types.size());
    size_t index = 0u;
    for (const auto& t : terrain_types)
    {
        m_terrain_types[index].name = t["Name"];
        index++;
    }
    //constexpr size_t hex_types = 55U;
}

auto Tiles::get_terrain_shape(const terrain_t terrain) -> Pixel_coordinate
{
    Expects(terrain < m_terrain_shapes.size());
    return m_terrain_shapes[terrain];
}

void Tiles::init_terrain_shapes()
{
    // First row below multihexes has
    pixel_t ty_offset = Tile_group::count * Tile_group::height;
    for (int tx = 0; tx < Tile_group::width; ++tx)
    {
        m_extra_shapes.emplace_back(
            tx * Tile_shape::full_width,
            ty_offset * Tile_shape::height
        );
    }
    ty_offset += 1;

    // Edge shapes
    for (int tx = 0; tx < Tile_group::width; ++tx)
    {
        m_edge_shapes.emplace_back(
            tx * Tile_shape::full_width,
            ty_offset * Tile_shape::height
        );
    }
    ty_offset += 1;

    // Basetiles
    for (int ty = 0; ty < Base_tiles::height; ++ty)
    {
        for (int tx = 0; tx < Base_tiles::width; ++tx)
        {
            m_terrain_shapes.emplace_back(
                tx * Tile_shape::full_width,
                (ty + ty_offset) * Tile_shape::height
            );
        }
    }
    ty_offset += Base_tiles::height;

    // Grid shapes
    for (int tx = 0; tx < Tile_group::width; ++tx)
    {
        m_grid_shapes.emplace_back(
            tx * Tile_shape::full_width,
            ty_offset * Tile_shape::height
        );
    }

    m_multigroup_terrain_offset = static_cast<terrain_t>(m_terrain_shapes.size());
    for (int ty = 0; ty < Tile_group::count * Tile_group::height; ++ty)
    {
        for (int tx = 0; tx < Tile_group::width; ++tx)
        {
            m_terrain_shapes.emplace_back(
                tx * Tile_shape::full_width,
                ty * Tile_shape::height
            );
        }
    }
}


#if 0
void Tiles::draw()
{
    m_renderer.clear(Color(0.125F, 0.25F, 0.5F));
    m_map_view.draw();

    // Top HUD line
    {
        Coordinate mouse_position = m_renderer.mouse_position();
        Map *map = get_map();
        Map_view &view = m_map_view;
        //Coordinate tile_position = view.wrap(view.pixel_to_tile(mouse_position));
        Coordinate tile_position = view.pixel_to_tile(mouse_position);
        //Coordinate pixel_position = view.tile_to_pixel_center(tile_position);
        //Coordinate tile_position2 = view.pixel_to_tile(pixel_position);
        //Coordinate tile_position2 = view.wrap(view.pixel_to_tile(pixel_position));
        auto tile = map->get_terrain(tile_position);
        auto terrain = get_terrain_type(tile);

        fmt::memory_buffer buf;
        fmt::format_to(buf, "{} {}, {}", terrain.name, tile_position.x, tile_position.y);
        //ss << "M: " << mouse_position.x << ", " << mouse_position.y << " ";
        //ss << "T: " << tile_position.x << ", " << tile_position.y << " ";
        //ss << "P: " << pixel_position.x << ", " << pixel_position.y << " ";
        //ss << "T': " << tile_position2.x << ", " << tile_position2.y;
        //view.draw_text(pixel_position.x, pixel_position.y, ss.str());
        //view.draw_text(0, m_renderer.get_height() - 1, ss.str());
        view.draw_text(tile_full_width, 0, buf.data());
        if (tile < m_terrain_shapes.size())
        {
            Coordinate &shape = m_terrain_shapes[tile];
            SDL_Rect src_rect;
            src_rect.x = shape.x;
            src_rect.y = shape.y;
            src_rect.w = tile_full_width;
            src_rect.h = tile_height;
            SDL_Rect dst_rect;
            dst_rect.x = 0;
            dst_rect.y = 0;
            dst_rect.w = tile_full_width;
            dst_rect.h = tile_height;
            m_renderer.blit(m_hex_tiles, src_rect, dst_rect);
        }
    }

    if (m_map_view.hit(m_renderer.mouse_position()))
    {
        Coordinate center_position = m_map->wrap(m_map_view.pixel_to_tile(m_renderer.mouse_position()));
        Tiles *game = this;
        if (m_map_view.hit_tile(center_position))
        {
            Renderer *renderer = &m_renderer;
            SDL_Texture* texture = m_hex_tiles;
            m_renderer.set_texture_alpha_mode(texture, 0.5F);
            std::function<void(Coordinate)> brush_preview = [renderer, texture, game] (Coordinate position) -> void
            {
                MapEditor &map_editor = game->m_map_editor;
                Map_view &map_view = game->m_map_view;
                Map *map = game->get_map();
                terrain_t terrain = map_editor.last_brush;
                Coordinate &shape = game->m_terrain_shapes[terrain + map_editor.offset];
                Coordinate view_relative_tile = map->wrap(position - map_view.m_tile_offset);

                SDL_Rect src_rect;
                src_rect.x = shape.x;
                src_rect.y = shape.y;
                src_rect.w = tile_full_width;
                src_rect.h = tile_height;

                SDL_Rect dst_rect;
                dst_rect.w = tile_full_width;
                dst_rect.h = tile_height;
                dst_rect.x = view_relative_tile.x * tile_interleave_width;
                int y_offset = view_relative_tile.is_odd() ? 0 : tile_odd_y_offset;
                dst_rect.y = (view_relative_tile.y * tile_height + y_offset);
                renderer->blit(texture, src_rect, dst_rect);
            };
            m_map->HexCircle(center_position, 0, m_map_editor.brush_size - 1, brush_preview);
            m_renderer.set_texture_alpha_mode(texture, 1.0F);
        }
    }

    if (m_terrain_palette.visible)
    {
        m_terrain_palette.draw();
    }

    if (m_hotbar.visible)
    {
        m_hotbar.draw();
    }

    m_renderer.present();
}

void Tiles::key_event(const SDL_Event& e)
{
    if (e.type != SDL_KEYDOWN)
    {
        return;
    }

    switch (e.key.keysym.sym)
    {
        case SDLK_g:
        {
            m_map_view.m_grid++;
            if (m_map_view.m_grid == 3)
            {
                m_map_view.m_grid = 0;
            }
            break;
        }
        case SDLK_w:
        case SDLK_UP:       m_map_view.translate(Coordinate( 0, -4)); break;
        case SDLK_s:
        case SDLK_DOWN:     m_map_view.translate(Coordinate( 0,  4)); break;
        case SDLK_a:
        case SDLK_LEFT:     m_map_view.translate(Coordinate(-4,  0)); break;
        case SDLK_d:
        case SDLK_RIGHT:    m_map_view.translate(Coordinate( 4,  0)); break;
        case SDLK_PAGEUP:
        {
            m_map_view.zoom(1);
            break;
        }
        case SDLK_PAGEDOWN:
        {
            m_map_view.zoom(-1);
            break;
        }
        case SDLK_HOME:
        {
            m_map_editor.brush_size++;
            break;
        }
        case SDLK_END:
        {
            if (m_map_editor.brush_size > 1)
            {
                m_map_editor.brush_size--;
            }
            break;
        }
        case SDLK_ESCAPE:
        {
            m_running = false;
            break;
        }
        default:
        {
            break;
        }
    }
}

void Tiles::mouse_event(const SDL_Event& e)
{
    m_mouse_event = true;
    if ((e.type == SDL_MOUSEBUTTONDOWN) ||
        (e.type == SDL_MOUSEBUTTONUP)
    )
    {
        if (m_terrain_palette.visible && m_terrain_palette.hit(m_renderer.mouse_position()))
        {
            m_terrain_palette.mouse_event(e);
        }
        else
        {
            bool pressed = (e.type == SDL_MOUSEBUTTONDOWN);
            if (e.button.button == SDL_BUTTON_LEFT)
            {
                m_left_mouse = pressed;
            }
            else if (e.button.button == SDL_BUTTON_RIGHT)
            {
                m_right_mouse = pressed;
            }
        }
    }
}

void Tiles::main_loop()
{
    m_running = true;
    m_left_mouse = false;
    m_right_mouse = false;
    m_renderer.set_window_event_handler(this);
    m_renderer.set_key_event_handler(this);
    m_renderer.set_mouse_event_handler(this);
    while (m_running)
    {
        m_mouse_event = false;

        m_renderer.handle_events();

        if (m_mouse_event)
        {
            if (m_map_view.hit(m_renderer.mouse_position()))
            {
                m_map_editor.mouse(m_left_mouse, m_right_mouse, m_renderer.mouse_position());
            }
        }

        draw();

    }
    m_renderer.set_window_event_handler(nullptr);
    m_renderer.set_key_event_handler(nullptr);
    m_renderer.set_mouse_event_handler(nullptr);
}
#endif

} // namespace hextiles
