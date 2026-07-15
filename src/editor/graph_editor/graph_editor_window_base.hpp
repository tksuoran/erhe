#pragma once

#include "erhe_imgui/imgui_window.hpp"

#include <memory>
#include <string>
#include <vector>

struct ImVec2;

namespace ax::NodeEditor { class EditorContext; }

namespace editor {

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

    // The node with the given item id in the currently edited graph, or null.
    [[nodiscard]] virtual auto find_graph_editor_node(std::size_t node_id) -> std::shared_ptr<Graph_editor_node> = 0;

    // Canvas node geometry, keyed by node id in the window's ax::NodeEditor
    // context (graph-independent). Size is the content-derived on-canvas
    // extent (read-only by nature; adjust through the node's ui scale).
    [[nodiscard]] virtual auto get_node_position(const Graph_editor_node& node) -> ImVec2 = 0;
    virtual void set_node_position(const Graph_editor_node& node, const ImVec2& position) = 0;
    [[nodiscard]] virtual auto get_node_size(const Graph_editor_node& node) -> ImVec2 = 0;

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

    // Adopts an active interactive node-resize drag (EnableNodeResize on the
    // editor context): while the user drags a node's edge / corner, applies
    // the reported size to the node's requested extent (Node Properties
    // "Size") and the reported position (left / top edges move the node).
    // Call right after the canvas End().
    void apply_node_resize(ax::NodeEditor::EditorContext& node_editor);

    // Fills m_palette_categories once (the node set is fixed per editor).
    virtual void build_palette() = 0;
    // Spawns the node type chosen in the palette (the concrete window creates it
    // through its factory + an undoable insert operation).
    virtual void add_node_from_palette(const std::string& type_name) = 0;

    std::vector<Palette_category> m_palette_categories; // built lazily by build_palette()
    std::string                   m_palette_filter;     // node-palette search text
    int                           m_instance_slot{1};   // primary singletons are slot 1
};

} // namespace editor
