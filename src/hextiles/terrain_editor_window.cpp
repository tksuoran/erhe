#include "terrain_editor_window.hpp"
#include "tiles.hpp"
#include "type_editor.hpp"
#include "map_window.hpp"
#include "menu_window.hpp"

#include "erhe/application/imgui_windows.hpp"

#include <imgui.h>

namespace hextiles
{

Terrain_editor_window::Terrain_editor_window()
    : erhe::components::Component{c_name}
    , Imgui_window               {c_name, c_title}
{
}

Terrain_editor_window::~Terrain_editor_window()
{
}

void Terrain_editor_window::initialize_component()
{
    Imgui_window::initialize(*m_components);
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
}

void Terrain_editor_window::imgui()
{
    constexpr ImVec2 button_size{110.0f, 0.0f};

    if (ImGui::Button("Back to Menu", button_size))
    {
        hide();
        get<Menu_window>()->show();
    }
    ImGui::SameLine();

    if (ImGui::Button("Load", button_size))
    {
        get<Tiles>()->load_terrain_defs();
    }
    ImGui::SameLine();

    if (ImGui::Button("Save", button_size))
    {
        get<Tiles>()->save_terrain_defs();
    }

    get<Type_editor>()->terrain_editor_imgui();
}

//

Terrain_group_editor_window::Terrain_group_editor_window()
    : erhe::components::Component{c_name}
    , Imgui_window               {c_name, c_title}
{
}

Terrain_group_editor_window::~Terrain_group_editor_window()
{
}

void Terrain_group_editor_window::initialize_component()
{
    Imgui_window::initialize(*m_components);
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
}

void Terrain_group_editor_window::imgui()
{
    constexpr ImVec2 button_size{110.0f, 0.0f};

    if (ImGui::Button("Back to Menu", button_size))
    {
        hide();
        get<Menu_window>()->show();
    }
    ImGui::SameLine();

    if (ImGui::Button("Load", button_size))
    {
        get<Tiles>()->load_terrain_group_defs();
    }
    ImGui::SameLine();

    if (ImGui::Button("Save", button_size))
    {
        get<Tiles>()->save_terrain_group_defs();
    }

    //if (ImGui::Button("Update", button_size))
    //{
    //    get<Tiles>()->update_terrain_groups();
    //    get<Map_window>()->update_elevation_terrains();
    //}

    get<Type_editor>()->terrain_group_editor_imgui();

    get<Tiles>()->update_terrain_groups();
    get<Map_window>()->update_elevation_terrains();
}

//

Terrain_replacement_rule_editor_window::Terrain_replacement_rule_editor_window()
    : erhe::components::Component{c_name}
    , Imgui_window               {c_name, c_title}
{
}

Terrain_replacement_rule_editor_window::~Terrain_replacement_rule_editor_window()
{
}

void Terrain_replacement_rule_editor_window::initialize_component()
{
    Imgui_window::initialize(*m_components);
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
}

void Terrain_replacement_rule_editor_window::imgui()
{
    constexpr ImVec2 button_size{110.0f, 0.0f};

    if (ImGui::Button("Back to Menu", button_size))
    {
        hide();
        get<Menu_window>()->show();
    }
    ImGui::SameLine();

    if (ImGui::Button("Load", button_size))
    {
        get<Tiles>()->load_terrain_replacement_rule_defs();
    }
    ImGui::SameLine();

    if (ImGui::Button("Save", button_size))
    {
        get<Tiles>()->save_terrain_replacement_rule_defs();
    }

    get<Type_editor>()->terrain_replacement_rule_editor_imgui();

    get<Tiles>()->update_terrain_groups();
    get<Map_window>()->update_elevation_terrains();
}

} // namespace hextiles
