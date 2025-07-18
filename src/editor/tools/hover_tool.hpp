#pragma once

#include "tools/tool.hpp"
#include "erhe_imgui/imgui_window.hpp"

#include <string>
#include <vector>

namespace erhe::imgui { class Imgui_windows; }

namespace editor {

class App_context;
class App_message_bus;

class Hover_tool
    : public erhe::imgui::Imgui_window
    , public Tool
{
public:
    Hover_tool(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 app_context,
        App_message_bus&             app_message_bus,
        Tools&                       tools
    );

    // Implements Tool
    void tool_render(const Render_context& context) override;

    // Implements Imgui_window
    void imgui() override;

    void reset_item_tree_hover();

    void add_line(const std::string& line);

private:
    void on_message(App_message& message);
    [[nodiscard]] auto get_hover_node() const -> erhe::scene::Node*;

    bool                     m_show_hover_normal              {true};
    bool                     m_show_snapped_grid_position     {false};
    bool                     m_geometry_debug_hover_facet_only{true};
    std::vector<std::string> m_text_lines;

    std::weak_ptr<erhe::scene::Node> m_hovered_node_in_viewport;
    std::weak_ptr<erhe::scene::Node> m_hovered_node_in_item_tree;
};

}
