#include "unit_editor_window.hpp"
#include "tiles.hpp"
#include "type_editor.hpp"
#include "menu_window.hpp"

#include "erhe/application/imgui_windows.hpp"

#include <imgui.h>

namespace hextiles
{

Unit_editor_window::Unit_editor_window()
    : erhe::components::Component{c_name}
    , Imgui_window               {c_name, c_title}
{
}

Unit_editor_window::~Unit_editor_window()
{
}

void Unit_editor_window::initialize_component()
{
    Imgui_window::initialize(*m_components);
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
}

void Unit_editor_window::imgui()
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
        get<Tiles>()->load_unit_defs();
    }
    ImGui::SameLine();

    if (ImGui::Button("Save", button_size))
    {
        get<Tiles>()->save_unit_defs();
    }

    get<Type_editor>()->unit_editor_imgui();
}

} // namespace hextiles
