#include "windows/tool_properties_window.hpp"

#include "tools/tool.hpp"
#include "windows/operations.hpp"

#include "erhe/application/imgui_windows.hpp"
#include "erhe/toolkit/profile.hpp"

#include <gsl/gsl>
#include <imgui.h>

namespace editor
{

Tool_properties_window::Tool_properties_window()
    : erhe::components::Component    {c_label}
    , erhe::application::Imgui_window{c_title, c_label}
{
}

Tool_properties_window::~Tool_properties_window() noexcept
{
}

void Tool_properties_window::connect()
{
    m_operations = get<Operations>();
    require<erhe::application::Imgui_windows>();
}

void Tool_properties_window::initialize_component()
{
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
}

void Tool_properties_window::imgui()
{
    ERHE_PROFILE_FUNCTION

    if (!m_operations)
    {
        return;
    }
    auto* const tool = m_operations->get_active_tool();
    if (tool == nullptr)
    {
        return;
    }
    tool->tool_properties();
}

} // namespace editor
