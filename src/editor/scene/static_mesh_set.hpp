#pragma once

#include <unordered_set>

namespace erhe::scene {
    class Xformable;
    using Node = Xformable;
}

namespace editor {

class Content_library;

// The node tests of the PVS static set (doc/editor/pvs.md D3), shared by the PVS bake and the navigation input.

// The node or an ancestor carries a rigid body whose motion mode is not static: the body moves the node.
[[nodiscard]] auto is_moved_by_physics(const erhe::scene::Node& node) -> bool;

// The target nodes of every animation channel of the library; locks the library's mutex.
void collect_animated_nodes(Content_library& content_library, std::unordered_set<const erhe::scene::Node*>& out_nodes);

// The node or an ancestor is one of animated_nodes.
[[nodiscard]] auto is_animated(const erhe::scene::Node& node, const std::unordered_set<const erhe::scene::Node*>& animated_nodes) -> bool;

}
