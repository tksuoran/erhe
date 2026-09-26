#include "transform/ik_constraint.hpp"

#include "scene/ik_properties.hpp"

#include "erhe_item/item.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_utility/bit_helpers.hpp"

#include <cmath>

namespace editor {

using namespace glm;

namespace {

constexpr float c_epsilon = 1.0e-6f;

} // anonymous namespace

auto derive_ik_twist_axis(const vec3 child_offset_local) -> int
{
    if (length(child_offset_local) < c_epsilon) {
        return -1;
    }
    int   axis = 0;
    float best = std::abs(child_offset_local.x);
    if (std::abs(child_offset_local.y) > best) {
        axis = 1;
        best = std::abs(child_offset_local.y);
    }
    if (std::abs(child_offset_local.z) > best) {
        axis = 2;
    }
    return axis;
}

auto resolve_ik_constraint(
    const erhe::scene::Node& joint,
    const quat&              local_rotation_before,
    const int                twist_axis
) -> Ik_joint_constraint
{
    const Ik_settings_data data = read_ik_settings(joint);

    Ik_joint_constraint constraint;
    constraint.twist_axis = twist_axis;

    const uint64_t flags = joint.get_flag_bits();
    constraint.lock[0] = erhe::utility::test_bit_set(flags, erhe::Item_flags::lock_rotation_x);
    constraint.lock[1] = erhe::utility::test_bit_set(flags, erhe::Item_flags::lock_rotation_y);
    constraint.lock[2] = erhe::utility::test_bit_set(flags, erhe::Item_flags::lock_rotation_z);
    bool any_ik_constraint = false;
    for (int axis = 0; axis < 3; ++axis) {
        constraint.lock [axis] = constraint.lock[axis] || data.lock[axis];
        constraint.limit[axis] = data.limit[axis];
        any_ik_constraint = any_ik_constraint || data.lock[axis] || data.limit[axis];
    }
    constraint.limit_min = data.limit_min;
    constraint.limit_max = data.limit_max;

    // The limits frame. A joint with any Ik lock or limit on takes the
    // effective Ik.rest_rotation - a local value, a style, or the per-object
    // default, which is the bind pose and otherwise identity: a fixed
    // zero, so the limits do not drift with the pose. The drag-start local
    // rotation is the rest only for a joint constrained by channel-lock flags
    // alone, which is the section 2 frame note's case.
    constraint.rest_rotation = any_ik_constraint ? data.rest_rotation : local_rotation_before;

    // Any lock or limit routes the chain into the constrained solver, the
    // twist axis included: world-space shortest arcs composed onto a bent
    // parent chain do turn a joint about its own twist axis.
    bool any_constraint = false;
    for (int axis = 0; axis < 3; ++axis) {
        if (constraint.lock[axis] || constraint.limit[axis]) {
            any_constraint = true;
        }
    }
    constraint.enabled = (twist_axis >= 0) && any_constraint;

    // Stiffness scales the joint's per-iteration change in the constrained
    // solve and, when nonzero, routes the chain there as a lock or limit
    // does (ik_settings.md section 4). A joint without a twist axis has a
    // zero-length child offset, which the solve never turns: its stiffness
    // has nothing to act on.
    constraint.stiffness = (twist_axis >= 0) ? data.stiffness : vec3{0.0f};
    return constraint;
}

} // namespace editor
