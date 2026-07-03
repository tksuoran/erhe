#pragma once

#include "geometry_graph/geometry_graph.hpp"

#include "erhe_imgui/imgui_window.hpp"

#include <nlohmann/json_fwd.hpp>

#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
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
class Geometry_graph_node;

// ImGui window hosting the geometry node graph editor.
//
// Follows the Graph_window (shader graph) pattern: toolbar for creating
// nodes, ax::NodeEditor canvas rendering all nodes, link creation and
// node / link deletion handling, selection integration.
//
// Evaluation runs in the background so a heavy chain does not freeze the
// UI: update_evaluation() (called once per frame from the editor main
// loop, window visible or not) snapshots the live graph into a shadow
// clone - factory-built nodes with the same parameters, links, cached
// output payloads and dirty flags - and evaluates the shadow on a
// tf::Executor worker. The worker never touches live nodes, so the
// canvas, undo / redo and the MCP mutations stay fully interactive
// during evaluation; when the worker finishes, the payloads (and the
// output nodes' evaluated scene products) are copied back on the main
// thread. Edits made while a run is in flight simply re-mark nodes
// dirty and trigger the next run.
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

    // Undoable edits: each creates an Operation and executes it through
    // the operation stack. Used by the toolbar / canvas gestures and the
    // in-editor MCP server.
    // Type names: box, sphere, torus, cone, disc, subdivide, conway,
    // transform, triangulate, normalize, reverse, repair, distribute,
    // instance, realize, join, boolean, float, integer, vector, math,
    // output, group_input, group_output, group (see
    // make_geometry_graph_node()).
    auto add_node_of_type(const std::string& type_name) -> Geometry_graph_node*;
    void remove_node     (const std::shared_ptr<Geometry_graph_node>& node);
    auto connect         (erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin) -> bool;
    void disconnect      (erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin);

    // Undoable parameter change: applies the (possibly partial) parameter
    // object through read_parameters() and records before / after state
    // in a Geometry_graph_parameter_operation. Used by the MCP
    // geometry_graph_set_parameter tool.
    void set_node_parameters(const std::shared_ptr<Geometry_graph_node>& node, const nlohmann::json& parameters);

    [[nodiscard]] auto get_graph() -> Geometry_graph&;
    [[nodiscard]] auto get_nodes() const -> const std::vector<std::shared_ptr<Geometry_graph_node>>&;

    // Graph serialization (JSON: node types + parameters + canvas
    // positions, links by node index + pin slot). Load and clear are
    // undoable (single Geometry_graph_replace_operation).
    auto save_graph (const std::filesystem::path& path) -> bool;
    auto load_graph (const std::filesystem::path& path) -> bool;
    void clear_graph();

    // Background evaluation (see the class comment). update_evaluation()
    // is the once-per-frame driver; wait_for_idle_evaluation() blocks
    // until the graph is fully evaluated, for callers that need
    // synchronous semantics (the MCP get_geometry_graph query and graph
    // file saves).
    void update_evaluation();
    void wait_for_idle_evaluation();

    // Non-undoable primitives used by the geometry graph operations
    // (and graph load); prefer the undoable edits above.
    void insert_node    (const std::shared_ptr<Geometry_graph_node>& node);
    void erase_node     (const std::shared_ptr<Geometry_graph_node>& node);
    auto connect_pins   (erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin) -> bool;
    auto disconnect_pins(erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin) -> bool;
    [[nodiscard]] auto get_node_position(const Geometry_graph_node& node) -> ImVec2;
    void set_node_position(const Geometry_graph_node& node, const ImVec2& position);

private:
    auto make_node       (const std::string& type_name) -> std::shared_ptr<Geometry_graph_node>;
    void file_toolbar    ();
    void node_toolbar    ();
    void handle_link_create();
    void handle_deletions();

    // Canvas position for the next newly created node: a grid that
    // advances with every add_node_of_type(), so new nodes do not all
    // stack at (0, 0). Reset by clear_graph().
    auto next_node_spawn_position() -> ImVec2;

    // One background evaluation: the shadow graph the worker evaluates
    // plus the completion handshake. Owned via shared_ptr so the worker
    // keeps the shadow alive even if the window is destroyed mid-run.
    class Evaluation_run
    {
    public:
        Geometry_graph                                    shadow_graph;
        std::vector<std::shared_ptr<Geometry_graph_node>> shadow_nodes;
        std::vector<std::size_t>                          live_node_ids; // parallel to shadow_nodes
        std::mutex                                        mutex;
        std::condition_variable                           condition;
        bool                                              done{false};   // guarded by mutex
    };

    void launch_evaluation();
    void finish_evaluation(); // pre: m_evaluation_run && done
    [[nodiscard]] auto is_evaluation_run_done() -> bool;

    App_context&                                      m_app_context;
    Geometry_graph                                    m_graph;
    std::unique_ptr<ax::NodeEditor::EditorContext>    m_node_editor;
    std::vector<std::shared_ptr<Geometry_graph_node>> m_nodes;
    std::shared_ptr<Evaluation_run>                   m_evaluation_run; // non-null while a run is in flight
    std::string                                       m_graph_path{"res/editor/graphs/geometry_graph.json"};
    int                                               m_spawn_count{0};
};

}
