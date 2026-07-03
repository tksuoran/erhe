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
class Graph_mesh;

// ImGui window hosting the geometry node graph editor.
//
// Follows the Graph_window (shader graph) pattern: toolbar for creating
// nodes, ax::NodeEditor canvas rendering all nodes, link creation and
// node / link deletion handling, selection integration.
//
// The window does not own THE graph: geometry graphs live in Graph_mesh
// content-library assets (plus one window-owned scratch fallback), and
// the window edits whichever Graph_mesh is currently selected - the
// same selected-asset model as Texture_graph_window / Graph_texture.
//
// Evaluation runs in the background so a heavy chain does not freeze the
// UI: update_evaluation() (called once per frame from the editor main
// loop, window visible or not) picks one graph that needs evaluation
// (the scratch or any Graph_mesh asset in any scene), snapshots it into
// a shadow clone - factory-built nodes with the same parameters, links,
// cached output payloads and dirty flags - and evaluates the shadow on a
// tf::Executor worker. The worker never touches live nodes, so the
// canvas, undo / redo and the MCP mutations stay fully interactive
// during evaluation; when the worker finishes, the payloads (and the
// output nodes' evaluated scene products) are copied back on the main
// thread. At most one run is in flight at a time; edits made while a
// run is in flight simply re-mark nodes dirty and trigger the next run.
class Geometry_graph_window : public erhe::imgui::Imgui_window
{
public:
    Geometry_graph_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 app_context
    );
    ~Geometry_graph_window() noexcept override;

    // Implements Imgui_window. Renders the ax::NodeEditor canvas only; the file
    // toolbar and node toolbar live in the companion Geometry_graph_palette_window
    // (see controls_imgui()) so the two can be laid out independently.
    void imgui() override;
    auto flags() -> ImGuiWindowFlags override;

    // Renders the file toolbar (Save / Load / Clear) and the node-creation
    // toolbar, plus the background-evaluation status line. Called by the
    // companion palette window, not from this window's imgui().
    void controls_imgui();

    // Undoable edits: each creates an Operation and executes it through
    // the operation stack. Used by the toolbar / canvas gestures and the
    // in-editor MCP server. All target the current graph (see
    // get_current_graph_mesh()).
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

    // The graph currently being edited: the selected content-library
    // Graph_mesh asset, or the window's scratch graph when nothing (of
    // that type) is selected. Refreshed on every access so an MCP
    // mutation arriving before the frame's update still targets the
    // live selection.
    [[nodiscard]] auto get_current_graph_mesh() -> const std::shared_ptr<Graph_mesh>&;

    // Both refresh the current graph from the live selection first, so
    // MCP reads and node resolution always agree with the graph a
    // subsequent mutation targets.
    [[nodiscard]] auto get_graph() -> Geometry_graph&;
    [[nodiscard]] auto get_nodes() -> const std::vector<std::shared_ptr<Geometry_graph_node>>&;

    // Graph serialization (JSON: node types + parameters + canvas
    // positions, links by node index + pin slot). Load and clear are
    // undoable (single Geometry_graph_replace_operation). All target
    // the current graph.
    auto save_graph (const std::filesystem::path& path) -> bool;
    auto load_graph (const std::filesystem::path& path) -> bool;
    void clear_graph();

    // Background evaluation (see the class comment). update_evaluation()
    // is the once-per-frame driver; wait_for_idle_evaluation() blocks
    // until EVERY graph (scratch + all Graph_mesh assets) is fully
    // evaluated, for callers that need synchronous semantics (the MCP
    // get_geometry_graph query and graph file saves).
    void update_evaluation();
    void wait_for_idle_evaluation();

    // Non-undoable primitives used by the geometry graph operations
    // (and graph load); prefer the undoable edits above. They target a
    // specific Graph_mesh so undo/redo stays correct even if the
    // selection changes between an edit and its undo (the operations
    // bind the graph they were created for).
    void insert_node    (Graph_mesh& graph_mesh, const std::shared_ptr<Geometry_graph_node>& node);
    void erase_node     (Graph_mesh& graph_mesh, const std::shared_ptr<Geometry_graph_node>& node);
    auto connect_pins   (Graph_mesh& graph_mesh, erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin) -> bool;
    auto disconnect_pins(Graph_mesh& graph_mesh, erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin) -> bool;
    // Canvas node positions live in the ax::NodeEditor context keyed by
    // node id, so they are graph-independent (no Graph_mesh argument).
    [[nodiscard]] auto get_node_position(const Geometry_graph_node& node) -> ImVec2;
    void set_node_position(const Geometry_graph_node& node, const ImVec2& position);

private:
    auto make_node       (const std::string& type_name) -> std::shared_ptr<Geometry_graph_node>;
    // Creates a Graph_mesh asset in the (single) scene's content library and
    // selects it, so the window switches from the scratch graph to editing a
    // real, saveable asset. Bound to the toolbar "New Graph Mesh" button.
    void create_and_select_graph_mesh();
    void file_toolbar    ();
    void node_toolbar    ();
    void handle_link_create();
    void handle_deletions();

    // Points m_graph_mesh at the selected content-library Graph_mesh, or
    // the window's scratch graph when nothing (of that type) is selected.
    void refresh_current_graph_mesh();

    // Accessors to the currently-edited graph's state (the selected
    // asset or the scratch). Used by the canvas iteration and the
    // serialization surface.
    [[nodiscard]] auto graph() -> Geometry_graph&;
    [[nodiscard]] auto mutable_nodes() -> std::vector<std::shared_ptr<Geometry_graph_node>>&;

    // Canvas position for the next newly created node: a grid that
    // advances with every add_node_of_type(), so new nodes do not all
    // stack at (0, 0). Reset by clear_graph().
    auto next_node_spawn_position() -> ImVec2;

    // One background evaluation: the target graph, the shadow graph the
    // worker evaluates, plus the completion handshake. Owned via
    // shared_ptr so the worker keeps the shadow alive even if the window
    // is destroyed mid-run (and the target alive even if the asset is
    // removed from its library mid-run).
    class Evaluation_run
    {
    public:
        std::shared_ptr<Graph_mesh>                       target;
        Geometry_graph                                    shadow_graph;
        std::vector<std::shared_ptr<Geometry_graph_node>> shadow_nodes;
        std::vector<std::size_t>                          live_node_ids; // parallel to shadow_nodes
        std::mutex                                        mutex;
        std::condition_variable                           condition;
        bool                                              done{false};   // guarded by mutex
    };

    // The next graph that needs evaluation: the scratch first, then the
    // Graph_mesh assets of every scene's content library. Null when all
    // are idle.
    [[nodiscard]] auto next_graph_needing_evaluation() -> std::shared_ptr<Graph_mesh>;

    void launch_evaluation(const std::shared_ptr<Graph_mesh>& graph_mesh);
    void finish_evaluation(); // pre: m_evaluation_run && done
    [[nodiscard]] auto is_evaluation_run_done() -> bool;

    // Pushes the graph's freshly published baked products to every
    // Geometry_graph_mesh attachment bound to it, in every scene. Called
    // at the end of finish_evaluation(); main thread.
    void apply_baked_products_to_attachments(const std::shared_ptr<Graph_mesh>& graph_mesh);

    // Honors Graph_mesh::request_attachment_push() flags once per frame
    // (pushes that need no evaluation - node re-entered a scene, output
    // node removed).
    void process_attachment_push_requests();

    App_context&                                      m_app_context;
    // The window's own fallback graph, edited when no Graph_mesh asset is
    // selected. A benign scratch surface (mirrors Texture_graph_window's
    // default Graph_texture): the selected asset always takes precedence,
    // and the scratch is never referenced by a scene node attachment.
    std::shared_ptr<Graph_mesh>                       m_scratch_graph_mesh;
    // The graph currently being edited (selected asset or the scratch).
    std::shared_ptr<Graph_mesh>                       m_graph_mesh;
    std::unique_ptr<ax::NodeEditor::EditorContext>    m_node_editor;
    std::shared_ptr<Evaluation_run>                   m_evaluation_run; // non-null while a run is in flight
    std::string                                       m_graph_path{"res/editor/graphs/geometry_graph.json"};
    int                                               m_spawn_count{0};
};

// Companion window hosting the Geometry Graph's file toolbar and node toolbar,
// so the palette and the canvas can be docked / sized independently. It owns no
// graph state; it forwards to Geometry_graph_window::controls_imgui().
class Geometry_graph_palette_window : public erhe::imgui::Imgui_window
{
public:
    Geometry_graph_palette_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Geometry_graph_window&       graph_window
    );

    // Implements Imgui_window
    void imgui() override;

private:
    Geometry_graph_window& m_graph_window;
};

}
