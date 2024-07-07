#pragma once

#include "erhe_imgui/imgui_window.hpp"

namespace erhe::imgui {
    class Imgui_windows;
}

namespace hextiles {

class Map_editor;

class Terrain_palette_window : public erhe::imgui::Imgui_window
{
public:
    Terrain_palette_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Map_editor&                  map_editor
    );

    // Implements Imgui_window
    void imgui() override;

private:
    Map_editor& m_map_editor;
};

} // namespace hextiles
