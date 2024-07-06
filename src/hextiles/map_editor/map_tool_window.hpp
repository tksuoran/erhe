#pragma once

#include "coordinate.hpp"

#include "erhe_imgui/imgui_window.hpp"

#include <string>

namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_windows;
}

namespace hextiles {

class Map_editor;
class Map_generator;
class Map_window;
class Menu_window;
class Tile_renderer;
class Tiles;

class Map_tool_window
    : public erhe::imgui::Imgui_window
{
public:
    Map_tool_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Map_editor&                  map_editor,
        Map_generator&               map_generator,
        Map_window&                  map_window,
        Menu_window&                 menu_window,
        Tile_renderer&               tile_renderer,
        Tiles&                       tiles
    );

    // Implements Imgui_window
    void imgui() override;

private:
    void tile_info(const Tile_coordinate tile_position);

    Map_editor&    m_map_editor;
    Map_generator& m_map_generator;
    Map_window&    m_map_window;
    Menu_window&   m_menu_window;
    Tile_renderer& m_tile_renderer;
    Tiles&         m_tiles;
};

} // namespace hextiles
