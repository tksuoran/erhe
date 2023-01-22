#include "map_editor/terrain_palette_window.hpp"

#include "map_editor/map_editor.hpp"

#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/toolkit/verify.hpp"

namespace hextiles
{

Terrain_palette_window* g_terrain_palette_window{nullptr};

Terrain_palette_window::Terrain_palette_window()
    : erhe::components::Component{c_type_name}
    , Imgui_window               {c_title}
{
}

Terrain_palette_window::~Terrain_palette_window() noexcept
{
    ERHE_VERIFY(g_terrain_palette_window == this);
    g_terrain_palette_window = nullptr;
}

void Terrain_palette_window::declare_required_components()
{
    require<erhe::application::Imgui_windows>();
}

void Terrain_palette_window::initialize_component()
{
    ERHE_VERIFY(g_terrain_palette_window == nullptr);

    erhe::application::g_imgui_windows->register_imgui_window(this);
    hide();

    g_terrain_palette_window = this;
}

void Terrain_palette_window::imgui()
{
    g_map_editor->terrain_palette();
}

} // namespace hextiles
