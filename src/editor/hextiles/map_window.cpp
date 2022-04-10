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
#include "renderers/imgui_renderer.hpp"
#include "renderers/programs.hpp"
#include "renderers/text_renderer.hpp"

#include "erhe/graphics/texture.hpp"
#include "erhe/graphics/framebuffer.hpp"

#include "log.hpp"

#include <imgui.h>

#include <gsl/assert>

#include <algorithm>
#include <functional>

namespace hextiles
{

#pragma region commands
auto Map_free_zoom_command::try_call(editor::Command_context& context) -> bool
{
    const double v = context.relative_wheel().x + context.relative_wheel().y;
    const double k = 0.1;

    const auto window_position = context.editor_view().to_window_top_left(context.absolute_pointer());
    m_map_window.hover(
        window_position
    );

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

    const auto window_position = context.editor_view().to_window_top_left(context.absolute_pointer());
    m_map_window.hover(
        window_position
    );
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

auto Map_primary_brush_command::try_call(editor::Command_context& context) -> bool
{
    if (state() == editor::State::Ready)
    {
        set_active(context);
    }

    if (state() != editor::State::Active)
    {
        return false;
    }

    const auto window_position = context.editor_view().to_window_top_left(context.absolute_pointer());
    m_map_window.primary_brush(window_position);
    return true;
}

void Map_primary_brush_command::try_ready(editor::Command_context& context)
{
    if (state() != editor::State::Inactive)
    {
        return;
    }

    // TODO only set ready when hovering over map
    set_ready(context);
    const auto window_position = context.editor_view().to_window_top_left(context.absolute_pointer());
    m_map_window.primary_brush(window_position);
}

auto Map_hover_command::try_call(editor::Command_context& context) -> bool
{
    const auto window_position = context.editor_view().to_window_top_left(context.absolute_pointer());
    m_map_window.hover(
        window_position
    );
    return false;
}

auto Map_scroll_command::try_call(editor::Command_context& context) -> bool
{
    static_cast<void>(context);

    m_map_window.scroll_tiles(m_offset);
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
    , m_map_primary_brush_command{*this}
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
    view->register_command(&m_map_primary_brush_command);

    view->register_command(&m_scroll_left_command);
    view->register_command(&m_scroll_right_command);
    view->register_command(&m_scroll_up_command);
    view->register_command(&m_scroll_down_command);
    view->register_command(&m_zoom_in_command);
    view->register_command(&m_zoom_out_command);

    view->bind_command_to_mouse_wheel (&m_free_zoom_command);
    view->bind_command_to_mouse_drag  (&m_mouse_scroll_command, erhe::toolkit::Mouse_button_right);
    view->bind_command_to_mouse_motion(&m_map_hover_command);
    //view->bind_command_to_mouse_click (&m_map_primary_brush_command, erhe::toolkit::Mouse_button_left);
    view->bind_command_to_mouse_drag  (&m_map_primary_brush_command, erhe::toolkit::Mouse_button_left);

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

void Map_window::hover(glm::vec2 window_position)
{
    m_hover_window_position = window_position;

    Pixel_coordinate hover_pixel_position{
        static_cast<pixel_t>(window_position.x),
        static_cast<pixel_t>(window_position.y)
    };
    m_hover_tile_position = pixel_to_tile(hover_pixel_position);
}

void Map_window::primary_brush(glm::vec2 mouse_position)
{
    if (!m_texture)
    {
        return;
    }
    const auto pixel_position = Pixel_coordinate{
        static_cast<pixel_t>(mouse_position.x),
        static_cast<pixel_t>(mouse_position.y)
    };
    const auto tile_position = pixel_to_tile(pixel_position);

    std::function<void(Tile_coordinate)> set_terrain_op =
    [this] (Tile_coordinate position) -> void
    {
        m_map->set_terrain(position, m_left_brush);
    };

    std::function<void(Tile_coordinate)> update_group_terrain =
    [this] (Tile_coordinate position) -> void
    {
        terrain_t terrain_ = m_map->get_terrain(position);
        terrain_t terrain = m_tiles->get_base_terrain(terrain_);
        const auto& terrain_type = m_tiles->get_terrain_type(terrain);
        int group = terrain_type.group;
        if (group < 0)
        {
            return;
        }

        uint32_t neighbor_mask;
        bool promote;
        bool demote;
        size_t counter = 0U;
        do
        {
            promote = false;
            demote = false;
            const Multi_shape& multishape = gsl::at(multishapes, group);

            neighbor_mask = 0U;
            for (
                direction_t direction = direction_first;
                direction <= direction_last;
                ++direction
            )
            {
                const Tile_coordinate neighbor_position = m_map->neighbor(position, direction);
                const terrain_t neighbor_terrain_ = m_map->get_terrain(neighbor_position);
                const terrain_t neighbor_terrain = m_tiles->get_base_terrain(neighbor_terrain_);
                const int neighbor_group = m_tiles->get_terrain_type(neighbor_terrain).group;
                if (
                    (neighbor_group == group) ||
                    (
                        (multishape.link_group >= 0) &&
                        (neighbor_group == multishape.link_group)
                    ) ||
                    (
                        (multishape.link_group2 >= 0) &&
                        (neighbor_group == multishape.link_group2)
                    ) ||
                    (
                        (multishape.link_first >= 0) &&
                        (multishape.link_last >= 0) &&
                        (neighbor_terrain >= multishape.link_first) &&
                        (neighbor_terrain <= multishape.link_last)
                    ) ||
                    (
                        (multishape.link2_first >= 0) &&
                        (multishape.link2_last >= 0) &&
                        (neighbor_terrain >= multishape.link2_first) &&
                        (neighbor_terrain <= multishape.link2_last)
                    )
                )
                {
                    neighbor_mask = neighbor_mask | (1U << direction);
                }
                if (
                    (multishape.demoted >= 0) &&
                    (neighbor_group != group) &&
                    (neighbor_group != multishape.demoted))
                {
                    demote = true;
                }
            }
            promote = (neighbor_mask == direction_mask_all) && (multishape.promoted >= 0);
            Expects((promote && demote) == false);
            if (promote)
            {
                group = multishape.promoted;
            }
            else if (demote)
            {
                group = multishape.demoted;
            }
        }
        while (
            (promote || demote) &&
            (++counter < 2U)
        );

        terrain_t updated_terrain = m_tiles->get_multishape_terrain(group, neighbor_mask);
        m_map->set_terrain(position, updated_terrain);
    };

    m_map->hex_circle(tile_position, 0, m_brush_size - 1, set_terrain_op);
    m_map->hex_circle(tile_position, 0, m_brush_size + 1, update_group_terrain);

    //m_map->set_terrain(tile_position, m_left_brush);
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

    log_map_window.trace("scale_zoom(scale = {}) final zoom = {}\n", scale, m_zoom);
}

void Map_window::set_zoom(float scale)
{
    m_zoom = scale;
    if (m_zoom < 1.0f)
    {
        m_zoom = 1.0f;
    }
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

Terrain_palette_window::Terrain_palette_window()
    : erhe::components::Component{c_name}
    , Imgui_window               {c_name, c_title}
{
}

Terrain_palette_window::~Terrain_palette_window()
{
}

void Terrain_palette_window::connect()
{
    m_map_window = get<Map_window>();
}

void Terrain_palette_window::initialize_component()
{
    get<editor::Editor_imgui_windows>()->register_imgui_window(this);
}

void Terrain_palette_window::imgui()
{
    m_map_window->terrain_palette_imgui();
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

auto Map_window::tile_image(terrain_t terrain, const int scale) -> bool
{
    const auto& terrain_shapes = m_tiles->get_terrain_shapes();
    if (terrain >= terrain_shapes.size())
    {
        return false;
    }

    const auto&     texel           = terrain_shapes.at(terrain);
    const auto&     tileset_texture = m_map_renderer->tileset_texture();
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

void Map_window::terrain_palette()
{
    const auto& terrain_type = m_tiles->get_terrain_type(m_left_brush);
    tile_image(m_left_brush, 3);
    ImGui::SameLine();
    ImGui::Text("%s", terrain_type.name.c_str());

    terrain_t terrain = 0;
    for (int ty = 0; ty < Base_tiles::height; ++ty)
    {
        for (int tx = 0; tx < Base_tiles::width; ++tx)
        {
            const bool pressed = tile_image(terrain, 2);
            if (pressed)
            {
                m_left_brush = terrain;
            }
            ++terrain;
            if (tx + 1< Base_tiles::width)
            {
                ImGui::SameLine();
            }
        }
    }
}

void Map_window::tile_info(const Tile_coordinate tile_position)
{
    const terrain_t terrain = m_map->get_terrain(tile_position);

    const auto& terrain_shapes = m_tiles->get_terrain_shapes();
    if (terrain >= terrain_shapes.size())
    {
        return;
    }
    const auto base_terrain = m_tiles->get_base_terrain(terrain);
    if (base_terrain >= terrain_shapes.size())
    {
        return;
    }

    const auto& terrain_type = m_tiles->get_terrain_type(base_terrain);

    ImGui::Text("Tile @ %d, %d %s", tile_position.x, tile_position.y, terrain_type.name.c_str());

    tile_image(terrain, 3);
    ImGui::SameLine();
    tile_image(base_terrain, 3);
}

void Map_window::terrain_palette_imgui()
{
    terrain_palette();
}

void Map_window::tool_imgui()
{
    ImGui::Combo("Grid", &m_grid, c_grid_mode_strings, IM_ARRAYSIZE(c_grid_mode_strings));
    //ImGui::Text(
    //    "Center tile: %d, %d",
    //    static_cast<int>(m_center_tile.x),
    //    static_cast<int>(m_center_tile.y)
    //);
    //ImGui::Text(
    //    "Pixel offset: %.2f, %.2f",
    //    m_pixel_offset.x,
    //    m_pixel_offset.y
    //);
    ImGui::Text("Zoom: %.2f", m_zoom);
    //ImGui::Text("center tile: %d, %d",      m_center_tile.x,  m_center_tile.y);
    //ImGui::Text("pixel offset: %.2f, %.2f", m_pixel_offset.x, m_pixel_offset.y);
    if (!m_hover_window_position.has_value())
    {
        return;
    }

    auto mouse_input_sink = m_editor_view->mouse_input_sink();
    if (mouse_input_sink != this)
    {
        return;
    }
    const auto window_position = m_hover_window_position.value();
    //ImGui::Text("window top left: %.1f, %.1f", window_position.x, window_position.y);
    //const Pixel_coordinate pixel_coordinate{
    //    static_cast<pixel_t>(window_position.x),
    //    static_cast<pixel_t>(window_position.y)
    //};

#if 0
    {
        ImGui::Text("Pixel coordinate %d, %d", pixel_coordinate.x, pixel_coordinate.y);
        const float extent_x       = static_cast<float>(m_texture->width ());
        const float extent_y       = static_cast<float>(m_texture->height());
        ImGui::Text("Extent %.1f, %.1f", extent_x, extent_y);
        const float center_pixel_x = std::floor(extent_x / 2.0f);
        const float center_pixel_y = std::floor(extent_y / 2.0f);
        ImGui::Text("Center pixel: %.1f, %.1f", center_pixel_x, center_pixel_y);
        const float pix_minus_center_x   = pixel_coordinate.x - center_pixel_x;
        const float pix_minus_center_y   = pixel_coordinate.y - center_pixel_y;
        ImGui::Text("pixel coordinat - center: %.1f, %.1f", pix_minus_center_x, pix_minus_center_y);
        const float pix_minus_offset_x   = pixel_coordinate.x + m_pixel_offset.x;
        const float pix_minus_offset_y   = pixel_coordinate.y + m_pixel_offset.y;
        ImGui::Text("pixel coordinat + offset: %.1f, %.1f", pix_minus_offset_x, pix_minus_offset_y);
        const float normalized_x   = pixel_coordinate.x - center_pixel_x + m_pixel_offset.x;
        const float normalized_y   = pixel_coordinate.y - center_pixel_y + m_pixel_offset.y;
        ImGui::Text("Normalized w. zoom: %.1f, %.1f", normalized_x, normalized_y);

        const Pixel_coordinate norm{
            pixel_round(normalized_x / m_zoom),
            pixel_round(normalized_y / m_zoom)
        };
        ImGui::Text("Norm: %d, %d", norm.x, norm.y);
    }
#endif

    //const auto normalized         = normalize(pixel_coordinate);
    //const auto lut_xy             = m_pixel_lookup->lut_xy(normalized);
    //const auto lut                = m_pixel_lookup->lut_value(normalized);
    //const auto view_relative_tile = m_pixel_lookup->pixel_to_tile(normalized);
    //ImGui::Text("Normalized pixel coordinate: %d, %d", normalized.x, normalized.y);
    //ImGui::Text("LUT xy: %d, %d", lut_xy.x, lut_xy.y);
    //ImGui::Text("LUT value: %d, %d", lut.x, lut.y);
    //ImGui::Text("View relative tile: %d, %d", view_relative_tile.x, view_relative_tile.y);

    const auto tile_position = pixel_to_tile(
        Pixel_coordinate{
            static_cast<pixel_t>(window_position.x),
            static_cast<pixel_t>(window_position.y)
        }
    );
    tile_info(tile_position);
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

    const auto  grid_shape     = m_tiles->get_grid_shape(std::max(0, m_grid - 1));
    const auto& terrain_shapes = m_tiles->get_terrain_shapes();

    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, m_framebuffer->gl_name());
    gl::clear_color     (0.2f, 0.0f, 0.2f, 1.0f);
    gl::clear           (gl::Clear_buffer_mask::color_buffer_bit);

    m_map_renderer->begin();
#if 1
    coordinate_t half_width_in_tiles  = 2 + static_cast<coordinate_t>(std::ceil(extent_x / (Tile_shape::interleave_width * m_zoom)));
    coordinate_t half_height_in_tiles = 2 + static_cast<coordinate_t>(std::ceil(extent_y / (Tile_shape::height           * m_zoom)));
    for (coordinate_t vx = -half_width_in_tiles; vx < half_width_in_tiles; ++vx)
    {
        for (coordinate_t vy = -half_height_in_tiles; vy < half_height_in_tiles; ++vy)
        {
#else
    for (coordinate_t vx = -4; vx < 5; ++vx)
    {
        for (coordinate_t vy = -4; vy < 5; ++vy)
        {
#endif
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

#if 1
    if (
        m_hover_tile_position.has_value() &&
        m_left_brush < terrain_shapes.size()
    )
    {
        const auto tile = m_hover_tile_position.value();
        const Pixel_coordinate& shape = terrain_shapes[m_left_brush];
        const auto position = tile_position(tile);
        m_map_renderer->blit(
            shape.x,
            shape.y,
            Tile_shape::full_width,
            Tile_shape::height,
            position.x,
            position.y,
            position.x + Tile_shape::full_width * m_zoom,
            position.y - Tile_shape::height * m_zoom,
            0x88888888u
        );
        std::string text = fmt::format("{}, {}", tile.x, tile.y);
        static const float z = -0.5f;
        const auto bounds = m_text_renderer->measure(text);
        m_text_renderer->print(
            glm::vec3{
                position.x + Tile_shape::center_x * m_zoom - bounds.half_size().x,
                position.y - Tile_shape::center_y * m_zoom - bounds.half_size().y,
                z
            },
            0xffffffffu,
            text
        );

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
