#pragma once

#include "erhe_imgui/imgui_window.hpp"

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

struct ImVec2;

namespace ax::NodeEditor { class EditorContext; }
namespace erhe::graph {
    class Graph;
    class Link;
    class Node;
}

namespace editor {

class App_context;
class Graph_editor_node;

// Common base for the node-graph editor windows (the geometry graph and the
// texture graph). It carries the seam the companion palette window forwards to
// (controls_imgui) and the payload-blind node palette (the searchable,
// categorized node-creation list). The canvas / link / target machinery still
// lives in the concrete windows; growing this base further is deferred Phase C
// work (see doc/graph-editor-shared-plan.md, C7 remainder).
class Graph_editor_window_base : public erhe::imgui::Imgui_window
{
public:
    using erhe::imgui::Imgui_window::Imgui_window;

    // Draws the node palette (search box + categories). The companion
    // Graph_editor_palette_window forwards here so the palette and the canvas
    // can be docked / sized independently.
    virtual void controls_imgui() = 0;

    // Stable per-instance slot (1 = the primary singleton owned by Editor,
    // 2... = the extra "Open Editor" instances) carried in the window title
    // and the "<Type>_graph_window N" ini label. The lowest free slot is
    // reused after an extra instance is destroyed, so window layout and open
    // state persist across sessions (issue #265).
    void set_instance_slot(int slot) { m_instance_slot = slot; }
    [[nodiscard]] auto get_instance_slot() const -> int { return m_instance_slot; }

    // Node Properties window support. Node selection lives purely in each
    // window's ax::NodeEditor context (issue #252 - it is deliberately not
    // synced into the global selection), so the Node Properties window asks
    // every graph editor window for its canvas-selected nodes directly.
    // Appends this window's selected nodes to out.
    virtual void collect_selected_nodes(std::vector<std::shared_ptr<Graph_editor_node>>& out) = 0;

    // Appends this window's canvas-selected links to out. Link selection -
    // like node selection - lives in the window's ax::NodeEditor context;
    // the returned pointers stay valid while the link exists in the graph.
    void collect_selected_links(std::vector<erhe::graph::Link*>& out);

    // The node with the given item id in the currently edited graph, or null.
    [[nodiscard]] virtual auto find_graph_editor_node(std::size_t node_id) -> std::shared_ptr<Graph_editor_node> = 0;

    // Canvas node geometry, keyed by node id in the window's ax::NodeEditor
    // context (graph-independent). Size is the content-derived on-canvas
    // extent (read-only by nature; adjust through the node's ui scale).
    [[nodiscard]] virtual auto get_node_position(const Graph_editor_node& node) -> ImVec2 = 0;
    virtual void set_node_position(const Graph_editor_node& node, const ImVec2& position) = 0;
    [[nodiscard]] virtual auto get_node_size(const Graph_editor_node& node) -> ImVec2 = 0;

    // The window's ax::NodeEditor context (canvas state: node positions, link
    // routing mid points). Used by the clipboard machinery and the MCP graph
    // tools; never null once the window is constructed.
    [[nodiscard]] virtual auto get_node_editor() -> ax::NodeEditor::EditorContext* = 0;

    // Spawns a palette node type from outside the window (inventory / hotbar
    // slots): shows the window and creates the node on the auto-advancing
    // spawn grid. No-op in the empty state (no graph being edited).
    void spawn_node_from_slot(const std::string& type_name);

    // The primary graph editor window whose clipboard_kind() matches, or
    // nullptr (used to route inventory / hotbar graph-node slots and dropped
    // palette payloads to the right editor).
    [[nodiscard]] static auto find_window_by_kind(App_context& context, const char* kind) -> Graph_editor_window_base*;

    // Shared automatic layout (all graph editors). Nodes flow left to right:
    // sources on the left, sinks on the right, one column per depth, a
    // node's column chosen by its longest link distance to the deepest node
    // of its subgraph (so sinks right-align). One longest path is laid on a
    // single horizontal line; the other nodes of each column stack below it.
    // The graph need not be connected: each disjoint subgraph is laid out
    // separately, largest first, stacked top to bottom.
    //
    // The layout is applied deferred by apply_automatic_layout(): node sizes
    // are measured canvas extents, so it waits until every node has been
    // drawn with a stable size (a node can grow a frame after it first
    // appears, e.g. when a preview thumbnail arrives).
    void request_automatic_layout();

    // Requests a plain grid layout instead of the DAG layout: nodes in graph
    // order, row-major, each cell sized to the widest / tallest node so nothing
    // overlaps. Used by "Add all" (below), where every node is unconnected and
    // the DAG layout would degenerate to one node per connected component and
    // stack them in a single tall column. Shares the deferred-size machinery of
    // request_automatic_layout(): the layout waits for measured sizes.
    void request_grid_layout();

    // Spawns one node of every palette type and grid-lays the result
    // (request_grid_layout). Payload-blind, like node_palette() - it walks
    // m_palette_categories and spawns through add_nodes_from_palette(), so a
    // newly registered node type is included without touching this code.
    // Intended for visual verification: it puts the whole node library on the
    // canvas at once, where a node that fails to compose, renders black or
    // mis-lays-out its widgets is obvious at a glance. Public so the canvas
    // context menu, and the texture_graph_add_all MCP tool, share one path.
    void add_all_palette_nodes();

protected:
    // One selectable palette entry (a spawnable node type).
    class Palette_entry
    {
    public:
        std::string type_name;
        std::string label;
    };
    // One palette category (e.g. "Primitives") and its entries.
    class Palette_category
    {
    public:
        std::string                name;
        std::vector<Palette_entry> entries;
    };

    // Searchable, categorized node palette: a filter box and one collapsing
    // header per category whose selectables spawn a node. Payload-blind - it
    // reads m_palette_categories (filled lazily by build_palette()) and spawns
    // via add_node_from_palette().
    void node_palette();

    // Right-click "Add node" menu on the canvas background: a submenu per
    // palette category whose items spawn a node. Call inside the ax::NodeEditor
    // Begin/End, bracketed by the editor's own Suspend/Resume. The editor
    // context is still owned by the concrete window (passed in here).
    void node_background_context_menu(ax::NodeEditor::EditorContext& node_editor);

    // Accepts a node-palette payload (c_graph_node_payload_type) of this
    // editor's kind. Must be called inside an open drag-drop target block
    // covering the canvas: while the drag is in flight it draws a ghost of
    // the prospective node at the cursor; on delivery it spawns the node at
    // the drop position.
    void accept_palette_drop(ax::NodeEditor::EditorContext& node_editor, const ImVec2& rect_min, const ImVec2& rect_max);

    // Whole-canvas drag-drop target that accepts only palette payloads
    // (accept_palette_drop). For windows without their own canvas target;
    // call right after the canvas End().
    void palette_drop_target(ax::NodeEditor::EditorContext& node_editor, const ImVec2& rect_min, const ImVec2& rect_max);

    // Ghost rectangle for an in-flight canvas drop: node footprint (canvas
    // units) anchored with its top-left corner at the cursor - where the
    // spawned node's origin lands - scaled to the canvas zoom, clipped to
    // the canvas rect, with a label inside.
    void draw_canvas_drop_ghost(
        ax::NodeEditor::EditorContext& node_editor,
        const ImVec2&                  rect_min,
        const ImVec2&                  rect_max,
        const char*                    label,
        const ImVec2&                  footprint
    );

    // Adopts an active interactive node-resize drag (EnableNodeResize on the
    // editor context): while the user drags a node's edge / corner, applies
    // the reported size to the node's requested extent (Node Properties
    // "Size") and the reported position (left / top edges move the node).
    // Call right after the canvas End().
    void apply_node_resize(ax::NodeEditor::EditorContext& node_editor);

    // Applies a pending request_automatic_layout() once every canvas node
    // has a stable measured size, then frames the result on the next frame
    // (NavigateToContent). Call right after the canvas End().
    void apply_automatic_layout();

    // Canvas node id for a graph node. The graph editors draw graph nodes
    // directly, so the canvas id is the node's item id (default); the
    // rendergraph viewer draws proxy nodes and overrides this to map to them.
    [[nodiscard]] virtual auto get_layout_node_id(const erhe::graph::Node& node) const -> std::size_t;

    // Cut / Copy / Paste / Duplicate for canvas nodes. The clipboard is
    // editor-global (one per process, tagged with clipboard_kind()), so nodes
    // copy between windows - and between graphs - of the same editor kind.
    // Copy serializes the selected nodes (factory type + parameters + layout +
    // canvas position) and the links among them; paste rebuilds them through
    // the concrete window's factory as one undo step, preserving relative
    // layout anchored at the paste position, and selects the pasted nodes.

    // Right-click menu on a canvas node: Cut / Copy / Paste / Duplicate /
    // Delete, acting on the canvas selection (right-clicking a node outside
    // the selection first retargets the selection to it). Call inside the
    // ax::NodeEditor Begin/End, like node_background_context_menu().
    void node_context_menu(ax::NodeEditor::EditorContext& node_editor);

    // The node editor's built-in Ctrl+X / Ctrl+C / Ctrl+V / Ctrl+D shortcuts
    // on a focused canvas, routed to the same actions as the context menu.
    // Call inside the ax::NodeEditor Begin/End.
    void handle_clipboard_shortcuts(ax::NodeEditor::EditorContext& node_editor);

    // Applies the canvas selection deferred by a paste / duplicate: the
    // editor context creates canvas nodes on first draw, so freshly pasted
    // nodes can only be selected after the node draw loop of the next frame.
    // Call right after the node draw loop, inside Begin/End.
    void apply_pending_canvas_selection(ax::NodeEditor::EditorContext& node_editor);

    // Serializes the nodes to the editor-global clipboard (no-op when empty).
    void copy_nodes(const std::vector<std::shared_ptr<Graph_editor_node>>& nodes);
    // Whether the clipboard holds content this editor kind can paste.
    [[nodiscard]] auto can_paste() const -> bool;
    // Pastes the clipboard with the block's top-left corner at the given
    // canvas position (no-op when can_paste() is false).
    void paste_clipboard(const ImVec2& canvas_position);
    // Copy + paste slightly offset, without touching the shared clipboard.
    void duplicate_nodes(const std::vector<std::shared_ptr<Graph_editor_node>>& nodes);

    // Per-editor hooks for the clipboard machinery.
    // Tags clipboard content ("geometry_graph" / "texture_graph") so paste
    // only accepts content from the same editor kind.
    [[nodiscard]] virtual auto clipboard_kind() const -> const char* = 0;
    // The currently edited graph, or nullptr in the empty state.
    [[nodiscard]] virtual auto get_current_graph() -> erhe::graph::Graph* = 0;
    // Rebuilds clipboard-format JSON as one undo step (paste_graph_nodes with
    // the concrete factory / operation types); returns the new node ids.
    virtual auto paste_nodes(const nlohmann::json& clipboard, const ImVec2& position) -> std::vector<std::size_t> = 0;
    // Removes the nodes as one undo step (remove_graph_nodes).
    virtual void remove_nodes(const std::vector<std::shared_ptr<Graph_editor_node>>& nodes) = 0;

    // Fills m_palette_categories once (the node set is fixed per editor).
    virtual void build_palette() = 0;
    // Spawns the node type chosen in the palette (the concrete window creates it
    // through its factory + an undoable insert operation). spawn_position is the
    // canvas-space position for the new node, or nullptr to use the window's
    // auto-advancing spawn grid (the background context menu passes the position
    // where the right-click opened the menu; the palette list passes nullptr).
    virtual void add_node_from_palette(const std::string& type_name, const ImVec2* spawn_position) = 0;

    // Spawns one node per given type. The default loops add_node_from_palette(),
    // i.e. one undo entry per node; an editor that can batch overrides this to
    // push a single compound undo entry (adding the whole library one node at a
    // time would otherwise take one Ctrl+Z per node to back out).
    virtual void add_nodes_from_palette(const std::vector<std::string>& type_names);

    // Extra items appended to the canvas background context menu, after the
    // palette submenus. Empty by default; an editor overrides it to offer
    // actions that only make sense there. Runs inside the open popup, with the
    // node editor suspended (screen space).
    virtual void background_context_menu_extra_items();

    std::vector<Palette_category> m_palette_categories; // built lazily by build_palette()
    std::string                   m_palette_filter;     // node-palette search text
    int                           m_instance_slot{1};   // primary singletons are slot 1

private:
    // Clipboard-format JSON for the given nodes (see graph_clipboard.hpp);
    // null when the selection or the edited graph is empty.
    [[nodiscard]] auto serialize_nodes_json(const std::vector<std::shared_ptr<Graph_editor_node>>& nodes) -> nlohmann::json;

    // Node ids to select once they have been drawn (paste / duplicate),
    // applied by apply_pending_canvas_selection().
    std::vector<std::size_t>                        m_pending_canvas_selection;
    // Automatic layout request state (apply_automatic_layout()): the layout
    // waits for every node's measured size to be stable across two frames
    // before applying, then frames the content one frame later.
    enum class Layout_mode : unsigned int { dag = 0, grid = 1 };
    Layout_mode                                     m_layout_mode{Layout_mode::dag};
    bool                                            m_automatic_layout_pending {false};
    bool                                            m_navigate_to_content_pending{false};
    float                                           m_layout_size_sum{-1.0f};
    // Reused scratch for the context menu / shortcut selection queries
    // (cleared at point of use, capacity kept).
    std::vector<std::shared_ptr<Graph_editor_node>> m_selected_nodes_scratch;
};

} // namespace editor
