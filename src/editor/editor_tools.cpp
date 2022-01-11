#include "editor_tools.hpp"
#include "editor_imgui_windows.hpp"
#include "windows/imgui_window.hpp"

#include "tools/tool.hpp"

namespace editor {

using namespace std;

Editor_tools::Editor_tools()
    : erhe::components::Component{c_name}
{
}

Editor_tools::~Editor_tools() = default;

void Editor_tools::connect()
{
    m_editor_imgui_windows = require<Editor_imgui_windows>();
}

void Editor_tools::register_tool(Tool* tool)
{
    lock_guard<mutex> lock{m_mutex};

    m_tools.emplace_back(tool);
    auto* const imgui_window = dynamic_cast<Imgui_window*>(tool);
    if (imgui_window != nullptr)
    {
        m_editor_imgui_windows->register_imgui_window(imgui_window);
    }
}

void Editor_tools::register_background_tool(Tool* tool)
{
    lock_guard<mutex> lock{m_mutex};

    m_background_tools.emplace_back(tool);
    auto* const imgui_window = dynamic_cast<Imgui_window*>(tool);
    if (imgui_window != nullptr)
    {
        m_editor_imgui_windows->register_imgui_window(imgui_window);
    }
}

void Editor_tools::render_tools(const Render_context& context)
{
    for (auto tool : m_background_tools)
    {
        tool->tool_render(context);
    }
    for (auto tool : m_tools)
    {
        tool->tool_render(context);
    }
}

void Editor_tools::begin_frame()
{
    for (auto tool : m_background_tools)
    {
        tool->begin_frame();
    }
    for (auto tool : m_tools)
    {
        tool->begin_frame();
    }
}

}  // namespace editor
