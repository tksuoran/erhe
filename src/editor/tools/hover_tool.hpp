#pragma once

#include "tools/tool.hpp"
#include "erhe_imgui/imgui_window.hpp"

#include <string>
#include <vector>

namespace erhe::imgui {
    class Imgui_windows;
}

namespace editor {

class Editor_context;
class Editor_message_bus;

class Hover_tool
    : public erhe::imgui::Imgui_window
    , public Tool
{
public:
    Hover_tool(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Editor_context&              editor_context,
        Editor_message_bus&          editor_message_bus,
        Tools&                       tools
    );

    // Implements Tool
    void tool_render(const Render_context& context) override;

    // Implements Imgui_window
    void imgui() override;

    void add_line(const std::string& line);

private:
    bool m_show_snapped_grid_position{false};

    std::vector<std::string> m_text_lines;
};

} // namespace editor
