#include "map_editor/map_tool_window.hpp"

#include "hextiles_log.hpp"
#include "map_editor/map_editor.hpp"
#include "map_editor/terrain_palette_window.hpp"
#include "map_generator/map_generator.hpp"
#include "map.hpp"
#include "map_window.hpp"
#include "menu_window.hpp"
#include "tiles.hpp"
#include "tile_renderer.hpp"

#include "erhe/imgui/imgui_windows.hpp"
#include "erhe/imgui/imgui_renderer.hpp"
#include "erhe/toolkit/file.hpp"
#include "erhe/toolkit/verify.hpp"

#include <gsl/assert>

namespace hextiles
{

Map_tool_window::Map_tool_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Map_editor&                  map_editor,
    Map_generator&               map_generator,
    Map_window&                  map_window,
    Menu_window&                 menu_window,
    Tile_renderer&               tile_renderer,
    Tiles&                       tiles
)
    : Imgui_window   {imgui_renderer, imgui_windows, "Map Tool", "map_tool"}
    , m_map_editor   {map_editor}
    , m_map_generator{map_generator}
    , m_map_window   {map_window}
    , m_menu_window  {menu_window}
    , m_tile_renderer{tile_renderer}
    , m_tiles        {tiles}
{
    hide();
}

void Map_tool_window::imgui()
{
    constexpr ImVec2 button_size{100.0f, 0.0f};

    if (ImGui::Button("Back to Menu", button_size)) {
        m_menu_window.show_menu();
    }
    if (ImGui::Button("Generator", button_size)) {
        m_map_generator.show();
    }
    if (ImGui::Button("Load Map")) {
        const auto path_opt = erhe::toolkit::select_file();
        if (path_opt.has_value()) {
            File_read_stream file{path_opt.value()};
            m_map_editor.get_map()->read(file);
        }
    }
    if (ImGui::Button("Save Map")) {
        const auto path_opt = erhe::toolkit::select_file();
        if (path_opt.has_value()) {
            File_write_stream file{path_opt.value()};
            m_map_editor.get_map()->write(file);
        }
    }

    const auto hover_pos_opt = m_map_editor.get_hover_tile_position();
    if (hover_pos_opt.has_value()) {
        const auto hover_pos = hover_pos_opt.value();
        ImGui::Text("Hover pos = %d, %d", hover_pos.x, hover_pos.y);
    }

#if 0
    ImGui::Combo("Grid", &m_grid, c_grid_mode_strings, IM_ARRAYSIZE(c_grid_mode_strings));
    //ImGui::Text("Zoom: %.2f", m_zoom);
    if (!m_hover_window_position.has_value()) {
        return;
    }

    auto input_sink = m_editor_view->input_sink();
    if (input_sink != this) {
        return;
    }
    const auto window_position = m_hover_window_position.value();

    const auto tile_position = m_map_window->pixel_to_tile(
        Pixel_coordinate{
            static_cast<pixel_t>(window_position.x),
            static_cast<pixel_t>(window_position.y)
        }
    );
    tile_info(tile_position);
#endif
}

void Map_tool_window::tile_info(const Tile_coordinate tile_position)
{
    const auto&          map            = m_map_editor.get_map();
    const terrain_tile_t terrain_tile   = map->get_terrain_tile(tile_position);
    const auto&          terrain_shapes = m_tile_renderer.get_terrain_shapes();
    if (terrain_tile >= terrain_shapes.size()) {
        return;
    }
    const auto terrain = m_tiles.get_terrain_from_tile(terrain_tile);
    if (terrain >= terrain_shapes.size()) {
        return;
    }

    const auto& terrain_type = m_tiles.get_terrain_type(terrain);

    ImGui::Text("Tile @ %d, %d %s", tile_position.x, tile_position.y, terrain_type.name.c_str());
    ImGui::Text("Terrain: %d, base: %d", terrain, terrain);
    ImGui::Text("City Size: %d", terrain_type.city_size);

    m_map_window.tile_image(terrain_tile, 3);
    ImGui::SameLine();

    const terrain_tile_t base_terrain_tile = m_tiles.get_terrain_tile_from_terrain(terrain);
    m_map_window.tile_image(base_terrain_tile, 3);

    const int distance = map->distance(tile_position, Tile_coordinate{0, 0});
    ImGui::Text("Distance to 0,0: %d", distance);
}

} // namespace hextiles
