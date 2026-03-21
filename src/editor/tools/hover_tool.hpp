#pragma once

#include "tools/tool.hpp"
#include "tools/tool_window.hpp"

#include "app_message.hpp"
#include "erhe_message_bus/message_bus.hpp"

#include <memory>
#include <string>
#include <vector>

namespace erhe::imgui { class Imgui_windows; }

namespace editor {

class App_context;
class App_message_bus;
struct Hover_mesh_message;
struct Hover_tree_node_message;

class Hover_tool : public Tool
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

    void reset_item_tree_hover();

    void add_line(const std::string& line);

private:
    void window_imgui       ();
    void on_message         (Hover_scene_view_message& message);
    void on_hover_mesh      (Hover_mesh_message& message);
    void on_hover_tree_node (Hover_tree_node_message& message);
    [[nodiscard]] auto get_hover_node() const -> std::shared_ptr<erhe::scene::Node>;

    Tool_window              m_window;
    erhe::message_bus::Subscription<Hover_scene_view_message> m_hover_scene_view_subscription;
    erhe::message_bus::Subscription<Hover_mesh_message>       m_hover_mesh_subscription;
    erhe::message_bus::Subscription<Hover_tree_node_message>  m_hover_tree_node_subscription;
    bool                     m_show_hover_normal              {true};
    bool                     m_show_snapped_grid_position     {false};
    bool                     m_geometry_debug_hover_facet_only{true};
    float                    m_normal_length                  {0.5f};
    std::vector<std::string> m_text_lines;

    std::weak_ptr<erhe::scene::Node> m_hovered_node_in_viewport;
    std::weak_ptr<erhe::scene::Node> m_hovered_node_in_item_tree;
};

}
