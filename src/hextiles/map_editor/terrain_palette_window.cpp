#include "map_editor/terrain_palette_window.hpp"
#include "map_editor/map_editor.hpp"

#include "erhe/application/imgui_windows.hpp"

namespace hextiles
{

Terrain_palette_window::Terrain_palette_window()
    : erhe::components::Component{c_label}
    , Imgui_window               {c_title, c_label}
{
}

Terrain_palette_window::~Terrain_palette_window() noexcept
{
}

void Terrain_palette_window::declare_required_components()
{
    require<erhe::application::Imgui_windows>();
}

void Terrain_palette_window::initialize_component()
{
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
}

void Terrain_palette_window::post_initialize()
{
    m_map_editor = get<Map_editor>();
}

void Terrain_palette_window::imgui()
{
    m_map_editor->terrain_palette();
}

} // namespace hextiles
