#pragma once

#include <string_view>
#include <vector>

namespace erhe {
    class Hierarchy;
}
namespace erhe::scene {
    class Xformable; using Node = Xformable;
}

namespace editor {

class Scene_commands;

// One user-creatable typed prim that the Hierarchy context menu "Create"
// lists beside the kinds Scene_commands builds directly, and that the
// add_node_attachment MCP tool accepts: Mesh, Camera, Light. Any prim parents
// any prim (doc/usd_compatibility_design.md C5), so the parent is the Hierarchy
// it is and every parent takes any number of these.
//
// make(scene_commands, parent) queues the undoable insert of a new prim as
// the last child of parent via Scene_commands.
class Child_prim_type_info
{
public:
    std::string_view key;          // stable catalog key (MCP argument, e.g. "camera")
    std::string_view display_name; // menu label (e.g. "Camera")
    void (*make)(Scene_commands& scene_commands, erhe::Hierarchy& parent);
};

// One user-addable Node_attachment kind, applied to a node the way USD applies
// an API schema to a prim: rigid body, joint, layout, grid, frame controller,
// draw mode. The Hierarchy context menu "Add Attachment" lists these, and the
// add_node_attachment MCP tool accepts them.
//
// can_add / make are stateless free functions (function pointers, no heap):
//   can_add(node) gates the entry (duplicate / precondition, e.g. a node may
//                 hold at most one Layout).
//   make(scene_commands, node) queues the undoable operation(s) via
//                 Scene_commands.
class Attachment_type_info
{
public:
    std::string_view key;          // stable catalog key (MCP argument, e.g. "layout")
    std::string_view display_name; // menu label (e.g. "Layout")
    bool (*can_add)(const erhe::scene::Node& node);
    void (*make)   (Scene_commands& scene_commands, erhe::scene::Node& node);
};

// The user-creatable child prim kinds, in menu order. Built once (static).
[[nodiscard]] auto get_child_prim_types() -> const std::vector<Child_prim_type_info>&;

// The user-addable attachment kinds, in menu order. Built once (static).
[[nodiscard]] auto get_attachment_types() -> const std::vector<Attachment_type_info>&;

// Finds a child prim catalog entry by key, or nullptr when the key is unknown.
[[nodiscard]] auto find_child_prim_type(std::string_view key) -> const Child_prim_type_info*;

// Finds an attachment catalog entry by key, or nullptr when the key is unknown.
[[nodiscard]] auto find_attachment_type(std::string_view key) -> const Attachment_type_info*;

} // namespace editor
