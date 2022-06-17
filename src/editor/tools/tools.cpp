#include "tools/tools.hpp"
#include "tools/tool.hpp"

#include "erhe/application/imgui_windows.hpp"
#include "erhe/application/windows/imgui_window.hpp"

namespace editor {

Tools::Tools()
    : erhe::components::Component{c_label}
{
}

Tools::~Tools() noexcept
{
}

void Tools::post_initialize()
{
    m_imgui_windows = get<erhe::application::Imgui_windows>();
}

void Tools::register_tool(Tool* tool)
{
    const std::lock_guard<std::mutex> lock{m_mutex};

    m_tools.emplace_back(tool);
}

void Tools::register_background_tool(Tool* tool)
{
    const std::lock_guard<std::mutex> lock{m_mutex};

    m_background_tools.emplace_back(tool);
}

void Tools::render_tools(const Render_context& context)
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

}  // namespace erhe::application
