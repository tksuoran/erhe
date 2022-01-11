#include "windows/imgui_demo_window.hpp"
#include "editor_imgui_windows.hpp"

#include <imgui.h>

namespace editor
{

Imgui_demo_window::Imgui_demo_window()
    : erhe::components::Component{c_name}
    , Imgui_window               {c_title}
{
}

Imgui_demo_window::~Imgui_demo_window() = default;

void Imgui_demo_window::connect()
{
    require<Editor_imgui_windows>();
}

void Imgui_demo_window::initialize_component()
{
    get<Editor_imgui_windows>()->register_imgui_window(this);

    hide();
}

void Imgui_demo_window::imgui()
{
    ImGui::ShowDemoWindow();
}

}
