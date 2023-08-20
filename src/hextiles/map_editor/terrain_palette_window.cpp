#include "map_editor/terrain_palette_window.hpp"

#include "map_editor/map_editor.hpp"

#include "erhe_imgui/imgui_windows.hpp"

namespace hextiles
{

Terrain_palette_window::Terrain_palette_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Map_editor&                  map_editor
)
    : Imgui_window{imgui_renderer, imgui_windows, "Terrain Palette", "terrain_palete"}
    , m_map_editor{map_editor}
{
    hide();
}

void Terrain_palette_window::imgui()
{
    m_map_editor.terrain_palette();
}

} // namespace hextiles
