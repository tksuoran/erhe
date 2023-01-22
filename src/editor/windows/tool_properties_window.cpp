#include "windows/tool_properties_window.hpp"

#include "tools/tool.hpp"
#include "tools/tools.hpp"

#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

#include <gsl/gsl>

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

Tool_properties_window* g_tool_properties_window{nullptr};

Tool_properties_window::Tool_properties_window()
    : erhe::components::Component    {c_type_name}
    , erhe::application::Imgui_window{c_title}
{
}

Tool_properties_window::~Tool_properties_window() noexcept
{
    ERHE_VERIFY(g_tool_properties_window == this);
    g_tool_properties_window = nullptr;
}

void Tool_properties_window::declare_required_components()
{
    require<erhe::application::Imgui_windows>();
}

void Tool_properties_window::initialize_component()
{
    ERHE_VERIFY(g_tool_properties_window == nullptr);
    erhe::application::g_imgui_windows->register_imgui_window(this);
    g_tool_properties_window = this;
}

void Tool_properties_window::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION

    if (g_tools == nullptr)
    {
        return;
    }
    auto* const tool = g_tools->get_priority_tool();
    if (tool == nullptr)
    {
        return;
    }
    tool->tool_properties();
#endif
}

} // namespace editor
