#include "map_window.hpp"
#include "hextiles_log.hpp"
#include "map.hpp"
#include "menu_window.hpp"
#include "pixel_lookup.hpp"
#include "stream.hpp" // map loading
#include "tiles.hpp"
#include "tile_renderer.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui_windows.hpp"
#include "erhe/application/view.hpp"
#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/renderers/imgui_renderer.hpp"
#include "erhe/application/renderers/text_renderer.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/graphics/framebuffer.hpp"

#include <imgui.h>

#include <gsl/assert>

#include <algorithm>
#include <functional>

namespace hextiles
{

#pragma region commands
auto Map_free_zoom_command::try_call(erhe::application::Command_context& context) -> bool
{
    const auto wheel = context.get_vec2_relative_value();
    const double v = wheel.x + wheel.y;
    const double k = 0.1;

    //const auto window_position = context.view().to_window_top_left(context.get_vec2_absolute_value());
    //m_map_window.hover(
    //    window_position
    //);

    m_map_window.scale_zoom(static_cast<float>(1.0 + v * k));
    return true;
}

auto Map_mouse_scroll_command::try_call(erhe::application::Command_context& context) -> bool
{
    if (state() == erhe::application::State::Ready)
    {
        set_active(context);
    }

    if (state() != erhe::application::State::Active)
    {
        return false;
    }

    //const auto window_position = context.view().to_window_top_left(context.get_vec2_absolute_value());
    //m_map_window.hover(
    //    window_position
    //);
    m_map_window.scroll(
        glm::vec2{context.get_vec2_relative_value()}
    );
    return true;
}

void Map_mouse_scroll_command::try_ready(erhe::application::Command_context& context)
{
    if (state() != erhe::application::State::Inactive)
    {
        return;
    }

    set_ready(context);
}

auto Map_scroll_command::try_call(erhe::application::Command_context& context) -> bool
{
    static_cast<void>(context);

    m_map_window.scroll_tiles(m_offset);
    return true;
}

auto Map_zoom_command::try_call(erhe::application::Command_context& context) -> bool
{
    static_cast<void>(context);

    m_map_window.scale_zoom(m_scale);
    return true;
}

auto Map_grid_cycle_command::try_call(erhe::application::Command_context& context) -> bool
{
    static_cast<void>(context);
    m_map_window.grid_cycle();
    return true;
}

#pragma endregion commands


Map_window::Map_window()
    : erhe::components::Component{c_label}
    , Framebuffer_window         {c_title, c_label}
    , m_free_zoom_command        {*this}
    , m_mouse_scroll_command     {*this}
    , m_scroll_left_command      {*this, -4.0f,  0.0f }
    , m_scroll_right_command     {*this,  4.0f,  0.0f }
    , m_scroll_up_command        {*this,  0.0f, -4.0f }
    , m_scroll_down_command      {*this,  0.0f,  4.0f }
    , m_zoom_in_command          {*this, 0.5f}
    , m_zoom_out_command         {*this, 2.0f}
    , m_grid_cycle_command       {*this}
{
}

Map_window::~Map_window() noexcept
{
}

#pragma region Component
void Map_window::declare_required_components()
{
    require<erhe::application::Imgui_windows      >();
    require<erhe::application::Gl_context_provider>();
    require<erhe::application::Imgui_renderer     >(); // required for Imgui_window

    m_editor_view = require<erhe::application::View>();
    m_tiles       = require<Tiles>();
}

void Map_window::initialize_component()
{
    Framebuffer_window::initialize(
        *m_components
    );

    m_pixel_lookup = std::make_unique<Pixel_lookup>();

    const auto view = get<erhe::application::View>();
    view->register_command(&m_free_zoom_command);
    view->register_command(&m_mouse_scroll_command);

    view->register_command(&m_scroll_left_command);
    view->register_command(&m_scroll_right_command);
    view->register_command(&m_scroll_up_command);
    view->register_command(&m_scroll_down_command);
    view->register_command(&m_zoom_in_command);
    view->register_command(&m_zoom_out_command);
    view->register_command(&m_grid_cycle_command);

    view->bind_command_to_mouse_wheel(&m_free_zoom_command);
    view->bind_command_to_mouse_drag (&m_mouse_scroll_command, erhe::toolkit::Mouse_button_right);

    view->bind_command_to_key(&m_scroll_up_command,    erhe::toolkit::Key_w, false);
    view->bind_command_to_key(&m_scroll_left_command,  erhe::toolkit::Key_a, false);
    view->bind_command_to_key(&m_scroll_down_command,  erhe::toolkit::Key_s, false);
    view->bind_command_to_key(&m_scroll_right_command, erhe::toolkit::Key_d, false);
    view->bind_command_to_key(&m_zoom_in_command,      erhe::toolkit::Key_period, false);
    view->bind_command_to_key(&m_zoom_out_command,     erhe::toolkit::Key_comma,  false);
    view->bind_command_to_key(&m_grid_cycle_command,   erhe::toolkit::Key_g,  false);
}

void Map_window::post_initialize()
{
    m_text_renderer = get<erhe::application::Text_renderer>();
    m_tile_renderer = get<Tile_renderer>();
}

#pragma endregion Component

auto Map_window::flags() -> ImGuiWindowFlags
{
    return ImGuiWindowFlags_NoScrollbar;
}

auto Map_window::consumes_mouse_input() const -> bool
{
    return true;
}

void Map_window::on_begin()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
}

void Map_window::on_end()
{
    ImGui::PopStyleVar();
}

auto Map_window::mouse_scroll_try_ready() const -> bool
{
    return m_is_hovered;
}

void Map_window::scroll(glm::vec2 delta)
{
    m_pixel_offset.x += delta.x; //* m_zoom;
    m_pixel_offset.y += delta.y; //* m_zoom;

    while (m_pixel_offset.x < 0.0f)
    {
        m_pixel_offset.x += Tile_shape::interleave_width * 2.0f * m_zoom;
        m_center_tile.x -= 2;
    }

    while (m_pixel_offset.y < 0.0f)
    {
        m_pixel_offset.y += Tile_shape::height * m_zoom;
        --m_center_tile.y;
    }

    while (m_pixel_offset.x >= Tile_shape::interleave_width * 2.0f * m_zoom)
    {
        m_pixel_offset.x -= Tile_shape::interleave_width * 2.0f * m_zoom;
        m_center_tile.x += 2;
    }

    while (m_pixel_offset.y >= Tile_shape::height * m_zoom)
    {
        m_pixel_offset.y -= Tile_shape::height * m_zoom;
        ++m_center_tile.y;
    }
}

void Map_window::scroll_tiles(glm::vec2 delta)
{
    scroll(
        glm::vec2{
            delta.x * Tile_shape::interleave_width * m_zoom,
            delta.y * Tile_shape::height * m_zoom
        }
    );
}

void Map_window::grid_cycle()
{
    m_grid = m_grid + 1;
    if (m_grid > 2)
    {
        m_grid = 0;
    }
}
void Map_window::scroll_to(const Tile_coordinate center_tile)
{
    m_center_tile = center_tile;
    m_pixel_offset = glm::vec2{0.0f, 0.0f};
}

auto Map_window::get_size(glm::vec2 available_size) const -> glm::vec2
{
    return available_size;
}

auto Map_window::wrap(Tile_coordinate in) const -> Tile_coordinate
{
    return m_map->wrap(in);
}

auto Map_window::wrap_center(Tile_coordinate in) const -> Tile_coordinate
{
    return m_map->wrap_center(in);
}

auto Map_window::hit_tile(Tile_coordinate tile_coordinate) const -> bool
{
    return
        (tile_coordinate.x >= coordinate_t{0}) &&
        (tile_coordinate.y >= coordinate_t{0}) &&
        (tile_coordinate.x < m_map->width()) &&
        (tile_coordinate.y < m_map->height());
}

pixel_t pixel_round(float a)
{
    return static_cast<pixel_t>(std::floor(a));
}

auto Map_window::normalize(Pixel_coordinate pixel_coordinate) const -> Pixel_coordinate
{
    if (!m_texture)
    {
        return {};
    }
    const float extent_x       = static_cast<float>(m_texture->width ());
    const float extent_y       = static_cast<float>(m_texture->height());
    const float center_pixel_x = std::floor(extent_x / 2.0f);
    const float center_pixel_y = std::floor(extent_y / 2.0f);
    const float normalized_x   = pixel_coordinate.x - center_pixel_x + m_pixel_offset.x;
    const float normalized_y   = pixel_coordinate.y - center_pixel_y + m_pixel_offset.y;

    return Pixel_coordinate{
        pixel_round(normalized_x / m_zoom),
        pixel_round(normalized_y / m_zoom)
    };
}

auto Map_window::pixel_to_tile(Pixel_coordinate pixel_coordinate) const -> Tile_coordinate
{
    if (!m_map)
    {
        return Tile_coordinate{0, 0};
    }

    const auto normalized         = normalize(pixel_coordinate);
    const auto view_relative_tile = m_pixel_lookup->pixel_to_tile(normalized);
    return wrap(view_relative_tile + m_center_tile);
}

void Map_window::scale_zoom(float scale)
{
    m_pixel_offset /= m_zoom;
    m_zoom *= scale;
    if (m_zoom < 1.0f)
    {
        m_zoom = 1.0f;
    }
    m_pixel_offset *= m_zoom;

    log_map_window->trace("scale_zoom(scale = {}) final zoom = {}", scale, m_zoom);
}

void Map_window::set_zoom(float scale)
{
    m_zoom = scale;
    if (m_zoom < 1.0f)
    {
        m_zoom = 1.0f;
    }
}

// static constexpr const char* c_grid_mode_strings[] =
// {
//     "Off",
//     "Grid 1",
//     "Grid 2"
// };

auto Map_window::tile_image(terrain_tile_t terrain_tile, const int scale) -> bool
{
    const auto& terrain_shapes = m_tile_renderer->get_terrain_shapes();
    if (terrain_tile >= terrain_shapes.size())
    {
        return false;
    }

    const auto&     texel           = terrain_shapes.at(terrain_tile);
    const auto&     tileset_texture = m_tile_renderer->tileset_texture();
    const glm::vec2 uv0{
        static_cast<float>(texel.x) / static_cast<float>(tileset_texture->width()),
        static_cast<float>(texel.y) / static_cast<float>(tileset_texture->height()),
    };
    const glm::vec2 uv1 = uv0 + glm::vec2{
        static_cast<float>(Tile_shape::full_width) / static_cast<float>(tileset_texture->width()),
        static_cast<float>(Tile_shape::height) / static_cast<float>(tileset_texture->height()),
    };

    return m_imgui_renderer->image(
        tileset_texture,
        Tile_shape::full_width * scale,
        Tile_shape::height * scale,
        uv0,
        uv1,
        glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
        false
    );
}

void Map_window::set_map(const std::shared_ptr<Map>& map)
{
    m_map = map;
}

auto Map_window::tile_position(const Tile_coordinate absolute_tile) const -> glm::vec2
{
    Expects(m_texture);

    const float extent_x       = static_cast<float>(m_texture->width ());
    const float extent_y       = static_cast<float>(m_texture->height());
    const float center_pixel_x = std::floor(extent_x / 2.0f);
    const float center_pixel_y = std::floor(extent_y / 2.0f);

    const auto  center_relative_tile = wrap_center(absolute_tile - m_center_tile);
    const bool  x_odd                = absolute_tile.is_odd();
    const float y_offset             = x_odd ? Tile_shape::odd_y_offset * m_zoom : 0.0f;

    const float x0 =
        center_pixel_x
        - m_pixel_offset.x
        - (Tile_shape::center_x * m_zoom)
        + (center_relative_tile.x * Tile_shape::interleave_width * m_zoom);

    const float y0 =
        center_pixel_y
        + m_pixel_offset.y
        + (Tile_shape::center_y * m_zoom)
        - (center_relative_tile.y * Tile_shape::height * m_zoom)
        - y_offset;

    return glm::vec2{x0, y0};
}

void Map_window::render()
{
    if (!m_texture)
    {
        return;
    }

    const float extent_x = static_cast<float>(m_texture->width ());
    const float extent_y = static_cast<float>(m_texture->height());

    if ((extent_x < 1.0f) || (extent_y < 1.0f))
    {
        return;
    }

    const float center_pixel_x = std::floor(extent_x / 2.0f);
    const float center_pixel_y = std::floor(extent_y / 2.0f);

    const auto  grid_shape     = m_tile_renderer->get_extra_shape(Extra_tiles::grid_offset + std::max(0, m_grid - 1));
    const auto& terrain_shapes = m_tile_renderer->get_terrain_shapes();
    const auto& unit_shapes    = m_tile_renderer->get_unit_shapes();

    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, m_framebuffer->gl_name());
    gl::clear_color     (0.2f, 0.0f, 0.2f, 1.0f);
    gl::clear           (gl::Clear_buffer_mask::color_buffer_bit);

    m_tile_renderer->begin();
    coordinate_t half_width_in_tiles  = 2 + static_cast<coordinate_t>(std::ceil(extent_x / (Tile_shape::interleave_width * m_zoom)));
    coordinate_t half_height_in_tiles = 2 + static_cast<coordinate_t>(std::ceil(extent_y / (Tile_shape::height           * m_zoom)));
    for (coordinate_t vx = -half_width_in_tiles; vx < half_width_in_tiles; ++vx)
    {
        for (coordinate_t vy = -half_height_in_tiles; vy < half_height_in_tiles; ++vy)
        {
            const auto  absolute_tile        = wrap(m_center_tile + Tile_coordinate{vx, vy});
            const auto  center_relative_tile = Tile_coordinate{vx, vy};
            const bool  x_odd                = absolute_tile.is_odd();
            const float y_offset             = x_odd ? Tile_shape::odd_y_offset * m_zoom : 0.0f;

            const float x0 =
                center_pixel_x
                - m_pixel_offset.x
                - (Tile_shape::center_x * m_zoom)
                + (center_relative_tile.x * Tile_shape::interleave_width * m_zoom);

            const float y0 =
                center_pixel_y
                + m_pixel_offset.y
                + (Tile_shape::center_y * m_zoom)
                - (center_relative_tile.y * Tile_shape::height * m_zoom)
                - y_offset;

            const float x1 = x0 + Tile_shape::full_width * m_zoom;
            const float y1 = y0 - Tile_shape::height * m_zoom;

            const uint32_t       color        = 0xffffffffu; //get_color((absolute_tile.x & 1) == 1, (absolute_tile.y & 1) == 0);
            const terrain_tile_t terrain_tile = m_map->get_terrain_tile(absolute_tile);
            //const tile_t    terrain_tile = m_tile_renderer->get_terrain_tile(terrain_id);
            const unit_tile_t    unit_tile    = m_map->get_unit_tile(absolute_tile);
            //if (terrain >= terrain_shapes.size())
            //{
            //    terrain = 0;
            //}

            const Pixel_coordinate& terrain_shape = terrain_shapes[terrain_tile];
            m_tile_renderer->blit(
                terrain_shape.x,
                terrain_shape.y,
                Tile_shape::full_width,
                Tile_shape::height,
                x0,
                y0,
                x1,
                y1,
                color
            );

            if (unit_tile != 0)
            {
                const Pixel_coordinate& unit_shape = unit_shapes[unit_tile];
                //int player = unit_id / (Unit_group::width * Unit_group::height);
                m_tile_renderer->blit(
                    unit_shape.x,
                    unit_shape.y, // + player * Unit_group::height * Tile_shape::height,
                    Tile_shape::full_width,
                    Tile_shape::height,
                    x0,
                    y0,
                    x1,
                    y1,
                    color
                );
            }

            if (m_grid > 0)
            {
                m_tile_renderer->blit(
                    grid_shape.x,
                    grid_shape.y,
                    Tile_shape::full_width,
                    Tile_shape::height,
                    x0,
                    y0,
                    x1,
                    y1,
                    //0x88888888u
                    0x44444444u
                );
            }

#if 0
            if (
                (vx > -5) &&
                (vx <  5) &&
                (vy > -5) &&
                (vy <  5)
            )
            {
                std::string text = fmt::format("{}, {}", absolute_tile.x, absolute_tile.y);
                static const float z = -0.5f;
                const auto bounds = m_text_renderer->measure(text);
                m_text_renderer->print(
                    glm::vec3{
                        x0 + Tile_shape::center_x * m_zoom - bounds.half_size().x,
                        y0 - Tile_shape::center_y * m_zoom - bounds.half_size().y,
                        z
                    },
                    0xffffffffu,
                    text
                );
            }
#endif
        }
    }

    //m_map_editor->render();

    m_tile_renderer->end();

    erhe::scene::Viewport viewport
    {
        .x             = 0,
        .y             = 0,
        .width         = static_cast<int>(extent_x),
        .height        = static_cast<int>(extent_y),
        .reverse_depth = get<erhe::application::Configuration>()->graphics.reverse_depth

    };
    m_tile_renderer->render(viewport);

    m_text_renderer->render(viewport);
    m_text_renderer->next_frame();

    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);
}

void Map_window::blit(
    const Pixel_coordinate shape,
    const Tile_coordinate  tile_location,
    const uint32_t         color
) const
{
    const auto pixel_position = tile_position(tile_location);
    m_tile_renderer->blit(
        shape.x,
        shape.y,
        Tile_shape::full_width,
        Tile_shape::height,
        pixel_position.x,
        pixel_position.y,
        pixel_position.x + Tile_shape::full_width * m_zoom,
        pixel_position.y - Tile_shape::height     * m_zoom,
        color
    );
}

void Map_window::print(
    const std::string_view text,
    const Tile_coordinate  tile_location,
    const uint32_t         color
) const
{
    const auto pixel_position = tile_position(tile_location);
    static const float z = -0.5f;
    const auto bounds = m_text_renderer->measure(text);
    m_text_renderer->print(
        glm::vec3{
            pixel_position.x + Tile_shape::center_x * m_zoom - bounds.half_size().x,
            pixel_position.y - Tile_shape::center_y * m_zoom - bounds.half_size().y,
            z
        },
        color,
        text
    );
}

} // namespace hextiles
