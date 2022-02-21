#include "windows/tool_properties_window.hpp"
#include "editor_imgui_windows.hpp"
#include "windows/operations.hpp"
#include "tools/tool.hpp"

#include <gsl/gsl>
#include <imgui.h>

namespace editor
{

Tool_properties_window::Tool_properties_window()
    : erhe::components::Component{c_name}
    , Imgui_window               {c_title}
{
}

Tool_properties_window::~Tool_properties_window() = default;

void Tool_properties_window::connect()
{
    m_operations = get<Operations>();
    require<Editor_imgui_windows>();
}

void Tool_properties_window::initialize_component()
{
    get<Editor_imgui_windows>()->register_imgui_window(this);
}

void Tool_properties_window::imgui()
{
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
