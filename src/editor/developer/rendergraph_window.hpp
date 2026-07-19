#pragma once

#include "graph_editor/graph_editor_window_base.hpp"

#include <memory>

namespace erhe::imgui { class Imgui_windows; }

namespace ax::NodeEditor { class EditorContext; }

namespace editor {

class App_context;

// Read-only viewer for the frame rendergraph, hosted on the shared graph
// editor canvas (Graph_editor_window_base). Rendergraph nodes participate in
// erhe::graph but are not Graph_editor_nodes and the rendergraph is not
// user-editable from this window, so the editing hooks (palette, clipboard,
// node properties) are stubbed; the canvas itself (pan / zoom, node dragging,
// selection, link rendering) comes from ax::NodeEditor like in the other
// graph editor windows.
class Rendergraph_window : public Graph_editor_window_base
{
public:
    Rendergraph_window(erhe::imgui::Imgui_renderer& imgui_renderer, erhe::imgui::Imgui_windows& imgui_windows, App_context& app_context);
    ~Rendergraph_window() noexcept override;

    // Implements Imgui_window
    void imgui() override;
    auto flags() -> ImGuiWindowFlags override;

    // Implements Graph_editor_window_base. View-only: no palette, and the
    // rendergraph nodes are not Graph_editor_nodes, so the node hooks are
    // no-ops (Node Properties and the clipboard see an empty selection).
    void controls_imgui() override;
    void collect_selected_nodes(std::vector<std::shared_ptr<Graph_editor_node>>& out) override;
    [[nodiscard]] auto find_graph_editor_node(std::size_t node_id) -> std::shared_ptr<Graph_editor_node> override;
    [[nodiscard]] auto get_node_position(const Graph_editor_node& node) -> ImVec2 override;
    void set_node_position(const Graph_editor_node& node, const ImVec2& position) override;
    [[nodiscard]] auto get_node_size(const Graph_editor_node& node) -> ImVec2 override;
    [[nodiscard]] auto get_node_editor() -> ax::NodeEditor::EditorContext* override;

protected:
    [[nodiscard]] auto clipboard_kind() const -> const char* override;
    [[nodiscard]] auto get_current_graph() -> erhe::graph::Graph* override;
    auto paste_nodes(const nlohmann::json& clipboard, const ImVec2& position) -> std::vector<std::size_t> override;
    void remove_nodes(const std::vector<std::shared_ptr<Graph_editor_node>>& nodes) override;
    void build_palette() override;
    void add_node_from_palette(const std::string& type_name, const ImVec2* spawn_position) override;

private:
    App_context&                                   m_context;
    float                                          m_image_size{100.0f};
    std::unique_ptr<ax::NodeEditor::EditorContext> m_node_editor;
};

}
