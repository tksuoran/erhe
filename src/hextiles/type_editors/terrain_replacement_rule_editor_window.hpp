#pragma once

#include "erhe_imgui/imgui_window.hpp"

namespace erhe::imgui
{
    class Imgui_windows;
}

namespace hextiles
{

class Menu_window;
class Tiles;
class Type_editor;

class Terrain_replacement_rule_editor_window
    : public erhe::imgui::Imgui_window
{
public:
    Terrain_replacement_rule_editor_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Menu_window&                 menu_window,
        Tiles&                       tiles,
        Type_editor&                 type_editor
    );

    // Implements Imgui_window
    void imgui() override;

private:
    Menu_window& m_menu_window;
    Tiles&       m_tiles;
    Type_editor& m_type_editor;
};

} // namespace hextiles
