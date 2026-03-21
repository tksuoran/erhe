#include "tools/tool_window.hpp"

namespace editor {

Tool_window::Tool_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    const std::string_view       title,
    const std::string_view       ini_label,
    std::function<void()>        imgui_callback,
    const bool                   developer
)
    : Imgui_window    {imgui_renderer, imgui_windows, title, ini_label, developer}
    , m_imgui_callback{std::move(imgui_callback)}
{
}

void Tool_window::imgui()
{
    m_imgui_callback();
}

auto Tool_window::flags() -> ImGuiWindowFlags
{
    return flags_callback ? flags_callback() : Imgui_window::flags();
}

void Tool_window::on_begin()
{
    if (on_begin_callback) {
        on_begin_callback();
    } else {
        Imgui_window::on_begin();
    }
}

void Tool_window::on_end()
{
    if (on_end_callback) {
        on_end_callback();
    } else {
        Imgui_window::on_end();
    }
}

} // namespace editor
