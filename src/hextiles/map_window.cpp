#pragma warning( disable : 4701 )

#include "map_window.hpp"
#include "log.hpp"
#include "map.hpp"
#include "map_renderer.hpp"
#include "menu_window.hpp"
#include "pixel_lookup.hpp"
#include "stream.hpp"
#include "tiles.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui_windows.hpp"
#include "erhe/application/view.hpp"
#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"

#include "erhe/application/renderers/imgui_renderer.hpp"
#include "erhe/application/renderers/text_renderer.hpp"

#include "erhe/graphics/texture.hpp"
#include "erhe/graphics/framebuffer.hpp"

#include <imgui.h>

#include <glm/gtc/noise.hpp>
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

    const auto window_position = context.view().to_window_top_left(context.get_vec2_absolute_value());
    m_map_window.hover(
        window_position
    );

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

    const auto window_position = context.view().to_window_top_left(context.get_vec2_absolute_value());
    m_map_window.hover(
        window_position
    );
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

auto Map_primary_brush_command::try_call(erhe::application::Command_context& context) -> bool
{
    if (state() == erhe::application::State::Ready)
    {
        set_active(context);
    }

    if (state() != erhe::application::State::Active)
    {
        return false;
    }

    const auto window_position = context.view().to_window_top_left(context.get_vec2_absolute_value());
    m_map_window.primary_brush(window_position);
    return true;
}

void Map_primary_brush_command::try_ready(erhe::application::Command_context& context)
{
    if (state() != erhe::application::State::Inactive)
    {
        return;
    }

    // TODO only set ready when hovering over map
    set_ready(context);
    const auto window_position = context.view().to_window_top_left(context.get_vec2_absolute_value());
    m_map_window.primary_brush(window_position);
}

auto Map_hover_command::try_call(erhe::application::Command_context& context) -> bool
{
    const auto window_position = context.view().to_window_top_left(context.get_vec2_absolute_value());
    m_map_window.hover(
        window_position
    );
    return false;
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
    require<erhe::application::Imgui_windows      >();
    require<erhe::application::Gl_context_provider>();
    require<erhe::application::Imgui_renderer     >(); // required for Imgui_window

    m_editor_view   = get    <erhe::application::View         >();
    m_map_renderer  = get    <Map_renderer                    >();
    m_tiles         = require<Tiles                           >();
    m_text_renderer = get    <erhe::application::Text_renderer>();
}

void Map_window::initialize_component()
{
    Framebuffer_window::initialize(
        *m_components
    );

    m_pixel_lookup = std::make_unique<Pixel_lookup>();

    update_elevation_terrains();

    m_map = std::make_shared<Map>();

    File_read_stream file{"res/hextiles/map_new"};
    m_map->read(file);

    const auto view = get<erhe::application::View>();
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

void Map_window::update_group_terrain(Tile_coordinate position)
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
        const Terrain_group& terrain_group = m_tiles->get_terrain_group(group);

        neighbor_mask = 0U;
        for (
            direction_t direction = direction_first;
            direction <= direction_last;
            ++direction
        )
        {
            const Tile_coordinate neighbor_position = m_map->neighbor(position, direction);
            const terrain_t       neighbor_terrain_ = m_map->get_terrain(neighbor_position);
            const terrain_t       neighbor_terrain  = m_tiles->get_base_terrain(neighbor_terrain_);
            const int neighbor_group = m_tiles->get_terrain_type(neighbor_terrain).group;
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
                    (terrain_group.link_first[0] >= 0) &&
                    (terrain_group.link_last[0] >= 0) &&
                    (neighbor_terrain >= terrain_group.link_first[0]) &&
                    (neighbor_terrain <= terrain_group.link_last[0])
                ) ||
                (
                    (terrain_group.link_first[1] >= 0) &&
                    (terrain_group.link_last[1] >= 0) &&
                    (neighbor_terrain >= terrain_group.link_first[1]) &&
                    (neighbor_terrain <= terrain_group.link_last[1])
                )
            )
            {
                neighbor_mask = neighbor_mask | (1U << direction);
            }
            if (
                (terrain_group.demoted >= 0) &&
                (neighbor_group != group) &&
                (neighbor_group != terrain_group.demoted))
            {
                demote = true;
            }
        }
        promote = (neighbor_mask == direction_mask_all) && (terrain_group.promoted >= 0);
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

    terrain_t updated_terrain = m_tiles->get_terrain_group_shape(group, neighbor_mask);
    m_map->set_terrain(position, updated_terrain);
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

    std::function<void(Tile_coordinate)> update_op =
    [this] (Tile_coordinate position) -> void
    {
        update_group_terrain(position);
    };

    m_map->hex_circle(tile_position, 0, m_brush_size - 1, set_terrain_op);
    m_map->hex_circle(tile_position, 0, m_brush_size + 1, update_op);

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
    m_map_window     = get<Map_window>();
    m_imgui_renderer = get<erhe::application::Imgui_renderer>();
}

void Map_tool_window::initialize_component()
{
    Expects(m_components != nullptr);
    Imgui_window::initialize(*m_components);
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
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
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
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
    auto& terrain_type = m_tiles->get_terrain_type(m_left_brush);
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
    ImGui::Text("Terrain: %d, base: %d", terrain, base_terrain);
    ImGui::Text("City Size: %d", terrain_type.city_size);

    tile_image(terrain, 3);
    ImGui::SameLine();
    tile_image(base_terrain, 3);

    const int distance = m_map->distance(tile_position, Tile_coordinate{0, 0});
    ImGui::Text("Distance to 0,0: %d", distance);
}

void Map_window::terrain_palette_imgui()
{
    terrain_palette();
}

auto Map_window::get_map() const -> const std::shared_ptr<Map>&
{
    return m_map;
}

#pragma region Fbm_noise
void Fbm_noise::prepare()
{
    const float gain  = std::abs(m_gain);
    float amp         = gain;
    float amp_fractal = 1.0f;
    for (int i = 1; i < m_octaves; i++)
    {
        amp_fractal += amp;
        amp *= gain;
    }
    m_bounding = 1.0f / amp_fractal;
}

void Fbm_noise::imgui()
{
    ImGui::DragInt   ("Octaves",    &m_octaves,     0.1f,     1,         9);
    ImGui::DragFloat ("Frequency",  &m_frequency,   0.1f,     0.001f,   10.0f);
    ImGui::DragFloat ("Lacunarity", &m_lacunarity,  0.1f,     0.001f,   10.0f);
    ImGui::DragFloat ("Gain",       &m_gain,        0.1f,     0.001f,    1.0f);
    ImGui::DragFloat2("Scale",      &m_scale[0],    0.1f,     0.001f,    1.0f);
    ImGui::DragFloat2("Location",   &m_location[0], 0.1f, -1000.0f,   1000.0f);
}

auto Fbm_noise::generate(float s, float t, glm::vec4 seed) -> float
{
    float x = m_location[0] + std::cos(s * glm::two_pi<float>()) * m_scale[0];
    float y = m_location[1] + std::cos(t * glm::two_pi<float>()) * m_scale[1];
    float z = m_location[0] + std::sin(s * glm::two_pi<float>()) * m_scale[0];
    float w = m_location[1] + std::sin(t * glm::two_pi<float>()) * m_scale[1];

    return generate(x, y, z, w, seed);
}

auto Fbm_noise::generate(float x, float y, float z, float w, glm::vec4 seed) -> float
{
    float sum = 0;
    float amp = m_bounding;

    for (int i = 0; i < m_octaves; i++)
    {
        float noise = glm::simplex(seed + glm::vec4{x, y, z, w});
        sum += noise * amp;

        x *= m_lacunarity;
        y *= m_lacunarity;
        z *= m_lacunarity;
        w *= m_lacunarity;
        amp *= m_gain;
    }

    return sum;
}
#pragma endregion Fbm_noise

#pragma region Variations
void Variations::reset(size_t count)
{
    m_values.reserve(count);
    m_values.clear();
    m_min_value = std::numeric_limits<float>::max();
    m_max_value = std::numeric_limits<float>::lowest();
}

void Variations::push(float value)
{
    m_values.push_back(value);
    m_min_value = std::min(m_min_value, value);
    m_max_value = std::max(m_max_value, value);
}

auto Variations::get_noise_value(size_t index) const -> float
{
    Expects(index < m_values.size());
    return m_values[index];
}

auto Variations::normalize()
{
    float extent = m_max_value - m_min_value;

    // normalize values to 0..1
    for (auto& value : m_values)
    {
        value = (value - m_min_value) / extent;
    }

    float sum = 0.0f;
    for (const auto& entry : m_terrains)
    {
        sum += entry.ratio;
    }
    if (sum > 0.0f)
    {
        for (auto& entry : m_terrains)
        {
            entry.normalized_ratio = entry.ratio / sum;
        }
    }
    else
    {
        for (auto& entry : m_terrains)
        {
            entry.normalized_ratio = entry.ratio;
        }
    }
}

void Variations::compute_threshold_values()
{
    // Once all noise values have been generated, we
    // find threshold values.

    normalize();

    // Copy and sort
    std::vector<float> sorted_values = m_values;
    std::sort(
        sorted_values.begin(),
        sorted_values.end()
    );

    // Locate threshold values
    float offset{0.0f};
    size_t count = m_values.size();
    for (auto& terrain : m_terrains)
    {
        const float  normalized_ratio = std::min(1.0f, offset + terrain.normalized_ratio);
        const size_t terrain_index    = static_cast<size_t>(static_cast<float>(count - 1) * normalized_ratio);
        terrain.threshold = sorted_values[terrain_index];
        offset += terrain.normalized_ratio;
    }
}

auto Variations::get(size_t index) const -> Terrain_variation
{
    const float noise_value = get_noise_value(index);
    for (const auto& terrain : m_terrains)
    {
        if (noise_value < terrain.threshold)
        {
            return terrain;
        }
    }
    return *m_terrains.rbegin();
}

auto Variations::make(
    int       id,
    terrain_t base_terrain,
    terrain_t variation_terrain
) const -> Terrain_variation
{
    const auto i = std::find_if(
        m_terrains.begin(),
        m_terrains.end(),
        [base_terrain, variation_terrain](const Terrain_variation& entry)
        {
            return
                (entry.base_terrain      == base_terrain) &&
                (entry.variation_terrain == variation_terrain);
        }
    );
    if (i != m_terrains.end())
    {
        Terrain_variation result = *i;
        result.id = id;
        return result;
    }
    return Terrain_variation
    {
        .id                = id,
        .base_terrain      = base_terrain,
        .variation_terrain = variation_terrain,
        .ratio             = 1.0f,
        .normalized_ratio  = 1.0f,
        .threshold         = 0.5f
    };
}

void Variations::assign(std::vector<Terrain_variation>&& terrains)
{
    m_terrains = std::move(terrains);
    std::sort(
        m_terrains.begin(),
        m_terrains.end(),
        [](const Terrain_variation& lhs, const Terrain_variation& rhs)
        {
            return lhs.id < rhs.id;
        }
    );
}
#pragma endregion Variations

void Map_window::update_elevation_terrains()
{
    const terrain_t terrain_count = static_cast<terrain_t>(m_tiles->get_terrain_type_count());

    std::vector<Terrain_variation> new_elevation_terrains;
    std::vector<Terrain_variation> new_variation_terrains;
    int min_temperature = std::numeric_limits<int>::max();
    int max_temperature = std::numeric_limits<int>::lowest();
    int min_humidity    = std::numeric_limits<int>::max();
    int max_humidity    = std::numeric_limits<int>::lowest();
    for (terrain_t t = 0; t < terrain_count; ++t)
    {
        const Terrain_type terrain = m_tiles->get_terrain_type(t);

        min_temperature = std::min(terrain.generate_min_temperature, min_temperature);
        max_temperature = std::max(terrain.generate_max_temperature, max_temperature);
        min_humidity    = std::min(terrain.generate_min_humidity,    min_humidity);
        max_humidity    = std::max(terrain.generate_max_humidity,    max_humidity);
    }
    const float temperature_extent = static_cast<float>(max_temperature - min_temperature);
    const float humidity_extent    = static_cast<float>(max_humidity    - min_humidity);
    log_map_window.info(
        "temperature: min = {}, max = {}, extent = {}\n",
        min_temperature,
        max_temperature,
        temperature_extent
    );
    log_map_window.info(
        "humidity: min = {}, max = {}, m_humidity_extent = {}\n",
        min_humidity,
        max_humidity,
        humidity_extent
    );
    m_biomes.clear();

    for (terrain_t t = 0; t < terrain_count; ++t)
    {
        const Terrain_type terrain = m_tiles->get_terrain_type(t);

        if (terrain.generate_elevation != 0)
        {
            new_elevation_terrains.push_back(
                m_elevation_generator.make(terrain.generate_elevation, t)
            );
        }
        else if (terrain.generate_base != 0)
        {
            if (
                (terrain.generate_min_temperature != 0) ||
                (terrain.generate_max_temperature != 0) ||
                (terrain.generate_min_humidity != 0) ||
                (terrain.generate_max_humidity != 0)
            )
            {
                m_biomes.push_back(
                    Biome
                    {
                        .base_terrain    = terrain.generate_base,
                        .variation       = t,
                        .priority        = terrain.generate_priority,
                        .min_temperature = static_cast<float>(terrain.generate_min_temperature - min_temperature) / temperature_extent,
                        .max_temperature = static_cast<float>(terrain.generate_max_temperature - min_temperature) / temperature_extent,
                        .min_humidity    = static_cast<float>(terrain.generate_min_humidity    - min_humidity   ) / humidity_extent,
                        .max_humidity    = static_cast<float>(terrain.generate_max_humidity    - min_humidity   ) / humidity_extent,
                        .ratio           = terrain.generate_ratio
                    }
                );
            }
            else
            {
                const int id = static_cast<int>(new_variation_terrains.size());
                new_variation_terrains.push_back(
                    m_variation_generator.make(id, terrain.generate_base, t)
                );
            }
        }
    }

    m_elevation_generator.assign(std::move(new_elevation_terrains));
    m_variation_generator.assign(std::move(new_variation_terrains));

    for (const auto& entry : m_elevation_generator.m_terrains)
    {
        const Terrain_type terrain = m_tiles->get_terrain_type(entry.base_terrain);
        log_map_window.info(
            "elevation: base terrain {} - {}, elevation = {}, ratio = {}\n",
            entry.base_terrain,
            terrain.name,
            entry.id,
            entry.ratio
        );
    }

    std::sort(
        m_biomes.begin(),
        m_biomes.end(),
        [](const Biome& lhs, const Biome& rhs)
        {
            // Sort first by priority
            if (lhs.priority != rhs.priority)
            {
                return lhs.priority > rhs.priority;
            }

            // then by average temperature
            const auto lhs_temperature = lhs.min_temperature + lhs.max_temperature;
            const auto rhs_temperature = rhs.min_temperature + rhs.max_temperature;
            if (lhs_temperature != rhs_temperature)
            {
                return lhs_temperature < rhs_temperature;
            }

            // then by average humidity
            const auto lhs_humidity = lhs.min_humidity + lhs.max_humidity;
            const auto rhs_humidity = rhs.min_humidity + rhs.max_humidity;
            if (lhs_humidity != rhs_humidity)
            {
                return lhs_humidity < rhs_humidity;
            }

            // then last by ratio
            return lhs.ratio < rhs.ratio;
        }
    );

    for (const Biome& biome : m_biomes)
    {
        const Terrain_type base_terrain = m_tiles->get_terrain_type(biome.base_terrain);
        const Terrain_type variation    = m_tiles->get_terrain_type(biome.variation);
        log_map_window.info(
            "biome: base terrain {}, variation {}, temperature = {}..{}, humidity = {}..{}, ratio = {}\n",
            base_terrain.name,
            variation.name,
            biome.min_temperature,
            biome.max_temperature,
            biome.min_humidity,
            biome.max_humidity,
            biome.ratio
        );
    }
}

void Map_window::generate_noise_pass()
{
    // In the first pass, we just generate noise values
    const int    width  = m_map->width();
    const int    height = m_map->height();
    const size_t count  = static_cast<size_t>(width) * static_cast<size_t>(height);

    update_elevation_terrains();

    m_elevation_generator  .reset(count);
    m_temperature_generator.reset(count);
    m_humidity_generator   .reset(count);
    m_variation_generator  .reset(count);
    const glm::vec4 elevation_seed  {12334.1f, 14378.0f, 12381.1f, 14386.9f};
    const glm::vec4 temperature_seed{27865.9f, 24387.6f, 28726.5f, 28271.4f};
    const glm::vec4 humidity_seed   {38760.8f, 39732.0f, 39785.6f, 32317.8f};
    const glm::vec4 variation_seed  {41902.6f, 41986.3f, 42098.7f, 43260.9f};
    for (coordinate_t tx = 0; tx < width; ++tx)
    {
        const float x        = static_cast<float>(tx) / static_cast<float>(width);
        const float y_offset = (tx & 1) == 1 ? -0.5f : 0.0f;
        for (coordinate_t ty = 0; ty < height; ++ty)
        {
            const float y = (static_cast<float>(ty) + y_offset) / static_cast<float>(height);
            const float elevation   = m_noise.generate(x, y, elevation_seed  );
            const float temperature = m_noise.generate(x, y, temperature_seed);
            const float humidity    = m_noise.generate(x, y, humidity_seed   );
            const float variation   = m_noise.generate(x, y, variation_seed  );
            m_elevation_generator  .push(elevation  );
            m_temperature_generator.push(temperature);
            m_humidity_generator   .push(humidity   );
            m_variation_generator  .push(variation  );
        }
    }
}

void Map_window::generate_base_terrain_pass()
{
    // Second pass converts noise values to terrain values based on thresholds
    m_elevation_generator.compute_threshold_values();

    for (const auto& entry : m_elevation_generator.m_terrains)
    {
        const Terrain_type& terrain = m_tiles->get_terrain_type(entry.base_terrain);
        log_map_window.info(
            "terrain {} - {}, elevation = {}, ratio = {}, normalized ratio = {}, threshold = {}\n",
            entry.base_terrain,
            terrain.name,
            entry.id,
            entry.ratio,
            entry.normalized_ratio,
            entry.threshold
        );
    }

    const int w = m_map->width();
    const int h = m_map->height();

    size_t index = 0;
    for (coordinate_t tx = 0; tx < w; ++tx)
    {
        for (coordinate_t ty = 0; ty < h; ++ty)
        {
            const Terrain_variation t = m_elevation_generator.get(index);
            m_map->set_terrain(Tile_coordinate{tx, ty}, t.base_terrain);
            ++index;
        }
    }
}

//template <typename T>
//[[nodiscard]] auto is_in_list(
//    const T&                 item,
//    std::initializer_list<T> items
//) -> bool
//{
//    return std::find(
//        items.begin(),
//        items.end(),
//        item
//    ) != items.end();
//}

auto Map_window::get_variation(
    terrain_t base_terrain,
    float     temperature,
    float     humidity,
    float     variation
) const -> terrain_t
{
    for (const Biome& biome : m_biomes)
    {
        if (base_terrain != biome.base_terrain)
        {
            continue;
        }
        if (temperature < biome.min_temperature)
        {
            continue;
        }
        if (humidity < biome.min_humidity)
        {
            continue;
        }
        if (temperature > biome.max_temperature)
        {
            continue;
        }
        if (humidity > biome.max_humidity)
        {
            continue;
        }
        if (variation > biome.ratio)
        {
            continue;
        }
        return biome.variation;
    }
    return base_terrain;
}

void Map_window::generate_variation_pass()
{
    m_temperature_generator.compute_threshold_values();
    m_humidity_generator   .compute_threshold_values();
    m_variation_generator  .compute_threshold_values();

    const int width = m_map->width();
    const int height = m_map->height();

    size_t index = 0;
    for (coordinate_t tx = 0; tx < width; ++tx)
    {
        for (coordinate_t ty = 0; ty < height; ++ty)
        {
            const Tile_coordinate position{tx, ty};
            const terrain_t       base_terrain = m_map->get_terrain(position);
            const float           temperature  = m_temperature_generator.get_noise_value(index);
            const float           humidity     = m_humidity_generator   .get_noise_value(index);
            const float           variation    = m_variation_generator  .get_noise_value(index);
            const terrain_t       terrain      = get_variation(base_terrain, temperature, humidity, variation);
            m_map->set_terrain(position, terrain);
            ++index;
        }
    }
}

void Map_window::apply_rule(
    const Terrain_replacement_rule& rule
)
{
    std::function<void(Tile_coordinate)> post_process_op =
    [this, &rule](Tile_coordinate tile_position) -> void
    {
        const terrain_t primary_t = m_map->get_terrain(tile_position);
        if (primary_t != rule.primary)
        {
            return;
        }
        std::function<void(Tile_coordinate)> replace =
        [this, &rule] (Tile_coordinate position) -> void
        {
            const terrain_t secondary_t = m_map->get_terrain(position);
            const bool      found = std::find(
                rule.secondary.begin(),
                rule.secondary.end(),
                secondary_t
            ) != rule.secondary.end();
            const bool      apply = rule.equal ? found : !found;

            if (apply)
            {
                m_map->set_terrain(position, rule.replacement);
            }
        };
        m_map->hex_circle(tile_position, 0, 1, replace);
    };
    m_map->for_each_tile(post_process_op);
}

void Map_window::generate_apply_rules_pass()
{
    // Third pass does post-processing, adjusting neighoring
    // tiles based on a few rules.

    const size_t rule_count = m_tiles->get_terrain_replacement_rule_count();
    for (size_t i = 0; i < rule_count; ++i)
    {
        const Terrain_replacement_rule rule = m_tiles->get_terrain_replacement_rule(i);
        if (!rule.enabled)
        {
            continue;
        }
        apply_rule(rule);
    }
}

void Map_window::generate_group_fix_pass()
{
    // Apply terrain group rules
    m_map->for_each_tile(
        [this](Tile_coordinate tile_position)
        {
            update_group_terrain(tile_position);
        }
    );
    m_map->for_each_tile(
        [this](Tile_coordinate tile_position)
        {
            update_group_terrain(tile_position);
        }
    );
}

void Map_window::tool_imgui()
{
    constexpr ImVec2 button_size{100.0f, 0.0f};

    if (ImGui::Button("Back to Menu", button_size))
    {
        hide();
        get<Terrain_palette_window>()->hide();
        get<Map_tool_window       >()->hide();
        get<Menu_window           >()->show();
    }

    if (ImGui::Button("Load", button_size))
    {
    }

    if (ImGui::Button("Save", button_size))
    {
    }

    if (ImGui::Button("Fill", button_size))
    {
    }

    if (
        ImGui::TreeNodeEx(
            "Generate",
            ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen
        )
    )
    {
        if (ImGui::TreeNodeEx("Elevation", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen))
        {
            int slot = 0;
            for (Terrain_variation& elevation_terrain : m_elevation_generator.m_terrains)
            {
                const Terrain_type& terrain_type = m_tiles->get_terrain_type(elevation_terrain.base_terrain);

                const auto label = fmt::format("{}##elevation-{}", terrain_type.name, ++slot);
                ImGui::SliderFloat(
                    label.c_str(),
                    &elevation_terrain.ratio,
                    0.0f,
                    10.0f
                );
            }
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Variation", ImGuiTreeNodeFlags_Framed))
        {
            int slot = 0;
            for (Terrain_variation& variation_terrain : m_variation_generator.m_terrains)
            {
                const Terrain_type& terrain_type = m_tiles->get_terrain_type(variation_terrain.variation_terrain);

                const auto label = fmt::format("{}##variation-{}", terrain_type.name, ++slot);
                ImGui::SliderFloat(
                    label.c_str(),
                    &variation_terrain.ratio,
                    0.0f,
                    10.0f
                );
            }
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Noise", ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen))
        {
            m_noise.imgui();
            ImGui::TreePop();
        }

        if (ImGui::Button("Generate", button_size))
        {
            m_noise.prepare();
            generate_noise_pass();
            generate_base_terrain_pass();
            generate_apply_rules_pass();
            generate_group_fix_pass();
            generate_variation_pass();
            generate_group_fix_pass();
        }

        ImGui::TreePop();
    }

    ImGui::Combo("Grid", &m_grid, c_grid_mode_strings, IM_ARRAYSIZE(c_grid_mode_strings));
    //ImGui::Text("Zoom: %.2f", m_zoom);
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
        .reverse_depth = get<erhe::application::Configuration>()->reverse_depth

    };
    m_map_renderer->render(viewport);

    m_text_renderer->render(viewport);
    m_text_renderer->next_frame();

    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);
}

} // namespace hextiles
