#pragma once

#include <string_view>
#include <vector>

namespace erhe::scene {
    class Xformable; using Node = Xformable;
}

namespace editor {

class Scene_commands;

// What a catalog entry adds to a node.
enum class Attachment_kind : unsigned int {
    // A typed prim placed under the node as a child
    // (doc/usd-compatibility-plan.md C5): Mesh, Camera, Light.
    child_prim,
    // A Node_attachment applied to the node, the way USD applies an API
    // schema to a prim: rigid body, joint, layout, grid, frame controller.
    api_schema
};

// One user-addable kind for the "Add Child Prim" / "Add Attachment" UI
// (Properties window and Hierarchy context menu) and the
// add_node_attachment MCP tool.
//
// can_add / make are stateless free functions (function pointers, no heap):
//   can_add(node) gates the entry (duplicate / precondition, e.g. a node may
//                 hold at most one Layout).
//   make(scene_commands, node) queues the undoable operation(s) via
//                 Scene_commands.
class Attachment_type_info
{
public:
    std::string_view key;          // stable catalog key (MCP argument, e.g. "camera")
    std::string_view display_name; // menu label (e.g. "Camera")
    Attachment_kind  kind;
    bool (*can_add)(const erhe::scene::Node& node);
    void (*make)   (Scene_commands& scene_commands, erhe::scene::Node& node);
};

// The user-addable attachment kinds, in menu order. Built once (static).
[[nodiscard]] auto get_attachment_types() -> const std::vector<Attachment_type_info>&;

// Finds a catalog entry by key, or nullptr when the key is unknown.
[[nodiscard]] auto find_attachment_type(std::string_view key) -> const Attachment_type_info*;

} // namespace editor
