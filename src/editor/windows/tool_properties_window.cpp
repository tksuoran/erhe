#include "windows/tool_properties_window.hpp"

#include "tools/tool.hpp"
#include "windows/operations.hpp"

#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/toolkit/profile.hpp"

#include <gsl/gsl>

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

Tool_properties_window::Tool_properties_window()
    : erhe::components::Component    {c_type_name}
    , erhe::application::Imgui_window{c_title, c_type_name}
{
}

Tool_properties_window::~Tool_properties_window() noexcept
{
}

void Tool_properties_window::declare_required_components()
{
    require<erhe::application::Imgui_windows>();
}

void Tool_properties_window::initialize_component()
{
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);
}

void Tool_properties_window::post_initialize()
{
    m_operations = get<Operations>();
}

void Tool_properties_window::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
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
#endif
}

} // namespace editor
