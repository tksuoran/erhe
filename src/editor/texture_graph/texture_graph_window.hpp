#pragma once

#include "texture_graph/texture_graph.hpp"
#include "texture_graph/texture_renderer.hpp"

#include "erhe_imgui/imgui_window.hpp"

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct ImVec2;

namespace erhe::graph {
    class Pin;
}
namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_windows;
}
namespace ax::NodeEditor {
    class EditorContext;
}

namespace editor {

class App_context;
class Texture_graph_node;

// ImGui window hosting the texture node graph editor.
//
// Mirrors Geometry_graph_window's structure - toolbar, ax::NodeEditor canvas,
// link creation and node / link deletion, selection integration - but with the
// cheap synchronous evaluation of Texture_graph (no background shadow-clone
// engine): update() runs the dirty-flag evaluation once per frame from the
// editor main loop, window visible or not, so composition stays current even
// when the window is hidden.
//
// This is the Phase 3 Step 1 foundation: the canvas, add-node / connect /
// disconnect primitives, and synchronous evaluation. The concrete node set,
// preview / output rendering, serialization, undo/redo and the MCP surface are
// added in later steps; the edit methods below are structured so those bolt on
// the way they do for the geometry graph.
class Texture_graph_window : public erhe::imgui::Imgui_window
{
public:
    Texture_graph_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 app_context
    );
    ~Texture_graph_window() noexcept override;

    // Implements Imgui_window
    void imgui() override;
    auto flags() -> ImGuiWindowFlags override;

    // Runs the synchronous dirty-flag evaluation. Called once per frame from
    // the editor main loop (see editor.cpp) so the graph stays evaluated even
    // when the window is not visible.
    void update();

    // Undoable edits: each creates an Operation and executes it through the
    // operation stack. Used by the toolbar / canvas gestures (and, from Step 5,
    // the in-editor MCP server).
    auto add_node_of_type(const std::string& type_name) -> Texture_graph_node*;
    void remove_node     (const std::shared_ptr<Texture_graph_node>& node);
    auto connect         (erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin) -> bool;
    void disconnect      (erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin);

    // Undoable parameter change: applies the (possibly partial) parameter
    // object through read_parameters() and records before / after state in a
    // Texture_graph_parameter_operation.
    void set_node_parameters(const std::shared_ptr<Texture_graph_node>& node, const nlohmann::json& parameters);

    [[nodiscard]] auto get_graph() -> Texture_graph&;
    [[nodiscard]] auto get_nodes() const -> const std::vector<std::shared_ptr<Texture_graph_node>>&;

    // Graph serialization (JSON: node types + parameters + canvas positions,
    // links by node index + pin slot). Load and clear are undoable (single
    // Texture_graph_replace_operation).
    auto save_graph (const std::filesystem::path& path) -> bool;
    auto load_graph (const std::filesystem::path& path) -> bool;
    void clear_graph();

    // Non-undoable primitives (also used by future graph operations / load).
    void insert_node    (const std::shared_ptr<Texture_graph_node>& node);
    void erase_node     (const std::shared_ptr<Texture_graph_node>& node);
    auto connect_pins   (erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin) -> bool;
    auto disconnect_pins(erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin) -> bool;
    [[nodiscard]] auto get_node_position(const Texture_graph_node& node) -> ImVec2;
    void set_node_position(const Texture_graph_node& node, const ImVec2& position);

private:
    auto make_node       (const std::string& type_name) -> std::shared_ptr<Texture_graph_node>;
    void file_toolbar    ();
    void node_toolbar    ();
    void handle_link_create();
    void handle_deletions();

    // Canvas position for the next newly created node: a grid that advances
    // with every add_node_of_type(), so new nodes do not all stack at (0, 0).
    auto next_node_spawn_position() -> ImVec2;

    // Renders the products (preview thumbnails, output bakes) of nodes whose
    // composition changed this frame. Main thread, records into the frame
    // command buffer. Cheap in steady state (no dirty nodes -> no work).
    void render_dirty_products();

    App_context&                                     m_app_context;
    Texture_graph                                    m_graph;
    std::unique_ptr<Texture_renderer>                m_renderer;
    std::unique_ptr<ax::NodeEditor::EditorContext>   m_node_editor;
    std::vector<std::shared_ptr<Texture_graph_node>> m_nodes;
    std::string                                      m_graph_path{"res/editor/graphs/texture_graph.json"};
    int                                              m_spawn_count{0};
};

} // namespace editor
