#pragma once

#include <string_view>
#include <vector>

namespace erhe {
    class Hierarchy;
}

namespace editor {

class Scene_commands;

// One user-creatable typed prim that the Hierarchy context menu "Create"
// lists beside the kinds Scene_commands builds directly, and that the
// create_child_prim MCP tool accepts: Mesh, Camera, Light, Joint. Any prim
// parents any prim (doc/erhe/usd_compatibility_design.md C5), so the parent
// is the Hierarchy it is and every parent takes any number of these.
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

// The user-creatable child prim kinds, in menu order. Built once (static).
[[nodiscard]] auto get_child_prim_types() -> const std::vector<Child_prim_type_info>&;

// Finds a child prim catalog entry by key, or nullptr when the key is unknown.
[[nodiscard]] auto find_child_prim_type(std::string_view key) -> const Child_prim_type_info*;

} // namespace editor
