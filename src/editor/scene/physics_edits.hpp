#pragma once

#include <memory>

namespace erhe::physics {
    class Collision_filter;
    class Physics_material;
    class Physics_joint_settings;
}

namespace editor {

class App_context;

// Helpers for propagating edits of shared physics content-library items to
// the live physics state. Used by the properties window and the MCP server.

// Re-assigns the edited material to every scene body using it so the physics
// backend re-snapshots the values (the per-body snapshot does not track the
// shared item).
void reapply_physics_material(App_context& context, const std::shared_ptr<erhe::physics::Physics_material>& physics_material);

// Re-assigns the edited filter to every scene body using it so the physics
// backend recompiles the filter snapshot.
void reapply_collision_filter(App_context& context, const std::shared_ptr<erhe::physics::Collision_filter>& collision_filter);

// Rebuilds the constraint of every Node_joint using the edited settings,
// re-capturing the joint frames from the current node transforms.
void rebuild_joints_using_settings(App_context& context, const std::shared_ptr<erhe::physics::Physics_joint_settings>& settings);

}
