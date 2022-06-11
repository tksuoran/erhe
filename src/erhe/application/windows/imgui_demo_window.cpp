#include "erhe/application/windows/imgui_demo_window.hpp"
#include "erhe/application/imgui_windows.hpp"
#include "erhe/toolkit/profile.hpp"

#include <imgui.h>

namespace erhe::application
{

Imgui_demo_window::Imgui_demo_window()
    : erhe::components::Component{c_label}
    , Imgui_window               {c_title, c_label}
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
    ERHE_PROFILE_FUNCTION

    ImGui::ShowDemoWindow();
}

} // namespace erhe::application
