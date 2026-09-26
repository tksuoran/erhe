#pragma once

#include "erhe_physics/iconstraint.hpp"
#include "erhe_physics/transform.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>

namespace erhe::physics {

// How the two swing degrees of freedom of a six-DOF joint are limited.
//   pyramid : each swing axis on its own; the swing half-angle about axis k is
//             atan2(q_swing[k], q_swing.w) and is clamped to [min / 2, max / 2]
//             (Jolt ESwingType::Pyramid)
//   cone    : the swing angle 2 * acos(q_swing.w) is clamped to a symmetric
//             half-angle about the twist axis (Box3D spherical joint)
enum class Swing_limit_model : unsigned int {
    pyramid = 0,
    cone    = 1
};

// The limits a backend actually enforces for a set of authored six-DOF limits
// (IConstraint::get_enforced_limits). The joint coordinates are those of
// measure_joint_coordinates(): with anchor frames F_a and F_b in world space,
// the translation t = F_a.basis^T * (F_b.origin - F_a.origin) and the relative
// rotation q = inverse(F_a rotation) * F_b rotation, decomposed as
// q = q_swing * q_twist about the twist axis.
//
// A limited axis whose min == max is fixed at that value; an axis that is not
// limited is free.
class Joint_limit_shape
{
public:
    std::array<Constraint_axis_limit, 3> translation{};         // X, Y, Z of F_a
    int                                  twist_axis {0};        // 0..2, axis of F_a
    Constraint_axis_limit                twist      {};         // radians
    Swing_limit_model                    swing_model{Swing_limit_model::pyramid};
    std::array<Constraint_axis_limit, 2> swing      {};         // pyramid: radians about swing_axes(twist_axis)[k]
    Constraint_axis_limit                cone       {};         // cone: max = half-angle (min unused); not limited = free
    bool                                 is_exact   {true};     // false: the enforced shape differs from the authored limits
};

// THE JOINT CONTRACT. erhe states a joint as six per-axis limits
// (Six_dof_constraint_settings::limits: 0..2 translation, 3..5 rotation) in
// the D6 joint convention (as PhysX D6 and UsdPhysics rotX / rotY / rotZ):
//   - translation k: the offset of frame B from frame A along frame A's axis
//     k, confined to [min, max]; a fixed axis (min == max) holds that value;
//   - rotation X: the twist, the angle of the relative rotation about frame
//     A's X axis;
//   - rotation Y, Z: the swing, each swing angle limited on its own (the
//     pyramid form: half-angle atan2(q_k, q_w) of the swing quaternion);
//   - a free axis is not limited.
// That is what the settings mean whatever backend simulates them.
// get_contract_joint_limits() states it as a Joint_limit_shape, so the
// contract can be measured and drawn without any backend;
// get_enforced_joint_limits() below is what the built backend actually
// simulates, which may differ (is_exact false).
[[nodiscard]] auto get_contract_joint_limits(const std::array<Constraint_axis_limit, 6>& limits) -> Joint_limit_shape;

// The limits the physics backend this library is built with enforces for the
// authored six-DOF limits (0..2 translation XYZ, 3..5 rotation XYZ - the
// layout of Six_dof_constraint_settings::limits). Implemented by each backend
// next to its six-DOF constraint factory, so a joint that is not built yet
// and a live one read the same shape.
//   Jolt : twist about X, pyramid swing about Y and Z; rotation limits
//          clamped to [-pi, pi], a range within 0.5 degrees of zero is locked
//          at zero, a fixed translation axis is fixed at zero.
//   Box3D: the six-DOF classification picks a weld, revolute (twist about
//          the hinge axis), prismatic, spherical (twist about Z, cone of the
//          widest swing half range) or filter joint.
//   none : the contract (get_contract_joint_limits).
[[nodiscard]] auto get_enforced_joint_limits(const std::array<Constraint_axis_limit, 6>& limits) -> Joint_limit_shape;

// The two swing axes of a twist axis, the remaining coordinate axes in
// increasing order.
[[nodiscard]] auto get_swing_axes(int twist_axis) -> std::array<int, 2>;

// Current joint coordinates, in the terms of Joint_limit_shape.
class Joint_coordinates
{
public:
    glm::vec3            translation{0.0f};
    float                twist      {0.0f};          // radians about the twist axis
    std::array<float, 2> swing      {0.0f, 0.0f};    // pyramid swing angles, radians
    float                cone       {0.0f};          // swing angle from the twist axis, radians
};

[[nodiscard]] auto measure_joint_coordinates(
    const Transform&         world_from_frame_a,
    const Transform&         world_from_frame_b,
    const Joint_limit_shape& shape
) -> Joint_coordinates;

// True when value lies inside the limit (a free axis admits everything),
// allowing tolerance on each side.
[[nodiscard]] auto is_within(const Constraint_axis_limit& limit, float value, float tolerance) -> bool;

// Which joint coordinates lie outside the enforced limits.
class Joint_range_check
{
public:
    std::array<bool, 3> translation_ok{true, true, true};
    bool                twist_ok      {true};
    bool                swing_ok      {true}; // both pyramid axes, or the cone

    [[nodiscard]] auto all_ok() const -> bool;
};

[[nodiscard]] auto check_joint_range(
    const Joint_limit_shape& shape,
    const Joint_coordinates& coordinates,
    float                    linear_tolerance,
    float                    angular_tolerance
) -> Joint_range_check;

// The swing rotation of the pyramid swing angles (angle_0, angle_1) about
// swing_axes(twist_axis): the swing Jolt rebuilds from clamped half-angles,
// q = (sin(a0/2) cos(a1/2), cos(a0/2) sin(a1/2), cos(a0/2) cos(a1/2)) placed
// on the swing axes, normalized. A relative rotation q_swing * q_twist.
[[nodiscard]] auto pyramid_swing_rotation(int twist_axis, float angle_0, float angle_1) -> glm::quat;

// Direction of F_b's twist axis, in F_a, for the pyramid swing angles
// (angle_0, angle_1) about swing_axes(twist_axis): the swing Jolt rebuilds
// from clamped half-angles, q = (sin(a0/2) cos(a1/2), cos(a0/2) sin(a1/2),
// cos(a0/2) cos(a1/2)) placed on the swing axes, normalized.
[[nodiscard]] auto pyramid_swing_direction(int twist_axis, float angle_0, float angle_1) -> glm::vec3;

} // namespace erhe::physics
