#pragma once

#include "geometry_graph/geometry_graph.hpp"
#include "graph_editor/graph_editor_window_base.hpp"

#include "config/generated/preview_edge_lines_config.hpp"
#include "erhe_imgui/imgui_window.hpp"

#include <nlohmann/json_fwd.hpp>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

struct ImVec2;

namespace erhe {
    class Item_base;
}
namespace erhe::graph {
    class Graph;
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
// The window does not own THE graph: geometry graphs only live in
// Graph_mesh content-library assets, and the window edits an explicit
// target Graph_mesh (issue #252) set via set_target() - by "Open Editor"
// on the asset, by the target selector at the top of the window, or by
// the MCP graph-create tools. When the target is unset (or the asset was
// deleted - the target is a weak_ptr, so it clears itself) the window
// shows an empty state. The window no longer tracks the global selection,
// so selecting a canvas node does not risk deleting the whole asset.
//
// Evaluation runs in the background so a heavy chain does not freeze the
// UI: update_evaluation() (called once per frame from the editor main
// loop, window visible or not) picks one graph that needs evaluation
// (any Graph_mesh asset in any scene), snapshots it into
// a shadow clone - factory-built nodes with the same parameters, links,
// cached output payloads and dirty flags - and evaluates the shadow on a
// tf::Executor worker. The worker never touches live nodes, so the
// canvas, undo / redo and the MCP mutations stay fully interactive
// during evaluation; when the worker finishes, the payloads (and the
// output nodes' evaluated scene products) are copied back on the main
// thread. At most one run is in flight at a time; edits made while a
// run is in flight simply re-mark nodes dirty and trigger the next run.
class Geometry_graph_window : public Graph_editor_window_base
{
public:
    // title / ini_label default to the primary singleton's values (instance
    // slot 1); the Editor_windows manager passes slot-based values for the
    // extra "Open Editor" instances (issues #252, #265).
    Geometry_graph_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 app_context,
        std::string_view             title     = "Geometry Graph",
        std::string_view             ini_label = "Geometry_graph_window 1"
    );
    ~Geometry_graph_window() noexcept override;

    // Implements Imgui_window. Renders the ax::NodeEditor canvas only; the
    // node palette lives in the companion Graph_editor_palette_window
    // (see controls_imgui()) so the two can be laid out independently.
    void imgui() override;
    auto flags() -> ImGuiWindowFlags override;

    // Renders the edited-asset header ("Editing: <name>" / empty-state hint)
    // and the node-creation palette, plus the background-evaluation status
    // line. Called by the companion palette window, not from this window's
    // imgui().
    void controls_imgui() override;

    // Undoable edits: each creates an Operation and executes it through
    // the operation stack. Used by the toolbar / canvas gestures and the
    // in-editor MCP server. All target the current graph (see
    // get_current_graph_mesh()).
    // Type names: box, sphere, torus, cone, disc, brush, scene_mesh,
    // subdivide, conway_<operator> (plus legacy "conway"), transform,
    // triangulate, normalize, reverse, repair, distribute, instance,
    // realize, join, boolean, float, integer, vector, math, passthrough,
    // output, group_input, group_output, group
    // (see make_geometry_graph_node()). position is the canvas-space position for
    // the new node; nullptr uses the auto-advancing spawn grid.
    auto add_node_of_type(const std::string& type_name, const ImVec2* position = nullptr) -> Geometry_graph_node*;
    void remove_node     (const std::shared_ptr<Geometry_graph_node>& node);
    auto connect         (erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin) -> bool;
    void disconnect      (erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin);

    // Undoable parameter change: applies the (possibly partial) parameter
    // object through read_parameters() and records before / after state
    // in a Geometry_graph_parameter_operation. Used by the MCP
    // geometry_graph_set_parameter tool.
    void set_node_parameters(const std::shared_ptr<Geometry_graph_node>& node, const nlohmann::json& parameters);

    // Show the window (so it renders for a screenshot) and set the node
    // editor's zoom immediately (view scale; > 1 zooms in). Deterministic
    // headless zoom-quality knob used by the #251 verification harness.
    void set_node_editor_zoom(float zoom);

    // Sets the editor-global "Show node previews" setting
    // (Editor_settings_config::graph_node_previews.enabled; persistent, on
    // by default). Turning previews ON forces a full background
    // re-evaluation of every Graph_mesh so clean nodes get preview
    // primitives too. Used by the checkbox in controls_imgui and the MCP
    // geometry_graph_set_node_previews tool.
    void set_node_previews_enabled(bool enabled);

    // Canvas selection setter: clears the ax::NodeEditor selection and
    // selects the given node ids (empty = just clear). A node must have
    // been drawn at least once (the editor context creates canvas nodes on
    // first draw). Used by the in-editor MCP server to drive the Node
    // Properties window headlessly.
    void select_canvas_nodes(const std::vector<std::size_t>& node_ids);

    // Sets / clears the explicit target Graph_mesh this window edits
    // (issue #252). Stored as a weak_ptr so a deleted asset clears the
    // target automatically. Passing null clears the target (empty state).
    void set_target(const std::shared_ptr<Graph_mesh>& graph_mesh);
    [[nodiscard]] auto get_target() -> std::shared_ptr<Graph_mesh>;

    // The graph currently being edited: the resolved target Graph_mesh
    // asset, or null when the target is unset / expired - the window then
    // shows an empty state and edits refuse. Resolved from the weak_ptr
    // target on every access so an MCP mutation arriving before the
    // frame's update still targets the live asset.
    [[nodiscard]] auto get_current_graph_mesh() -> const std::shared_ptr<Graph_mesh>&;

    // Resolves the target first, so MCP reads and node resolution always
    // agree with the graph a subsequent mutation targets. Empty when the
    // target is unset / expired.
    [[nodiscard]] auto get_nodes() -> const std::vector<std::shared_ptr<Geometry_graph_node>>&;

    // Background evaluation (see the class comment). update_evaluation()
    // is the once-per-frame driver; wait_for_idle_evaluation() blocks
    // until EVERY Graph_mesh asset is fully evaluated, for callers that
    // need synchronous semantics (the MCP get_geometry_graph query).
    void update_evaluation();
    void wait_for_idle_evaluation();

    // Non-undoable primitives used by the geometry graph operations
    // (and scene load); prefer the undoable edits above. They target a
    // specific Graph_mesh so undo/redo stays correct even if the
    // selection changes between an edit and its undo (the operations
    // bind the graph they were created for).
    void insert_node    (Graph_mesh& graph_mesh, const std::shared_ptr<Geometry_graph_node>& node);
    void erase_node     (Graph_mesh& graph_mesh, const std::shared_ptr<Geometry_graph_node>& node);
    auto connect_pins   (Graph_mesh& graph_mesh, erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin) -> bool;
    auto disconnect_pins(Graph_mesh& graph_mesh, erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin) -> bool;
    // Implements Graph_editor_window_base: canvas node positions / sizes live
    // in the ax::NodeEditor context keyed by node id, so they are
    // graph-independent (no Graph_mesh argument); selected nodes are the
    // canvas selection (issue #252 - not the global selection).
    [[nodiscard]] auto get_node_position(const Graph_editor_node& node) -> ImVec2 override;
    void set_node_position(const Graph_editor_node& node, const ImVec2& position) override;
    [[nodiscard]] auto get_node_size(const Graph_editor_node& node) -> ImVec2 override;
    [[nodiscard]] auto get_node_editor() -> ax::NodeEditor::EditorContext* override { return m_node_editor.get(); }
    void collect_selected_nodes(std::vector<std::shared_ptr<Graph_editor_node>>& out) override;
    [[nodiscard]] auto find_graph_editor_node(std::size_t node_id) -> std::shared_ptr<Graph_editor_node> override;

private:
    auto make_node       (const std::string& type_name) -> std::shared_ptr<Geometry_graph_node>;
    // Implements Graph_editor_window_base: fills the palette with the geometry
    // node set, and spawns a chosen type through the factory + undoable insert.
    void build_palette   () override;
    void add_node_from_palette(const std::string& type_name, const ImVec2* spawn_position) override;
    // Implements the Graph_editor_window_base clipboard hooks (Cut / Copy /
    // Paste / Duplicate in the canvas context menus and shortcuts).
    [[nodiscard]] auto clipboard_kind() const -> const char* override;
    [[nodiscard]] auto get_current_graph() -> erhe::graph::Graph* override;
    auto paste_nodes(const nlohmann::json& clipboard, const ImVec2& position) -> std::vector<std::size_t> override;
    void remove_nodes(const std::vector<std::shared_ptr<Graph_editor_node>>& nodes) override;
    // Issue #252: the target-item selector row drawn at the top of the
    // window. Drag-drop a Graph_mesh asset onto it, pick from the popup, or
    // clear it. Bound to m_target.
    void target_selector_imgui();
    void handle_link_create();
    void handle_deletions();
    // Drop target covering the canvas: dropping a content-library brush
    // creates a Brush source node at the drop position, bound to that brush.
    void canvas_drag_and_drop_target(const ImVec2& rect_min, const ImVec2& rect_max);

    // Resolves the weak_ptr target into m_graph_mesh (locks it, or null
    // when unset / expired). Resets the spawn grid when the resolved
    // target changes. Replaces the old selection-scanning refresh.
    void resolve_target();

    // Accessors to the currently-edited asset's state. Callers must have
    // checked get_current_graph_mesh() for null (empty state) first.
    [[nodiscard]] auto graph() -> Geometry_graph&;
    [[nodiscard]] auto mutable_nodes() -> std::vector<std::shared_ptr<Geometry_graph_node>>&;

    // Canvas position for the next newly created node: a grid that
    // advances with every add_node_of_type(), so new nodes do not all
    // stack at (0, 0). Reset when the edited asset changes.
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

    // The next graph that needs evaluation: the currently edited graph
    // first (it may be a library orphan), then the Graph_mesh assets of
    // every scene's content library. Null when all are idle.
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

    // Renders pending per-node preview thumbnails (Graph_mesh assets with
    // node previews enabled) into per-node textures via Brush_preview, a
    // few per frame. Called from update_evaluation().
    void update_node_previews();

    App_context&                                      m_app_context;
    // The explicit target this window edits (issue #252). weak_ptr so a
    // deleted asset clears the target automatically. Bound to the target
    // selector widget and set via set_target().
    std::weak_ptr<Graph_mesh>                         m_target;
    // The resolved target: m_target.lock(), refreshed by resolve_target()
    // on every access. Null when the target is unset / expired (empty state).
    std::shared_ptr<Graph_mesh>                       m_graph_mesh;
    std::unique_ptr<ax::NodeEditor::EditorContext>    m_node_editor;
    bool                                              m_focus_requested{false};
    std::shared_ptr<Evaluation_run>                   m_evaluation_run; // non-null while a run is in flight
    int                                               m_spawn_count{0};
    // Reused scratch for the target selector's picker (cleared + refilled each frame).
    std::vector<std::shared_ptr<erhe::Item_base>>     m_target_candidates;
    // Last edge-line settings the previews were rendered with; a change arms
    // a re-render on every cached preview (see update_node_previews).
    Preview_edge_lines_config                         m_preview_edge_lines_snapshot{};
    bool                                              m_preview_edge_lines_snapshot_valid{false};
};

}
