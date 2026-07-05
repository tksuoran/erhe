#pragma once

#include "erhe_imgui/imgui_window.hpp"

#include <string>
#include <vector>

namespace ax::NodeEditor { class EditorContext; }

namespace editor {

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

    // Fills m_palette_categories once (the node set is fixed per editor).
    virtual void build_palette() = 0;
    // Spawns the node type chosen in the palette (the concrete window creates it
    // through its factory + an undoable insert operation).
    virtual void add_node_from_palette(const std::string& type_name) = 0;

    std::vector<Palette_category> m_palette_categories; // built lazily by build_palette()
    std::string                   m_palette_filter;     // node-palette search text
};

} // namespace editor
