#include "map_window.hpp"

#include "hextiles_log.hpp"
#include "map.hpp"
#include "menu_window.hpp"
#include "pixel_lookup.hpp"
#include "stream.hpp" // map loading
#include "tiles.hpp"
#include "tile_renderer.hpp"

#include "erhe/configuration/configuration.hpp"
#include "erhe/imgui/imgui_windows.hpp"
#include "erhe/commands/commands.hpp"
#include "erhe/commands/input_arguments.hpp"
#include "erhe/graphics/gl_context_provider.hpp"
#include "erhe/graphics/instance.hpp"
#include "erhe/imgui/imgui_renderer.hpp"
#include "erhe/imgui/imgui_windows.hpp"
#include "erhe/renderer/text_renderer.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/toolkit/verify.hpp"

#include <imgui.h>

#include <gsl/assert>

#include <algorithm>
#include <functional>

namespace hextiles
{

#pragma region commands
Map_free_zoom_command::Map_free_zoom_command(
    erhe::commands::Commands& commands,
    Map_window&               map_window
)
    : Command     {commands, "Map.hover"}
    , m_map_window{map_window}
{
}

auto Map_free_zoom_command::try_call_with_input(
    erhe::commands::Input_arguments& input
) -> bool
{
    const auto  wheel = input.vector2.relative_value;
    const float v = wheel.x + wheel.y;
    const float k = 0.1f;

    //const auto window_position = context.view().to_window_top_left(context.get_vec2_absolute_value());
    //m_map_window.hover(
    //    window_position
    //);

    m_map_window.scale_zoom(1.0f + v * k);
    return true;
}

Map_mouse_scroll_command::Map_mouse_scroll_command(
    erhe::commands::Commands& commands,
    Map_window&               map_window
)
    : Command     {commands, "Map.mouse_scroll"}
    , m_map_window{map_window}
{
}

auto Map_mouse_scroll_command::try_call_with_input(
    erhe::commands::Input_arguments& input
) -> bool
{
    if (get_command_state() == erhe::commands::State::Ready) {
        set_active();
    }

    if (get_command_state() != erhe::commands::State::Active) {
        return false;
    }

    //const auto window_position = context.view().to_window_top_left(context.get_vec2_absolute_value());
    //m_map_window.hover(
    //    window_position
    //);
    m_map_window.scroll(input.vector2.relative_value);
    return true;
}

void Map_mouse_scroll_command::try_ready()
{
    if (get_command_state() != erhe::commands::State::Inactive) {
        return;
    }

    set_ready();
}

Map_scroll_command::Map_scroll_command(
    erhe::commands::Commands& commands,
    Map_window&               map_window,
    const float               dx,
    const float               dy
)
    : Command     {commands, "Map.scroll"}
    , m_map_window{map_window}
    , m_offset    {dx, dy}
{
}

auto Map_scroll_command::try_call() -> bool
{
    m_map_window.scroll_tiles(m_offset);
    return true;
}

Map_zoom_command::Map_zoom_command(
    erhe::commands::Commands& commands,
    Map_window&               map_window,
    const float               scale
)
    : Command     {commands, "Map.scroll"}
    , m_map_window{map_window}
    , m_scale     {scale}
{
}

auto Map_zoom_command::try_call() -> bool
{
    m_map_window.scale_zoom(m_scale);
    return true;
}

Map_grid_cycle_command::Map_grid_cycle_command(
    erhe::commands::Commands& commands,
    Map_window&               map_window
)
    : Command     {commands, "Map.grid_cycle"}
    , m_map_window{map_window}
{
}

auto Map_grid_cycle_command::try_call() -> bool
{
    m_map_window.grid_cycle();
    return true;
}

#pragma endregion commands

Map_window::Map_window(
    erhe::commands::Commands&      commands,
    erhe::graphics::Instance&      graphics_instance,
    erhe::imgui::Imgui_renderer&   imgui_renderer,
    erhe::imgui::Imgui_windows&    imgui_windows,
    erhe::renderer::Text_renderer& text_renderer,
    Tile_renderer&                 tile_renderer
)
    : Framebuffer_window    {graphics_instance, imgui_renderer, imgui_windows, "Map", "map"}
    , m_text_renderer       {text_renderer}
    , m_tile_renderer       {tile_renderer}
    , m_free_zoom_command   {commands, *this}
    , m_mouse_scroll_command{commands, *this}
    , m_scroll_left_command {commands, *this, -4.0f,  0.0f }
    , m_scroll_right_command{commands, *this,  4.0f,  0.0f }
    , m_scroll_up_command   {commands, *this,  0.0f, -4.0f }
    , m_scroll_down_command {commands, *this,  0.0f,  4.0f }
    , m_zoom_in_command     {commands, *this, 0.5f}
    , m_zoom_out_command    {commands, *this, 2.0f}
    , m_grid_cycle_command  {commands, *this}
{
    commands.register_command(&m_free_zoom_command);
    commands.register_command(&m_mouse_scroll_command);
    commands.register_command(&m_scroll_left_command);
    commands.register_command(&m_scroll_right_command);
    commands.register_command(&m_scroll_up_command);
    commands.register_command(&m_scroll_down_command);
    commands.register_command(&m_zoom_in_command);
    commands.register_command(&m_zoom_out_command);
    commands.register_command(&m_grid_cycle_command);
    commands.bind_command_to_mouse_wheel(&m_free_zoom_command);
    commands.bind_command_to_mouse_drag (&m_mouse_scroll_command, erhe::toolkit::Mouse_button_right, true);
    commands.bind_command_to_key(&m_scroll_up_command,    erhe::toolkit::Key_w, false);
    commands.bind_command_to_key(&m_scroll_left_command,  erhe::toolkit::Key_a, false);
    commands.bind_command_to_key(&m_scroll_down_command,  erhe::toolkit::Key_s, false);
    commands.bind_command_to_key(&m_scroll_right_command, erhe::toolkit::Key_d, false);
    commands.bind_command_to_key(&m_zoom_in_command,      erhe::toolkit::Key_period, false);
    commands.bind_command_to_key(&m_zoom_out_command,     erhe::toolkit::Key_comma,  false);
    commands.bind_command_to_key(&m_grid_cycle_command,   erhe::toolkit::Key_g,  false);

    hide();
}

void Map_window::imgui()
{
    update_framebuffer();
    render();
    Framebuffer_window::imgui();
}

auto Map_window::flags() -> ImGuiWindowFlags
{
    return ImGuiWindowFlags_NoScrollbar;
}

void Map_window::on_begin()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
}

void Map_window::on_end()
{
    ImGui::PopStyleVar();
}

auto Map_window::want_keyboard_events() const -> bool
{
    return true;
}

auto Map_window::want_mouse_events() const -> bool
{
    return true;
}

auto Map_window::mouse_scroll_try_ready() const -> bool
{
    return m_is_hovered;
}

void Map_window::scroll(glm::vec2 delta)
{
    m_pixel_offset.x += delta.x; //* m_zoom;
    m_pixel_offset.y += delta.y; //* m_zoom;

    while (m_pixel_offset.x < 0.0f) {
        m_pixel_offset.x += Tile_shape::interleave_width * 2.0f * m_zoom;
        m_center_tile.x -= 2;
    }

    while (m_pixel_offset.y < 0.0f) {
        m_pixel_offset.y += Tile_shape::height * m_zoom;
        --m_center_tile.y;
    }

    while (m_pixel_offset.x >= Tile_shape::interleave_width * 2.0f * m_zoom) {
        m_pixel_offset.x -= Tile_shape::interleave_width * 2.0f * m_zoom;
        m_center_tile.x += 2;
    }

    while (m_pixel_offset.y >= Tile_shape::height * m_zoom) {
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
    if (m_grid > 2) {
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
    if (!m_texture) {
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
    if (m_map == nullptr) {
        return Tile_coordinate{0, 0};
    }

    const auto normalized         = normalize(pixel_coordinate);
    const auto view_relative_tile = m_pixel_lookup.pixel_to_tile(normalized);
    return wrap(view_relative_tile + m_center_tile);
}

void Map_window::scale_zoom(float scale)
{
    m_pixel_offset /= m_zoom;
    m_zoom *= scale;
    if (m_zoom < 1.0f) {
        m_zoom = 1.0f;
    }
    m_pixel_offset *= m_zoom;

    log_map_window->trace("scale_zoom(scale = {}) final zoom = {}", scale, m_zoom);
}

void Map_window::set_zoom(float scale)
{
    m_zoom = scale;
    if (m_zoom < 1.0f) {
        m_zoom = 1.0f;
    }
}

// static constexpr const char* c_grid_mode_strings[] = {
//     "Off",
//     "Grid 1",
//     "Grid 2"
// };

auto Map_window::tile_image(terrain_tile_t terrain_tile, const int scale) -> bool
{
    const auto& terrain_shapes = m_tile_renderer.get_terrain_shapes();
    if (terrain_tile >= terrain_shapes.size()) {
        return false;
    }

    const auto&     texel           = terrain_shapes.at(terrain_tile);
    const auto&     tileset_texture = m_tile_renderer.tileset_texture();
    const glm::vec2 uv0{
        static_cast<float>(texel.x) / static_cast<float>(tileset_texture->width()),
        static_cast<float>(texel.y) / static_cast<float>(tileset_texture->height()),
    };
    const glm::vec2 uv1 = uv0 + glm::vec2{
        static_cast<float>(Tile_shape::full_width) / static_cast<float>(tileset_texture->width()),
        static_cast<float>(Tile_shape::height) / static_cast<float>(tileset_texture->height()),
    };

    return m_imgui_renderer.image(
        tileset_texture,
        Tile_shape::full_width * scale,
        Tile_shape::height * scale,
        uv0,
        uv1,
        glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
        false
    );
}

void Map_window::set_map(Map* map)
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
    if (!m_texture) {
        return;
    }

    const float extent_x = static_cast<float>(m_texture->width ());
    const float extent_y = static_cast<float>(m_texture->height());

    if ((extent_x < 1.0f) || (extent_y < 1.0f)) {
        return;
    }

    const float center_pixel_x = std::floor(extent_x / 2.0f);
    const float center_pixel_y = std::floor(extent_y / 2.0f);

    const auto  grid_shape     = m_tile_renderer.get_extra_shape(Extra_tiles::grid_offset + std::max(0, m_grid - 1));
    const auto& terrain_shapes = m_tile_renderer.get_terrain_shapes();
    const auto& unit_shapes    = m_tile_renderer.get_unit_shapes();

    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, m_framebuffer->gl_name());
    gl::clear_color     (0.2f, 0.0f, 0.2f, 1.0f);
    gl::clear           (gl::Clear_buffer_mask::color_buffer_bit);

    m_tile_renderer.begin();
    coordinate_t half_width_in_tiles  = 2 + static_cast<coordinate_t>(std::ceil(extent_x / (Tile_shape::interleave_width * m_zoom)));
    coordinate_t half_height_in_tiles = 2 + static_cast<coordinate_t>(std::ceil(extent_y / (Tile_shape::height           * m_zoom)));
    for (coordinate_t vx = -half_width_in_tiles; vx < half_width_in_tiles; ++vx) {
        for (coordinate_t vy = -half_height_in_tiles; vy < half_height_in_tiles; ++vy) {
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
            m_tile_renderer.blit(
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

            if (unit_tile != 0) {
                const Pixel_coordinate& unit_shape = unit_shapes[unit_tile];
                //int player = unit_id / (Unit_group::width * Unit_group::height);
                m_tile_renderer.blit(
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

            if (m_grid > 0) {
                m_tile_renderer.blit(
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
            ) {
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

    m_tile_renderer.end();
    int width  = static_cast<int>(extent_x);
    int height = static_cast<int>(extent_y);
    m_tile_renderer.render(erhe::toolkit::Viewport{0, 0, width, height});
    m_text_renderer.render(erhe::toolkit::Viewport{0, 0, width, height});
    m_text_renderer.next_frame();

    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);
}

void Map_window::blit(
    const Pixel_coordinate shape,
    const Tile_coordinate  tile_location,
    const uint32_t         color
) const
{
    const auto pixel_position = tile_position(tile_location);
    m_tile_renderer.blit(
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
    const auto bounds = m_text_renderer.measure(text);
    m_text_renderer.print(
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
