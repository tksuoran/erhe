#pragma once

#include "transform/ik_solver.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace erhe::scene { class Xformable; using Node = Xformable; }

namespace editor {

// The constraint of one IK joint, resolved from the scene: shared by the IK
// drag (Ik_drag_chain::capture) and the IK limit visualization
// (doc/editor/tools.md "Debug_visualizations"), so what is drawn is what
// the solver enforces.

// Twist axis (doc/plans/rigging/ik_settings.md section 4): the local coordinate
// axis closest to the joint's child direction in the joint's own frame
// (pose-invariant - the child's local translation does not change with the
// joint's rotation), ties broken in X, Y, Z priority order. -1 when the
// child offset is (near) zero length - such a joint is unconstrained,
// consistent with the zero-length-segment skip rule.
[[nodiscard]] auto derive_ik_twist_axis(glm::vec3 child_offset_local) -> int;

// Per-joint constraint from the node's Ik.* values OR-ed with its
// lock_rotation_* channel-lock flags, plus its Ik.stiffness. A joint whose
// locks and limits are all off is unconstrained (it may still be stiff); a
// joint constrained by channel locks alone takes local_rotation_before (the
// drag-start local rotation) as its rest orientation (the rest-frame rule of
// doc/plans/rigging/ik_settings.md section 3).
[[nodiscard]] auto resolve_ik_constraint(
    const erhe::scene::Node& joint,
    const glm::quat&         local_rotation_before,
    int                      twist_axis
) -> Ik_joint_constraint;

} // namespace editor
