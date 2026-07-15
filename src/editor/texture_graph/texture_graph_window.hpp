#pragma once

#include "texture_graph/texture_graph.hpp"
#include "texture_graph/texture_renderer.hpp"
#include "graph_editor/graph_editor_window_base.hpp"

#include "erhe_imgui/imgui_window.hpp"

#include <nlohmann/json_fwd.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct ImVec2;

namespace erhe {
    class Item_base;
}
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
class Graph_texture;
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
class Texture_graph_window : public Graph_editor_window_base
{
public:
    // title / ini_label default to the primary singleton's values (instance
    // slot 1); the Editor_windows manager passes slot-based values for the
    // extra "Open Editor" instances (issues #252, #265).
    Texture_graph_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 app_context,
        std::string_view             title     = "Texture Graph",
        std::string_view             ini_label = "Texture_graph_window 1"
    );
    ~Texture_graph_window() noexcept override;

    // Implements Imgui_window. Renders the ax::NodeEditor canvas only; the
    // node palette lives in the companion Graph_editor_palette_window
    // (see controls_imgui()) so the two can be laid out independently.
    void imgui() override;
    auto flags() -> ImGuiWindowFlags override;

    // Renders the edited-asset header ("Editing: <name>" / empty-state hint,
    // Reseed all) and the searchable node palette. Called by the companion
    // palette window, not from this window's imgui(). Kept here because it
    // operates on this window's graph state and must stay outside the
    // ax::NodeEditor canvas Begin/End.
    void controls_imgui() override;

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

    // Sets / clears the explicit target Graph_texture this window edits
    // (issue #252). Stored as a weak_ptr so a deleted asset clears the
    // target automatically. Passing null clears the target (empty state).
    void set_target(const std::shared_ptr<Graph_texture>& graph_texture);
    [[nodiscard]] auto get_target() -> std::shared_ptr<Graph_texture>;

    // The graph currently being edited: the resolved target Graph_texture
    // asset, or null when the target is unset / expired - the window then
    // shows an empty state and edits refuse. Resolved from the weak_ptr
    // target on every access (update() / imgui() / MCP). MCP and the canvas
    // gestures operate on this one.
    [[nodiscard]] auto get_current_graph_texture() -> const std::shared_ptr<Graph_texture>&;

    // Empty when the target is unset / expired.
    [[nodiscard]] auto get_nodes() const -> const std::vector<std::shared_ptr<Texture_graph_node>>&;

    // The shared render-to-texture helper. Created lazily on first use (the part
    // ctor must not touch App_context, so the graphics device is not available
    // during construction). Returns nullptr only when no graphics device exists.
    // Used by the in-editor MCP server's texture_graph_export_png tool.
    [[nodiscard]] auto get_renderer() -> Texture_renderer*;

    // Assigns every seeded node a fresh deterministic seed (each an individually
    // undoable parameter change). Bound to the palette "Reseed all" button.
    void reseed_all();

    // Non-undoable primitives, targeting a specific Graph_texture so undo/redo
    // stays correct even if the selection changes between an edit and its undo
    // (the operations bind the graph they were created for). Called by the
    // graph operations and by scene load.
    void insert_node    (Graph_texture& graph_texture, const std::shared_ptr<Texture_graph_node>& node);
    void erase_node     (Graph_texture& graph_texture, const std::shared_ptr<Texture_graph_node>& node);
    auto connect_pins   (Graph_texture& graph_texture, erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin) -> bool;
    auto disconnect_pins(Graph_texture& graph_texture, erhe::graph::Pin* source_pin, erhe::graph::Pin* sink_pin) -> bool;
    // Implements Graph_editor_window_base: canvas node positions / sizes live
    // in the ax::NodeEditor context keyed by node id, so they are
    // graph-independent (no Graph_texture argument); selected nodes are the
    // canvas selection (issue #252 - not the global selection).
    [[nodiscard]] auto get_node_position(const Graph_editor_node& node) -> ImVec2 override;
    void set_node_position(const Graph_editor_node& node, const ImVec2& position) override;
    [[nodiscard]] auto get_node_size(const Graph_editor_node& node) -> ImVec2 override;
    void collect_selected_nodes(std::vector<std::shared_ptr<Graph_editor_node>>& out) override;

    // Canvas selection setter: clears the ax::NodeEditor selection and
    // selects the given node ids (empty = just clear). A node must have
    // been drawn at least once (the editor context creates canvas nodes on
    // first draw). Used by the in-editor MCP server to drive the Node
    // Properties window headlessly.
    void select_canvas_nodes(const std::vector<std::size_t>& node_ids);

private:
    auto make_node       (const std::string& type_name) -> std::shared_ptr<Texture_graph_node>;
    // Implements Graph_editor_window_base: fills the palette from the texture
    // descriptor registry, and spawns a chosen type through the factory + an
    // undoable insert.
    void build_palette   () override;
    void add_node_from_palette(const std::string& type_name) override;
    // Issue #252: the target-item selector row drawn at the top of the
    // window. Drag-drop a Graph_texture asset onto it, pick from the popup,
    // or clear it. Bound to m_target.
    void target_selector_imgui();
    void handle_link_create();
    void handle_deletions();

    // Canvas position for the next newly created node: a grid that advances
    // with every add_node_of_type(), so new nodes do not all stack at (0, 0).
    auto next_node_spawn_position() -> ImVec2;

    // Evaluates a graph texture's dirty subtrees and renders the products
    // (preview thumbnails, output bakes) of nodes whose composition changed this
    // frame. Main thread, records into the frame command buffer. Cheap in steady
    // state (no dirty nodes -> no work). update() runs this for the current graph
    // and every Graph_texture in every scene's content library, so a graph a
    // material samples stays baked even when it is not the selected one.
    void evaluate_and_render(Graph_texture& graph_texture);

    // Resolves the weak_ptr target into m_graph_texture (locks it, or null
    // when unset / expired). Resets the spawn grid when the resolved target
    // changes. Replaces the old selection-scanning refresh.
    void resolve_target();

    // Accessors to the currently-edited asset's state. Callers must have
    // checked get_current_graph_texture() for null (empty state) first.
    [[nodiscard]] auto graph() -> Texture_graph&;
    [[nodiscard]] auto mutable_nodes() -> std::vector<std::shared_ptr<Texture_graph_node>>&;

    App_context&                                     m_app_context;
    // The explicit target this window edits (issue #252). weak_ptr so a
    // deleted asset clears the target automatically. Bound to the target
    // selector widget and set via set_target().
    std::weak_ptr<Graph_texture>                     m_target;
    // The resolved target: m_target.lock(), refreshed by resolve_target()
    // on every access. Null when the target is unset / expired (empty state).
    std::shared_ptr<Graph_texture>                   m_graph_texture;
    std::unique_ptr<Texture_renderer>                m_renderer;
    std::unique_ptr<ax::NodeEditor::EditorContext>   m_node_editor;
    int                                              m_spawn_count{0};
    // Reused scratch for the target selector's picker (cleared + refilled each frame).
    std::vector<std::shared_ptr<erhe::Item_base>>    m_target_candidates;
};

} // namespace editor
