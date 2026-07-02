#pragma once

#include "geometry_graph/geometry_graph.hpp"

#include "erhe_imgui/imgui_window.hpp"

#include <memory>
#include <string>
#include <vector>

namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_windows;
}
namespace ax::NodeEditor {
    class EditorContext;
}

namespace editor {

class App_context;
class Geometry_graph_node;

// ImGui window hosting the geometry node graph editor.
//
// Follows the Graph_window (shader graph) pattern: toolbar for creating
// nodes, ax::NodeEditor canvas rendering all nodes, link creation and
// node / link deletion handling, selection integration.
class Geometry_graph_window : public erhe::imgui::Imgui_window
{
public:
    Geometry_graph_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 app_context
    );
    ~Geometry_graph_window() noexcept override;

    // Implements Imgui_window
    void imgui() override;
    auto flags() -> ImGuiWindowFlags override;

    // Programmatic access (used by the in-editor MCP server).
    // Type names: box, sphere, torus, cone, disc, subdivide, conway,
    // transform, triangulate, normalize, reverse, repair, join, boolean,
    // float, integer, vector, math, output.
    auto add_node_of_type(const std::string& type_name) -> Geometry_graph_node*;
    [[nodiscard]] auto get_graph() -> Geometry_graph&;
    [[nodiscard]] auto get_nodes() const -> const std::vector<std::shared_ptr<Geometry_graph_node>>&;

private:
    auto make_node       (const std::string& type_name) -> std::shared_ptr<Geometry_graph_node>;
    void add_node        (const std::shared_ptr<Geometry_graph_node>& node);
    void node_toolbar    ();
    void handle_link_create();
    void handle_deletions();

    App_context&                                      m_app_context;
    Geometry_graph                                    m_graph;
    std::unique_ptr<ax::NodeEditor::EditorContext>    m_node_editor;
    std::vector<std::shared_ptr<Geometry_graph_node>> m_nodes;
};

}
