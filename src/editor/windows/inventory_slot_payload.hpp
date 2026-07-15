#pragma once

#include "config/generated/operation_params.hpp"

namespace erhe::primitive { class Material; }
namespace erhe::commands { class Command; }

namespace editor {

class Brush;
class Tool;

// ImGui drag-and-drop payload type string for inventory slot drags
constexpr const char* c_inventory_slot_payload_type = "Inventory_Slot";

// POD payload for drag-and-drop (safe for memcpy by ImGui). Operation_params is
// all-scalar and the graph-node fields are fixed-size char arrays, so the
// whole struct remains trivially copyable.
class Slot_drag_payload
{
public:
    enum class Section : int { palette = 0, grid = 1, hotbar = 2 };

    Tool*                      tool    {nullptr};
    Brush*                     brush   {nullptr};
    erhe::primitive::Material* material{nullptr};
    erhe::commands::Command*   command {nullptr}; // Non-null for operation slots
    Operation_params           params  {};        // Frozen params for the operation slot
    char                       graph_node_kind [32]{}; // Non-empty (with type) for graph-node slots
    char                       graph_node_type [64]{}; // Node factory type name
    char                       graph_node_label[64]{}; // Display label
    Section                    section {Section::palette};
    int                        index   {-1};
};

}
