#include "map_editor/map_tool_window.hpp"
#include "map_editor/map_editor.hpp"
#include "map_editor/terrain_palette_window.hpp"
#include "map_generator/map_generator.hpp"
#include "map.hpp"
#include "map_window.hpp"
#include "menu_window.hpp"
#include "tiles.hpp"

#include "erhe/application/imgui_windows.hpp"
#include "erhe/application/renderers/imgui_renderer.hpp"

#include <gsl/assert>

namespace hextiles
{

Map_tool_window::Map_tool_window()
    : erhe::components::Component{c_label}
    , Imgui_window               {c_title, c_label}
{
}

Map_tool_window::~Map_tool_window()
{
}

void Map_tool_window::connect()
{
    m_map_editor     = get<Map_editor>();
    m_map_window     = get<Map_window>();
    m_imgui_renderer = get<erhe::application::Imgui_renderer>();
}

void Map_tool_window::initialize_component()
{
    Expects(m_components != nullptr);
    Imgui_window::initialize(*m_components);
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
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

    auto mouse_input_sink = m_editor_view->mouse_input_sink();
    if (mouse_input_sink != this)
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
    const auto&     map     = m_map_editor->get_map();
    const terrain_t terrain = map->get_terrain(tile_position);

    const auto& terrain_shapes = m_tiles->get_terrain_shapes();
    if (terrain >= terrain_shapes.size())
    {
        return;
    }
    const auto base_terrain = m_tiles->get_base_terrain(terrain);
    if (base_terrain >= terrain_shapes.size())
    {
        return;
    }

    const auto& terrain_type = m_tiles->get_terrain_type(base_terrain);

    ImGui::Text("Tile @ %d, %d %s", tile_position.x, tile_position.y, terrain_type.name.c_str());
    ImGui::Text("Terrain: %d, base: %d", terrain, base_terrain);
    ImGui::Text("City Size: %d", terrain_type.city_size);

    m_map_window->tile_image(terrain, 3);
    ImGui::SameLine();
    m_map_window->tile_image(base_terrain, 3);

    const int distance = map->distance(tile_position, Tile_coordinate{0, 0});
    ImGui::Text("Distance to 0,0: %d", distance);
}

} // namespace hextiles
