#include "hextiles/map_window.hpp"
#include "hextiles/tiles.hpp"
#include "hextiles/map.hpp"
#include "hextiles/map_renderer.hpp"
#include "hextiles/pixel_lookup.hpp"
#include "hextiles/stream.hpp"

#include "configuration.hpp"
#include "editor_imgui_windows.hpp"
#include "editor_view.hpp"
#include "commands/command_context.hpp"
#include "graphics/gl_context_provider.hpp"
#include "renderers/programs.hpp"
#include "renderers/text_renderer.hpp"

#include "erhe/graphics/texture.hpp"
#include "erhe/graphics/framebuffer.hpp"

#include <imgui.h>

#include <gsl/assert>

#include <algorithm>

namespace hextiles
{

#pragma region commands
auto Map_free_zoom_command::try_call(editor::Command_context& context) -> bool
{
    const double v = context.relative_pointer().x + context.relative_pointer().y;
    const double k = 0.1;

    m_map_window.scale_zoom(static_cast<float>(1.0 + v * k));
    return true;
}

auto Map_mouse_scroll_command::try_call(editor::Command_context& context) -> bool
{
    if (state() == editor::State::Ready)
    {
        set_active(context);
    }

    if (state() != editor::State::Active)
    {
        return false;
    }

    m_map_window.scroll(
        glm::vec2{context.relative_pointer()}
    );
    return true;
}

void Map_mouse_scroll_command::try_ready(editor::Command_context& context)
{
    if (state() != editor::State::Inactive)
    {
        return;
    }

    set_ready(context);
}

auto Map_hover_command::try_call(editor::Command_context& context) -> bool
{
    m_map_window.hover(
        glm::vec2{context.absolute_pointer()}
    );
    return false;
}

auto Map_scroll_command::try_call(editor::Command_context& context) -> bool
{
    static_cast<void>(context);

    m_map_window.scroll(m_offset);
    return true;
}

auto Map_zoom_command::try_call(editor::Command_context& context) -> bool
{
    static_cast<void>(context);

    m_map_window.scale_zoom(m_scale);
    return true;
}
#pragma endregion commands


Map_window::Map_window()
    : erhe::components::Component{c_name}
    , Framebuffer_window         {c_name, c_title}
    , m_free_zoom_command        {*this}
    , m_mouse_scroll_command     {*this}
    , m_map_hover_command        {*this}
    , m_scroll_left_command      {*this, -4.0f,  0.0f }
    , m_scroll_right_command     {*this,  4.0f,  0.0f }
    , m_scroll_up_command        {*this,  0.0f, -4.0f }
    , m_scroll_down_command      {*this,  0.0f,  4.0f }
    , m_zoom_in_command          {*this, 0.5f}
    , m_zoom_out_command         {*this, 2.0f}
{
}

Map_window::~Map_window()
{
}

#pragma region Component
void Map_window::connect()
{
    require<editor::Editor_imgui_windows>();
    require<editor::Gl_context_provider>();
    require<editor::Programs>();
    m_editor_view   = get<editor::Editor_view  >();
    m_map_renderer  = get<Map_renderer         >();
    m_tiles         = get<Tiles                >();
    m_text_renderer = get<editor::Text_renderer>();
}

void Map_window::initialize_component()
{
    Framebuffer_window::initialize(
        *get<editor::Editor_imgui_windows>().get(),
        get<editor::Gl_context_provider>(),
        get<editor::Programs>()->textured.get()
    );

    m_pixel_lookup = std::make_unique<Pixel_lookup>();

    m_map = std::make_shared<Map>();

    File_read_stream file{"res/hextiles/map_new"};
    m_map->read(file);

    const auto view = get<editor::Editor_view>();
    view->register_command(&m_free_zoom_command);
    view->register_command(&m_mouse_scroll_command);
    view->register_command(&m_map_hover_command);

    view->register_command(&m_scroll_left_command);
    view->register_command(&m_scroll_right_command);
    view->register_command(&m_scroll_up_command);
    view->register_command(&m_scroll_down_command);
    view->register_command(&m_zoom_in_command);
    view->register_command(&m_zoom_out_command);

    view->bind_command_to_mouse_wheel (&m_free_zoom_command);
    view->bind_command_to_mouse_drag  (&m_mouse_scroll_command, erhe::toolkit::Mouse_button_right);
    view->bind_command_to_mouse_motion(&m_map_hover_command);

    view->bind_command_to_key(&m_scroll_left_command,  erhe::toolkit::Key_left,      false);
    view->bind_command_to_key(&m_scroll_right_command, erhe::toolkit::Key_right,     false);
    view->bind_command_to_key(&m_scroll_up_command,    erhe::toolkit::Key_up,        false);
    view->bind_command_to_key(&m_scroll_down_command,  erhe::toolkit::Key_down,      false);
    view->bind_command_to_key(&m_zoom_in_command,      erhe::toolkit::Key_page_down, false);
    view->bind_command_to_key(&m_zoom_out_command,     erhe::toolkit::Key_page_up,   false);
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

void Map_window::hover(glm::vec2 mouse_position)
{
    m_hover_pixel_position = mouse_position;
}

auto Map_window::mouse_scroll_try_ready() const -> bool
{
    return m_is_hovered;
}

void Map_window::scroll(glm::vec2 delta)
{
    m_pixel_offset.x += delta.x; //* m_zoom;
    m_pixel_offset.y += delta.y; //* m_zoom;

    //const float scaled_map_width  = m_map->width()  * Tile_shape::interleave_width * m_zoom;
    //const float scaled_map_height = m_map->height() * Tile_shape::height           * m_zoom;

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

    //update_view();
}

void Map_window::update_view()
{
    //const Pixel_coordinate view_center_pixel{
    //    static_cast<pixel_t>(m_pixel_position.x),
    //    static_cast<pixel_t>(m_pixel_position.y)
    //};
    //const auto view_relative_tile = m_pixel_lookup->pixel_to_tile(view_center_pixel);
    //m_center_tile = wrap_zero_to_one(view_relative_tile + m_center_tile);

    //const auto tile_centered_position = m_pixel_position + glm::vec2{Tile_shape::center_x, Tile_shape::center_y};
    //m_pixel_offset.x = std::fmodf(tile_centered_position.x, Tile_shape::interleave_width * 2.0f);
    //m_pixel_offset.y = std::fmodf(tile_centered_position.y, Tile_shape::height);
}

auto Map_window::get_size(glm::vec2 available_size) const -> glm::vec2
{
    return available_size;
}

auto Map_window::wrap_zero_to_one(Tile_coordinate in) const -> Tile_coordinate
{
    return m_map->wrap_zero_to_one(in);
}

auto Map_window::wrap_minus_half_to_half(Tile_coordinate in) const -> Tile_coordinate
{
    return m_map->wrap_minus_half_to_half(in);
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
    //return (a >= 0.0f) ? static_cast<pixel_t>(a) : static_cast<pixel_t>(a) - 1;
    //return (a >= 0.0f) ? static_cast<int>(a) : static_cast<int>(a) - 1;
}

auto Map_window::normalize(Pixel_coordinate pixel_coordinate) const -> Pixel_coordinate
{
    const float extent_x       = static_cast<float>(m_texture->width ());
    const float extent_y       = static_cast<float>(m_texture->height());
    const float center_pixel_x = extent_x / 2.0f;
    const float center_pixel_y = extent_y / 2.0f;
    const float normalized_x   = pixel_coordinate.x - center_pixel_x + m_pixel_offset.x;
    const float normalized_y   = pixel_coordinate.y - center_pixel_y + m_pixel_offset.y;

    return Pixel_coordinate{
        pixel_round(normalized_x / m_zoom),
        pixel_round(normalized_y / m_zoom)
    };
}

auto Map_window::pixel_to_tile(Pixel_coordinate pixel_coordinate) const -> Tile_coordinate
{
    const auto normalized         = normalize(pixel_coordinate);
    const auto view_relative_tile = m_pixel_lookup->pixel_to_tile(normalized);
    return wrap_zero_to_one(view_relative_tile + m_center_tile);
}

#if 0
auto Map_window::tile_center_to_pixel(Tile_coordinate tile_coordinate) const -> Pixel_coordinate
{
    const auto  view_relative_tile = wrap_minus_half_to_half(tile_coordinate - m_center_tile);
    const float y_offset           = tile_coordinate.is_odd() ? 0.0f : Tile_shape::odd_y_offset;

    return Pixel_coordinate{
        m_pixel_offset.x + (view_relative_tile.x * m_scaled_tile_width),
        m_pixel_offset.y + (view_relative_tile.y * m_scaled_tile_height)
    };
}

auto Map_window::tile_top_left_to_pixel(Tile_coordinate tile_coordinate) const -> Pixel_coordinate
{
    return
        tile_center_to_pixel(tile_coordinate) -
        Pixel_coordinate{
            Tile_shape::full_width / 2,
            Tile_shape::height     / 2
        };
}
#endif

void Map_window::scale_zoom(float scale)
{
    m_pixel_offset /= m_zoom;
    m_zoom *= scale;
    if (m_zoom < 1.0f)
    {
        m_zoom = 1.0f;
    }
    m_pixel_offset *= m_zoom;

    update_view();
}

void Map_window::set_zoom(float scale)
{
    m_zoom = scale;
    if (m_zoom < 1.0f)
    {
        m_zoom = 1.0f;
    }

    update_view();
}

Map_tool_window::Map_tool_window()
    : erhe::components::Component{c_name}
    , Imgui_window               {c_name, c_title}
{
}

Map_tool_window::~Map_tool_window()
{
}

void Map_tool_window::connect()
{
    m_map_window = get<Map_window>();
}

void Map_tool_window::initialize_component()
{
    get<editor::Editor_imgui_windows>()->register_imgui_window(this);
}

void Map_tool_window::imgui()
{
    m_map_window->tool_imgui();
}

static constexpr const char* c_grid_mode_strings[] =
{
    "Off",
    "Grid 1",
    "Grid 2",
    "Grid 3",
    "Grid 4",
    "Grid 5"
};

void Map_window::tool_imgui()
{
    ImGui::Combo("Grid", &m_grid, c_grid_mode_strings, IM_ARRAYSIZE(c_grid_mode_strings));
    //ImGui::Text(
    //    "Pixel position: %.2f, %.2f",
    //    m_pixel_position.x / m_zoom,
    //    m_pixel_position.y / m_zoom
    //);
    ImGui::Text(
        "Center tile: %d, %d",
        static_cast<int>(m_center_tile.x),
        static_cast<int>(m_center_tile.y)
    );
    ImGui::Text(
        "Pixel offset: %.2f, %.2f",
        m_pixel_offset.x,
        m_pixel_offset.y
    );
    ImGui::Text("Zoom: %.2f", m_zoom);
    if (m_hover_pixel_position.has_value())
    {
        const auto& root_position = m_hover_pixel_position.value();
        ImGui::Text("Hover root pixel position: %.1f, %.1f", root_position.x, root_position.y);

        auto mouse_input_sink = m_editor_view->mouse_input_sink();
        if (mouse_input_sink != nullptr)
        {
            ImGui::Text("Mouse window: %s", mouse_input_sink->title().data());
        }

        if (mouse_input_sink == this)
        {
            auto window_position = m_editor_view->to_window(root_position);
            ImGui::Text("Hover window mouse position: %.1f, %.1f", window_position.x, window_position.y);

            const Pixel_coordinate pixel_coordinate{static_cast<pixel_t>(window_position.x), static_cast<pixel_t>(window_position.y)};
            const auto normalized         = normalize(pixel_coordinate);
            const auto lut_xy             = m_pixel_lookup->lut_xy(normalized);
            const auto lut                = m_pixel_lookup->lut_value(normalized);
            const auto view_relative_tile = m_pixel_lookup->pixel_to_tile(normalized);
            ImGui::Text("Normalized pixel coordinate: %d, %d", normalized.x, normalized.y);
            ImGui::Text("LUT xy: %d, %d", lut_xy.x, lut_xy.y);
            ImGui::Text("LUT value: %d, %d", lut.x, lut.y);
            ImGui::Text("View relative tile: %d, %d", view_relative_tile.x, view_relative_tile.y);

            const auto tile_position = pixel_to_tile(
                Pixel_coordinate{
                    static_cast<pixel_t>(window_position.x),
                    static_cast<pixel_t>(window_position.y)
                }
            );

            //ImGui::Text("Tile in window @ %d, %d", tile_position_in_window.x, tile_position_in_window.y);
            //
            //const auto tile_position = m_map->wrap(tile_position_in_window + m_tile_offset);

            ImGui::Text("Tile @ %d, %d", tile_position.x, tile_position.y);

            const terrain_t terrain = m_map->get_terrain(tile_position);

            const auto& terrain_shapes = m_tiles->get_terrain_shapes();
            if (terrain < terrain_shapes.size())
            {
                ImGui::Text("Tile: %d", static_cast<int>(terrain));

                const auto& terrain_type = m_tiles->get_terrain_type(terrain);

                ImGui::Text("Tile: %s", terrain_type.name.c_str());
            }
        }
    }
}

auto get_color(bool x_odd, bool y_odd) -> uint32_t
{
    const uint32_t blue {0xffff0000u};
    const uint32_t green{0xff00ff00u};
    const uint32_t red  {0xff0000ffu};
    const uint32_t white{0xffffffffu};
    return
        x_odd
            ? (y_odd ? blue : green)
            : (y_odd ? red  : white);
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

    const auto  grid_shape     = m_tiles->get_grid_shape(std::max(0, m_grid - 1));
    const auto& terrain_shapes = m_tiles->get_terrain_shapes();

    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, m_framebuffer->gl_name());
    gl::clear_color     (0.2f, 0.0f, 0.2f, 1.0f);
    gl::clear           (gl::Clear_buffer_mask::color_buffer_bit);

    m_map_renderer->begin();
    coordinate_t half_width_in_tiles  = 2 + static_cast<coordinate_t>(std::ceil(extent_x / (Tile_shape::interleave_width * m_zoom)));
    coordinate_t half_height_in_tiles = 2 + static_cast<coordinate_t>(std::ceil(extent_y / (Tile_shape::height           * m_zoom)));
    for (coordinate_t vx = -half_width_in_tiles; vx < half_width_in_tiles; ++vx)
    {
        for (coordinate_t vy = -half_height_in_tiles; vy < half_height_in_tiles; ++vy)
        {
            const auto  absolute_tile        = wrap_zero_to_one(m_center_tile + Tile_coordinate{vx, vy});
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

            const uint32_t color   = 0xffffffffu; //get_color((absolute_tile.x & 1) == 1, (absolute_tile.y & 1) == 0);
            terrain_t      terrain = m_map->get_terrain(absolute_tile);
            if (terrain >= terrain_shapes.size())
            {
                terrain = 0;
            }
            const Pixel_coordinate& shape = terrain_shapes[terrain];

            m_map_renderer->blit(
                shape.x,
                shape.y,
                Tile_shape::full_width,
                Tile_shape::height,
                x0,
                y0,
                x1,
                y1,
                color
            );
            if (m_grid > 0)
            {
                m_map_renderer->blit(
                    grid_shape.x,
                    grid_shape.y,
                    Tile_shape::full_width,
                    Tile_shape::height,
                    x0,
                    y0,
                    x1,
                    y1,
                    0x88888888u
                );
            }

            //if (
            //    (vx > -10) &&
            //    (vx <  10) &&
            //    (vy > -10) &&
            //    (vy <  10)
            //)
            //{
            //    std::string text = fmt::format("{}, {}", absolute_tile.x, absolute_tile.y);
            //    static const float z = -0.5f;
            //    const auto bounds = m_text_renderer->measure(text);
            //    m_text_renderer->print(
            //        glm::vec3{
            //            x0 + Tile_shape::center_x * m_zoom - bounds.half_size().x,
            //            y0 + Tile_shape::center_y * m_zoom - bounds.half_size().y,
            //            z
            //        },
            //        0xffffffffu,
            //        text
            //    );
            //}
        }
    }

#if 0
    m_map_renderer->begin();
    for (coordinate_t tx = tx0 ; tx < tx1; ++tx)
    {
        const coordinate_t x        = tx + m_center_tile.x;
        const float        x0       = m_pixel_offset.x + static_cast<float>(tx * m_scaled_tile_width);
        const float        x1       = x0 + Tile_shape::full_width * m_zoom;
        const bool         x_odd    = (x & 1) == 1;
        const float        y_offset = x_odd ? Tile_shape::odd_y_offset * m_zoom : 0.0f;
        for (coordinate_t ty = ty0; ty < ty1; ++ty)
        {
            const coordinate_t    y        = ty + m_center_tile.y;
            const Tile_coordinate pos      = m_map->wrap_zero_to_one(Tile_coordinate{x, y});
            const bool            y_odd   = (y & 1) == 1;
            const uint32_t        color   = get_color(x_odd, y_odd);
            terrain_t             terrain = m_map->get_terrain(pos);
            if (terrain >= terrain_shapes.size())
            {
                terrain = 0;
            }
            const Pixel_coordinate& shape = terrain_shapes[terrain];
            const int   src_x = shape.x;
            const int   src_y = shape.y;
            const float y0    = m_pixel_offset.y + static_cast<float>(ty * m_scaled_tile_height) + y_offset;
            const float y1    = y0 + Tile_shape::height * m_zoom;
            m_map_renderer->blit(
                src_x,
                src_y,
                Tile_shape::full_width,
                Tile_shape::height,
                x0,
                y0,
                x1,
                y1,
                color
            );
            if (m_grid > 0)
            {
                m_map_renderer->blit(
                    grid_shape.x,
                    grid_shape.y,
                    Tile_shape::full_width,
                    Tile_shape::height,
                    x0,
                    y0,
                    x1,
                    y1,
                    0xffffffffu
                );
            }
        }
    }
#endif

    m_map_renderer->end();

    erhe::scene::Viewport viewport
    {
        .x             = 0,
        .y             = 0,
        .width         = static_cast<int>(extent_x),
        .height        = static_cast<int>(extent_y),
        .reverse_depth = get<editor::Configuration>()->reverse_depth

    };
    m_map_renderer->render(viewport);

    m_text_renderer->render(viewport);
    m_text_renderer->next_frame();

    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);

    //for (auto& entry : m_text_entries)
    //{
    //    renderer.draw_text(entry.x, entry.y, entry.text.c_str());
    //}
    //m_text_entries.clear();
}

} // namespace hextiles
