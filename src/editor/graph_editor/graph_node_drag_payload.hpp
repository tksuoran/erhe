#pragma once

namespace editor {

// ImGui drag-and-drop payload type string for a node type dragged out of a
// graph editor's node palette (accepted by the graph editor canvases and by
// the inventory / hotbar slots).
constexpr const char* c_graph_node_payload_type = "Graph_palette_node";

// POD payload, safe for ImGui's memcpy of the drag-drop blob. Fixed-size
// copies of the palette entry are carried (not pointers), so the payload
// cannot dangle when the palette rebuilds between drag and drop.
class Graph_node_drag_payload
{
public:
    char kind     [32]{}; // editor kind tag (Graph_editor_window_base::clipboard_kind())
    char type_name[64]{}; // node factory type name (add_node_from_palette argument)
    char label    [64]{}; // palette display label
};

}
