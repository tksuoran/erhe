#include "editor_tools.hpp"
#include "editor_imgui_windows.hpp"
#include "windows/imgui_window.hpp"

#include "tools/tool.hpp"

namespace editor {

Editor_tools::Editor_tools()
    : erhe::components::Component{c_name}
{
}

Editor_tools::~Editor_tools() = default;

void Editor_tools::connect()
{
    m_editor_imgui_windows = require<Editor_imgui_windows>();
}

void Editor_tools::initialize_component()
{
}

void Editor_tools::register_tool(Tool* tool)
{
    const std::lock_guard<std::mutex> lock{m_mutex};

    m_tools.emplace_back(tool);
}

void Editor_tools::register_background_tool(Tool* tool)
{
    const std::lock_guard<std::mutex> lock{m_mutex};

    m_background_tools.emplace_back(tool);
}

void Editor_tools::render_tools(const Render_context& context)
{
    for (const auto& tool : m_background_tools)
    {
        tool->tool_render(context);
    }
    for (const auto& tool : m_tools)
    {
        tool->tool_render(context);
    }
}

}  // namespace editor
