#include "windows/tool_properties_window.hpp"

#include "app_context.hpp"
#include "tools/tool.hpp"
#include "tools/tools.hpp"

#include "erhe_imgui/imgui_windows.hpp"

namespace editor {

Tool_properties_window::Tool_properties_window(erhe::imgui::Imgui_renderer& imgui_renderer, erhe::imgui::Imgui_windows& imgui_windows, App_context& app_context)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Tool Properties", "tool_properties"}
    , m_context                {app_context}
{
}

void Tool_properties_window::imgui()
{
    //m_context.hotbar->imgui();
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    auto* const tool = m_context.tools->get_priority_tool();
    if (tool == nullptr) {
        return;
    }
    tool->tool_properties(*this);
#endif
}

}
