#pragma once

#include "game/game.hpp"
#include "new_game_window.hpp"
#include "map_editor/map_editor.hpp"
#include "type_editors/type_editor.hpp"

#include "erhe_imgui/imgui_window.hpp"

namespace erhe::commands { class Commands; }
namespace erhe::graphics { class Device; }
namespace erhe::imgui    { class Imgui_windows; }
namespace erhe::renderer { class Text_renderer; }
namespace erhe::window   { class Input_event_handler; }

namespace hextiles {

class Tiles;
class Tile_renderer;
class Map_window;

class Menu_window : public erhe::imgui::Imgui_window
{
public:
    Menu_window(
        erhe::commands::Commands&          commands,
        erhe::imgui::Imgui_renderer&       imgui_renderer,
        erhe::imgui::Imgui_windows&        imgui_windows,
        erhe::window::Input_event_handler& input_event_handler,
        Map_window&                        map_window,
        Tiles&                             tiles,
        Tile_renderer&                     tile_renderer
    );

    // Implements Imgui_window
    void imgui() override;

    void show_menu();

private:
    erhe::window::Input_event_handler& m_input_event_handler;
    Tiles&                             m_tiles;
    Tile_renderer&                     m_tile_renderer;
    Map_window&                        m_map_window;
    Game                               m_game;
    Map_editor                         m_map_editor;
    New_game_window                    m_new_game_window;
    Type_editor                        m_type_editor;
};

} // namespace hextiles
