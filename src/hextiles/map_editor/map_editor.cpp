#include "map_editor/map_editor.hpp"
#include "map.hpp"
#include "map_window.hpp"
#include "tiles.hpp"
#include "tile_renderer.hpp"

#include "erhe/application/commands/commands.hpp"
#include "erhe/application/commands/command_context.hpp"

#include <imgui.h>

#include <fmt/format.h>

namespace hextiles
{

auto Map_primary_brush_command::try_call(erhe::application::Command_context& context) -> bool
{
    if (get_command_state() == erhe::application::State::Ready)
    {
        set_active(context);
    }

    if (get_command_state() != erhe::application::State::Active)
    {
        return false;
    }

    m_map_editor.primary_brush(context.get_vec2_absolute_value());
    return true;
}

void Map_primary_brush_command::try_ready(erhe::application::Command_context& context)
{
    if (get_command_state() != erhe::application::State::Inactive)
    {
        return;
    }

    // TODO only set ready when hovering over map
    set_ready(context);
    m_map_editor.primary_brush(context.get_vec2_absolute_value());
}

auto Map_hover_command::try_call(erhe::application::Command_context& context) -> bool
{
    m_map_editor.hover(context.get_vec2_absolute_value());
    return false;
}

Map_editor::Map_editor()
    : erhe::components::Component{c_type_name}
    , m_map_hover_command        {*this}
    , m_map_primary_brush_command{*this}
{
}

Map_editor::~Map_editor() noexcept
{
}

void Map_editor::declare_required_components()
{
    require<erhe::application::Commands>();
    m_map_window = require<Map_window>();
}

void Map_editor::initialize_component()
{
    //m_pixel_lookup = std::make_unique<Pixel_lookup>();

    m_map = std::make_shared<Map>();

    File_read_stream file{"res/hextiles/map_new"};
    m_map->read(file);

    const auto commands = get<erhe::application::Commands>();
    commands->register_command(&m_map_hover_command);
    commands->register_command(&m_map_primary_brush_command);

    commands->bind_command_to_mouse_motion(&m_map_hover_command);
    commands->bind_command_to_mouse_drag  (&m_map_primary_brush_command, erhe::toolkit::Mouse_button_left);
}

void Map_editor::post_initialize()
{
    m_tile_renderer = get<Tile_renderer>();
    m_tiles         = get<Tiles        >();
}

auto Map_editor::get_map() -> std::shared_ptr<Map>
{
    return m_map;
}

void Map_editor::hover(glm::vec2 position_in_root)
{
    const glm::vec2 window_position = m_map_window->to_content(position_in_root);

    m_hover_window_position = window_position;

    Pixel_coordinate hover_pixel_position{
        static_cast<pixel_t>(window_position.x),
        static_cast<pixel_t>(window_position.y)
    };

    m_hover_tile_position = m_map_window->pixel_to_tile(hover_pixel_position);
}

void Map_editor::primary_brush(glm::vec2 position_in_root)
{
    const glm::vec2 mouse_position = m_map_window->to_content(position_in_root);
    const auto pixel_position = Pixel_coordinate{
        static_cast<pixel_t>(mouse_position.x),
        static_cast<pixel_t>(mouse_position.y)
    };
    const auto tile_position = m_map_window->pixel_to_tile(pixel_position);

    std::function<void(Tile_coordinate)> set_terrain_op =
    [this] (Tile_coordinate position) -> void
    {
        m_map->set_terrain_tile(position, m_left_brush);
    };

    std::function<void(Tile_coordinate)> update_op =
    [this] (Tile_coordinate position) -> void
    {
        m_map->update_group_terrain(*m_tiles.get(), position);
    };

    m_map->hex_circle(tile_position, 0, m_brush_size - 1, set_terrain_op);
    m_map->hex_circle(tile_position, 0, m_brush_size + 1, update_op);
}

void Map_editor::terrain_palette()
{
    auto& terrain_type = m_tiles->get_terrain_type(m_left_brush);
    m_map_window->tile_image(m_left_brush, 3);
    ImGui::SameLine();
    ImGui::Text("%s", terrain_type.name.c_str());

    terrain_tile_t terrain = 0;
    for (int ty = 0; ty < Base_tiles::height; ++ty)
    {
        for (int tx = 0; tx < Base_tiles::width; ++tx)
        {
            const bool pressed = m_map_window->tile_image(terrain, 2);
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

void Map_editor::render()
{
    const auto& terrain_shapes = m_tile_renderer->get_terrain_shapes();

    if (
        !m_hover_tile_position.has_value() ||
        m_left_brush >= terrain_shapes.size()
    )
    {
        return;
    }

    const auto&             location = m_hover_tile_position.value();
    const Pixel_coordinate& shape    = terrain_shapes[m_left_brush];
    //const tile_t      tile     = m_tile_renderer->get_terrain_tile(m_left_brush);
    const std::string text  = fmt::format("{}, {}", location.x, location.y);
    m_map_window->blit (shape, location, 0x88888888u);
    m_map_window->print(text, location);
}

} // namespace hextiles
