#include "map_editor/map_editor.hpp"

#include "hextiles_log.hpp"
#include "map.hpp"
#include "map_window.hpp"
#include "tiles.hpp"
#include "tile_renderer.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_commands/input_arguments.hpp"
#include "erhe_verify/verify.hpp"

#include <imgui/imgui.h>

#include <fmt/format.h>

namespace hextiles {

Map_primary_brush_command::Map_primary_brush_command(erhe::commands::Commands& commands, Map_editor& map_editor)
    : Command   {commands, "Map_editor.primary_brush"}
    , map_editor{map_editor}
{
}

void Map_primary_brush_command::try_ready()
{
    if (get_command_state() != erhe::commands::State::Inactive) {
        return;
    }

    set_ready();
}

auto Map_primary_brush_command::try_call() -> bool
{
    if (get_command_state() == erhe::commands::State::Ready) {
        set_active();
    }

    if (get_command_state() != erhe::commands::State::Active) {
        return false;
    }

    map_editor.primary_brush();
    return true;
}

auto Map_primary_brush_command::try_call_with_input(erhe::commands::Input_arguments& input) -> bool
{
    map_editor.hover(input.variant.vector2.absolute_value);
    return try_call();
}

Map_hover_command::Map_hover_command(erhe::commands::Commands& commands, Map_editor& map_editor)
    : Command   {commands, "Map_editor.hover"}
    , map_editor{map_editor}
{
}

auto Map_hover_command::try_call_with_input(erhe::commands::Input_arguments& input) -> bool
{
    map_editor.hover(input.variant.vector2.absolute_value);
    return false;
}

Map_editor::Map_editor(
    erhe::commands::Commands&    commands,
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Map_window&                  map_window,
    Menu_window&                 menu_window,
    Tile_renderer&               tile_renderer,
    Tiles&                       tiles
)
    : m_map_window   {map_window}
    , m_menu_window  {menu_window}
    , m_tile_renderer{tile_renderer}
    , m_tiles        {tiles}

    , m_map_generator         {imgui_renderer, imgui_windows, *this, tiles}
    , m_map_tool_window       {imgui_renderer, imgui_windows, *this, m_map_generator, map_window, menu_window, tile_renderer, tiles}
    , m_terrain_palette_window{imgui_renderer, imgui_windows, *this}

    , m_map_hover_command        {commands, *this}
    , m_map_primary_brush_command{commands, *this}

{
    File_read_stream file{"res/hextiles/map_new"};

    m_map = new Map{}; // TODO
    m_map->read(file);

    commands.register_command(&m_map_hover_command);
    commands.register_command(&m_map_primary_brush_command);
    commands.bind_command_to_mouse_motion(&m_map_hover_command);
    commands.bind_command_to_mouse_drag  (&m_map_primary_brush_command, erhe::window::Mouse_button_left, true);

    m_map_hover_command.set_host(this);
    m_map_primary_brush_command.set_host(this);
}

void Map_editor::hide_windows()
{
    m_map_window.set_map(nullptr);
    m_map_generator         .hide_window();
    m_map_tool_window       .hide_window();
    m_terrain_palette_window.hide_window();
}

void Map_editor::show_windows()
{
    m_map_window.set_map(m_map);
    m_map_generator         .show_window();
    m_map_tool_window       .show_window();
    m_terrain_palette_window.show_window();
}

void Map_editor::hover(glm::vec2 position_in_root)
{
    m_map_window.hover(position_in_root);
}

void Map_editor::primary_brush()
{
    const auto tile_position_opt = m_map_window.get_hover_tile_position();
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
        update_group_terrain(m_tiles, *m_map, position);
    };

    log_map_editor->info("brush size = {} p.x = {} p.y = {}", m_brush_size, tile_position.x, tile_position.y);

    m_map->hex_circle(tile_position, 0, m_brush_size - 1, set_terrain_op);
    m_map->hex_circle(tile_position, 0, m_brush_size + 1, update_op);
}

void Map_editor::terrain_palette()
{
    auto& terrain_type = m_tiles.get_terrain_type(m_left_brush);
    m_map_window.tile_image(m_left_brush, 3);
    ImGui::SameLine();
    ImGui::Text("%s", terrain_type.name.c_str());

    terrain_tile_t terrain = 0;
    for (int ty = 0; ty < Base_tiles::height; ++ty) {
        for (int tx = 0; tx < Base_tiles::width; ++tx) {
            const bool pressed = m_map_window.tile_image(terrain, 2);
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
    const auto& terrain_shapes = m_tile_renderer.get_terrain_shapes();

    std::optional<Tile_coordinate> hover_tile_position = m_map_window.get_hover_tile_position();
    if (
        !hover_tile_position.has_value() ||
        m_left_brush >= terrain_shapes.size()
    ) {
        return;
    }

    const auto&             location = hover_tile_position.value();
    const Pixel_coordinate& shape    = terrain_shapes[m_left_brush];
    const std::string       text     = fmt::format("{}, {}", location.x, location.y);
    m_map_window.blit (shape, location, 0x88888888u);
    m_map_window.print(text, location);
}

auto Map_editor::get_map() -> Map*
{
    return m_map;
}

auto Map_editor::get_hover_tile_position() const -> std::optional<Tile_coordinate>
{
    return m_map_window.get_hover_tile_position();
}

} // namespace hextiles
