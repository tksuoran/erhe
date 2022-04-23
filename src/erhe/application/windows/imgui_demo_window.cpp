#include "erhe/application/windows/imgui_demo_window.hpp"
#include "erhe/application/imgui_windows.hpp"

#include <imgui.h>

namespace erhe::application
{

Imgui_demo_window::Imgui_demo_window()
    : erhe::components::Component{c_name}
    , Imgui_window               {c_title}
{
}

Imgui_demo_window::~Imgui_demo_window() = default;

void Imgui_demo_window::connect()
{
    require<Imgui_windows>();
}

void Imgui_demo_window::initialize_component()
{
    get<Imgui_windows>()->register_imgui_window(this);
}

void Imgui_demo_window::imgui()
{
    ImGui::ShowDemoWindow();
}

} // namespace erhe::application
