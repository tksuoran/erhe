#pragma once

#include "coordinate.hpp"
#include "terrain_type.hpp"
#include "map.hpp"

#include "map_generator/map_generator.hpp"
#include "map_editor/map_tool_window.hpp"
#include "map_editor/terrain_palette_window.hpp"
#include "erhe/commands/command.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>

namespace erhe::imgui
{
    class Imgui_renderer;
    class Imgui_windows;
}

namespace hextiles
{

class Menu_window;

class Map_hover_command final
    : public erhe::commands::Command
{
public:
    Map_hover_command(
        erhe::commands::Commands& commands,
        Map_editor&               map_editor
    );

    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;

private:
    Map_editor& map_editor;
};

class Map_primary_brush_command final
    : public erhe::commands::Command
{
public:
    Map_primary_brush_command(
        erhe::commands::Commands& commands,
        Map_editor&               map_editor
    );

    void try_ready          () override;
    auto try_call           () -> bool override;
    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;

private:
    Map_editor& map_editor;
};

class Map_editor
    : public erhe::commands::Command_host
{
public:
    Map_editor(
        erhe::commands::Commands&    commands,
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Map_window&                  map_window,
        Menu_window&                 menu_window,
        Tile_renderer&               tile_renderer,
        Tiles&                       tiles
    );

    // Public API
    void render         ();
    void terrain_palette();
    void hide_windows   ();
    void show_windows   ();
    [[nodiscard]] auto get_map                () -> Map*;
    [[nodiscard]] auto get_hover_tile_position() const -> std::optional<Tile_coordinate>;

    // Commands
    void hover        (glm::vec2 window_position);
    void primary_brush();

private:
    Map_window&    m_map_window;
    Menu_window&   m_menu_window;
    Tile_renderer& m_tile_renderer;
    Tiles&         m_tiles;

    Map_generator          m_map_generator;
    Map_tool_window        m_map_tool_window;
    Terrain_palette_window m_terrain_palette_window;

    // Commands
    Map_hover_command              m_map_hover_command;
    Map_primary_brush_command      m_map_primary_brush_command;
    int                            m_brush_size{1};
    terrain_tile_t                 m_left_brush{Terrain_default};
    std::optional<glm::vec2>       m_hover_window_position;
    std::optional<Tile_coordinate> m_hover_tile_position;
    Map*                           m_map{nullptr};;
};

} // namespace hextiles

