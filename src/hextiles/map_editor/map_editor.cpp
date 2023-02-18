#include "map_editor/map_editor.hpp"
#include "map.hpp"
#include "map_window.hpp"
#include "tiles.hpp"
#include "tile_renderer.hpp"

#include "erhe/application/commands/commands.hpp"
#include "erhe/application/commands/command_context.hpp"
#include "erhe/toolkit/verify.hpp"

#include <imgui.h>

#include <fmt/format.h>

namespace hextiles
{

Map_primary_brush_command::Map_primary_brush_command()
    : Command{"Map_editor.primary_brush"}
{
}

void Map_primary_brush_command::try_ready()
{
    if (get_command_state() != erhe::application::State::Inactive) {
        return;
    }

    set_ready();
}

auto Map_primary_brush_command::try_call() -> bool
{
    if (get_command_state() == erhe::application::State::Ready) {
        set_active();
    }

    if (get_command_state() != erhe::application::State::Active) {
        return false;
    }

    g_map_editor->primary_brush();
    return true;
}

auto Map_primary_brush_command::try_call_with_input(erhe::application::Input_arguments& input) -> bool
{
    g_map_editor->hover(input.vector2.absolute_value);
    return try_call();
}

Map_hover_command::Map_hover_command()
    : Command{"Map_editor.hover"}
{
}

auto Map_hover_command::try_call_with_input(erhe::application::Input_arguments& input) -> bool
{
    g_map_editor->hover(input.vector2.absolute_value);
    return false;
}

Map_editor* g_map_editor{nullptr};

Map_editor::Map_editor()
    : Component{c_type_name}
{
}

Map_editor::~Map_editor() noexcept
{
    ERHE_VERIFY(g_map_editor == nullptr);
}

void Map_editor::deinitialize_component()
{
    ERHE_VERIFY(g_map_editor == this);
    m_map_hover_command.set_host(nullptr);
    m_map_primary_brush_command.set_host(nullptr);
    g_map_editor = nullptr;
}

void Map_editor::declare_required_components()
{
    require<erhe::application::Commands>();
}

void Map_editor::initialize_component()
{
    ERHE_VERIFY(g_map_editor == nullptr);

    //m_pixel_lookup = std::make_unique<Pixel_lookup>();

    File_read_stream file{"res/hextiles/map_new"};
    m_map = new Map; // TODO
    m_map->read(file);

    erhe::application::g_commands->register_command(&m_map_hover_command);
    erhe::application::g_commands->register_command(&m_map_primary_brush_command);

    erhe::application::g_commands->bind_command_to_mouse_motion(&m_map_hover_command);
    erhe::application::g_commands->bind_command_to_mouse_drag  (&m_map_primary_brush_command, erhe::toolkit::Mouse_button_left, true);

    m_map_hover_command.set_host(this);
    m_map_primary_brush_command.set_host(this);

    g_map_editor = this;
}

void Map_editor::hover(glm::vec2 position_in_root)
{
    const glm::vec2 window_position = g_map_window->to_content(position_in_root);

    m_hover_window_position = window_position;

    Pixel_coordinate hover_pixel_position{
        static_cast<pixel_t>(window_position.x),
        static_cast<pixel_t>(window_position.y)
    };

    m_hover_tile_position = g_map_window->pixel_to_tile(hover_pixel_position);
}

void Map_editor::primary_brush()
{
    const auto tile_position_opt = m_hover_tile_position; //g_map_window->pixel_to_tile(pixel_position);
    if (!tile_position_opt.has_value()) {
        return;
    }
    const auto tile_position = tile_position_opt.value();

    std::function<void(Tile_coordinate)> set_terrain_op =
    [this] (Tile_coordinate position) -> void
    {
        m_map->set_terrain_tile(position, m_left_brush);
    };

    std::function<void(Tile_coordinate)> update_op =
    [this] (Tile_coordinate position) -> void
    {
        m_map->update_group_terrain(position);
    };

    m_map->hex_circle(tile_position, 0, m_brush_size - 1, set_terrain_op);
    m_map->hex_circle(tile_position, 0, m_brush_size + 1, update_op);
}

void Map_editor::terrain_palette()
{
    auto& terrain_type = g_tiles->get_terrain_type(m_left_brush);
    g_map_window->tile_image(m_left_brush, 3);
    ImGui::SameLine();
    ImGui::Text("%s", terrain_type.name.c_str());

    terrain_tile_t terrain = 0;
    for (int ty = 0; ty < Base_tiles::height; ++ty) {
        for (int tx = 0; tx < Base_tiles::width; ++tx) {
            const bool pressed = g_map_window->tile_image(terrain, 2);
            if (pressed) {
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
    const auto& terrain_shapes = g_tile_renderer->get_terrain_shapes();

    if (
        !m_hover_tile_position.has_value() ||
        m_left_brush >= terrain_shapes.size()
    ) {
        return;
    }

    const auto&             location = m_hover_tile_position.value();
    const Pixel_coordinate& shape    = terrain_shapes[m_left_brush];
    //const tile_t      tile     = m_tile_renderer->get_terrain_tile(m_left_brush);
    const std::string text  = fmt::format("{}, {}", location.x, location.y);
    g_map_window->blit (shape, location, 0x88888888u);
    g_map_window->print(text, location);
}

auto Map_editor::get_map() -> Map*
{
    return m_map;
}

auto Map_editor::get_hover_tile_position() const -> std::optional<Tile_coordinate>
{
    return m_hover_tile_position;
}

} // namespace hextiles
