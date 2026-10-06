#pragma once

namespace erhe::scene {
    class Xformable;
    using Node = Xformable;
}

namespace editor {

class App_message_bus;

// Whether a transform change of the node moves anything the indirect
// diffuse producers trace or shade with: a content-layer mesh or a light in
// the node's subtree. A camera or tool node does not.
[[nodiscard]] auto node_affects_indirect_lighting(const erhe::scene::Node& node) -> bool;

// The one announcement of a COMMITTED node transform: sends
// Node_touched_message (synchronously) and, when the node affects indirect
// lighting, queues Scene_lighting_changed_message. Called by the commit
// sites only - Node_transform_operation execute / undo / redo, the end of
// its transform animation, Flip_joint_operation execute / undo, and
// App_context::on_item_property_changed() for a transform property
// (Property_flags::affects_transform) of a node. A live, uncommitted move
// (a transform animation frame, a navigation gizmo drag or axis snap, the
// viewport window's snap animation) sends Node_touched_message alone; a
// transform tool or IK drag sends nothing until it commits one
// Node_transform_operation at drag end.
void announce_committed_node_transform(App_message_bus& app_message_bus, erhe::scene::Node& node);

} // namespace editor
