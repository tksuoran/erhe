#include "windows/imgui_demo_window.hpp"
#include "tools.hpp"

#include <imgui.h>

namespace editor
{

Imgui_demo_window::Imgui_demo_window()
    : erhe::components::Component{c_name}
    , Imgui_window               {c_title}
{
}

Imgui_demo_window::~Imgui_demo_window() = default;

void Imgui_demo_window::initialize_component()
{
    get<Editor_tools>()->register_imgui_window(this);
}

void Imgui_demo_window::imgui(Pointer_context&)
{
    ImGui::ShowDemoWindow();
}

}
