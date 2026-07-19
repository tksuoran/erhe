#pragma once

#include "graph_editor/graph_editor_window_base.hpp"

#include <memory>
#include <unordered_map>

namespace erhe::graph { class Pin; }
namespace erhe::imgui { class Imgui_windows; }
namespace erhe::rendergraph {
    class Rendergraph;
    class Rendergraph_node;
}

namespace ax::NodeEditor { class EditorContext; }

namespace editor {

class App_context;
class Rendergraph_editor_node;

// Read-only viewer for the frame rendergraph, hosted on the shared graph
// editor canvas (Graph_editor_window_base). Rendergraph nodes participate in
// erhe::graph but are not Graph_editor_nodes, so the window maintains one
// proxy Graph_editor_node per registered rendergraph node (mirroring its
// pins) and renders through the exact same Graph_editor_node::node_editor()
// path as the geometry / texture graph editors - same node chrome, pin
// sockets, link arrows, and interactive node resizing. The rendergraph is
// not user-editable from this window, so the editing hooks (palette,
// clipboard, paste / remove) are stubbed.
class Rendergraph_window : public Graph_editor_window_base
{
public:
    Rendergraph_window(erhe::imgui::Imgui_renderer& imgui_renderer, erhe::imgui::Imgui_windows& imgui_windows, App_context& app_context);
    ~Rendergraph_window() noexcept override;

    // Implements Imgui_window
    void imgui() override;
    auto flags() -> ImGuiWindowFlags override;

    // Implements Graph_editor_window_base. Node queries resolve against the
    // proxy nodes; the editing hooks are no-ops (view-only, no palette).
    void controls_imgui() override;
    void collect_selected_nodes(std::vector<std::shared_ptr<Graph_editor_node>>& out) override;
    [[nodiscard]] auto find_graph_editor_node(std::size_t node_id) -> std::shared_ptr<Graph_editor_node> override;
    [[nodiscard]] auto get_node_position(const Graph_editor_node& node) -> ImVec2 override;
    void set_node_position(const Graph_editor_node& node, const ImVec2& position) override;
    [[nodiscard]] auto get_node_size(const Graph_editor_node& node) -> ImVec2 override;
    [[nodiscard]] auto get_node_editor() -> ax::NodeEditor::EditorContext* override;
    // The automatic layout addresses proxies, not the rendergraph nodes.
    [[nodiscard]] auto get_layout_node_id(const erhe::graph::Node& node) const -> std::size_t override;

protected:
    [[nodiscard]] auto clipboard_kind() const -> const char* override;
    [[nodiscard]] auto get_current_graph() -> erhe::graph::Graph* override;
    auto paste_nodes(const nlohmann::json& clipboard, const ImVec2& position) -> std::vector<std::size_t> override;
    void remove_nodes(const std::vector<std::shared_ptr<Graph_editor_node>>& nodes) override;
    void build_palette() override;
    void add_node_from_palette(const std::string& type_name, const ImVec2* spawn_position) override;

private:
    // Creates proxies for newly registered rendergraph nodes, drops proxies
    // of unregistered ones, and (on any change) rebuilds the real-pin ->
    // proxy-pin map used to draw the links.
    void sync_proxy_nodes(erhe::rendergraph::Rendergraph& rendergraph);

    App_context&                                   m_context;
    std::unique_ptr<ax::NodeEditor::EditorContext> m_node_editor;

    // One proxy Graph_editor_node per registered rendergraph node.
    std::unordered_map<erhe::rendergraph::Rendergraph_node*, std::shared_ptr<Rendergraph_editor_node>> m_proxy_nodes;

    // Link endpoints must reference the pins the proxies draw (BeginPin uses
    // the proxy pin's address as the PinId), so the rendergraph's real pins
    // are remapped here. Rebuilt by sync_proxy_nodes() when the node set
    // changes; pin addresses are stable (etl::vector storage inside the
    // heap-allocated nodes).
    std::unordered_map<const erhe::graph::Pin*, const erhe::graph::Pin*> m_pin_remap;
};

}
