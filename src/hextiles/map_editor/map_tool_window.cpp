#include "map_editor/map_tool_window.hpp"
#include "map_editor/map_editor.hpp"
#include "map_editor/terrain_palette_window.hpp"
#include "map_generator/map_generator.hpp"
#include "map.hpp"
#include "map_window.hpp"
#include "menu_window.hpp"
#include "tiles.hpp"
#include "tile_renderer.hpp"

#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/imgui/imgui_renderer.hpp"

#include <gsl/assert>

namespace hextiles
{

Map_tool_window::Map_tool_window()
    : erhe::components::Component{c_type_name}
    , Imgui_window               {c_title, c_type_name}
{
}

Map_tool_window::~Map_tool_window() noexcept
{
}

void Map_tool_window::declare_required_components()
{
    require<erhe::application::Imgui_windows>();
}

void Map_tool_window::initialize_component()
{
    Expects(m_components != nullptr);
    Imgui_window::initialize(*m_components);
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
}

void Map_tool_window::post_initialize()
{
    m_imgui_renderer = get<erhe::application::Imgui_renderer>();
    m_map_editor     = get<Map_editor>();
    m_map_window     = get<Map_window>();
    m_tile_renderer  = get<Tile_renderer>();
}

void Map_tool_window::imgui()
{
    constexpr ImVec2 button_size{100.0f, 0.0f};

    if (ImGui::Button("Back to Menu", button_size))
    {
        get<Menu_window>()->show_menu();
    }
    if (ImGui::Button("Generator", button_size))
    {
        get<Map_generator>()->show();
    }

#if 0
    ImGui::Combo("Grid", &m_grid, c_grid_mode_strings, IM_ARRAYSIZE(c_grid_mode_strings));
    //ImGui::Text("Zoom: %.2f", m_zoom);
    if (!m_hover_window_position.has_value())
    {
        return;
    }

    auto input_sink = m_editor_view->input_sink();
    if (input_sink != this)
    {
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
    const auto&          map            = m_map_editor->get_map();
    const terrain_tile_t terrain_tile   = map->get_terrain_tile(tile_position);
    const auto&          terrain_shapes = m_tile_renderer->get_terrain_shapes();
    if (terrain_tile >= terrain_shapes.size())
    {
        return;
    }
    const auto terrain = m_tiles->get_terrain_from_tile(terrain_tile);
    if (terrain >= terrain_shapes.size())
    {
        return;
    }

    const auto& terrain_type = m_tiles->get_terrain_type(terrain);

    ImGui::Text("Tile @ %d, %d %s", tile_position.x, tile_position.y, terrain_type.name.c_str());
    ImGui::Text("Terrain: %d, base: %d", terrain, terrain);
    ImGui::Text("City Size: %d", terrain_type.city_size);

    m_map_window->tile_image(terrain_tile, 3);
    ImGui::SameLine();

    const terrain_tile_t base_terrain_tile = m_tiles->get_terrain_tile_from_terrain(terrain);
    m_map_window->tile_image(base_terrain_tile, 3);

    const int distance = map->distance(tile_position, Tile_coordinate{0, 0});
    ImGui::Text("Distance to 0,0: %d", distance);
}

} // namespace hextiles
