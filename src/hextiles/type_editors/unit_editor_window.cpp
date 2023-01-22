#include "type_editors/unit_editor_window.hpp"
#include "tiles.hpp"
#include "menu_window.hpp"
#include "type_editors/type_editor.hpp"

#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/toolkit/verify.hpp"

#include <imgui.h>

namespace hextiles
{

Unit_editor_window* g_unit_editor_window{nullptr};

Unit_editor_window::Unit_editor_window()
    : erhe::components::Component{c_type_name}
    , Imgui_window               {c_title}
{
}

Unit_editor_window::~Unit_editor_window() noexcept
{
    ERHE_VERIFY(g_unit_editor_window == this);
    g_unit_editor_window = nullptr;
}

void Unit_editor_window::declare_required_components()
{
    require<erhe::application::Imgui_windows>();
}

void Unit_editor_window::initialize_component()
{
    ERHE_VERIFY(g_unit_editor_window == nullptr);
    erhe::application::g_imgui_windows->register_imgui_window(this);
    hide();
    g_unit_editor_window = this;
}

void Unit_editor_window::imgui()
{
    constexpr ImVec2 button_size{110.0f, 0.0f};

    if (ImGui::Button("Back to Menu", button_size))
    {
        g_menu_window->show_menu();
    }
    ImGui::SameLine();

    if (ImGui::Button("Load", button_size))
    {
        g_tiles->load_unit_defs();
    }
    ImGui::SameLine();

    if (ImGui::Button("Save", button_size))
    {
        g_tiles->save_unit_defs();
    }

    g_type_editor->unit_editor_imgui();
}

} // namespace hextiles
