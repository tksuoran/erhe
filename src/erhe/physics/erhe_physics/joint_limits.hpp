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

// How the three translation degrees of freedom of a six-DOF joint are limited.
//   box    : each axis of F_a on its own (the contract; Jolt)
//   sphere : the distance |t| between the frame origins is confined to
//            [distance.min, distance.max] (Box3D distance joint); the per-axis
//            translation entries then hold the bounding box of that sphere
enum class Translation_limit_model : unsigned int {
    box    = 0,
    sphere = 1
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
    Translation_limit_model              translation_model{Translation_limit_model::box};
    std::array<Constraint_axis_limit, 3> translation{};         // X, Y, Z of F_a (sphere: its bounding box)
    Constraint_axis_limit                distance   {};         // sphere: [min, max] of |t|; not limited = free
    int                                  twist_axis {0};        // 0..2, axis of F_a
    Constraint_axis_limit                twist      {};         // radians
    Swing_limit_model                    swing_model{Swing_limit_model::pyramid};
    std::array<Constraint_axis_limit, 2> swing      {};         // pyramid: radians about swing_axes(twist_axis)[k]
    Constraint_axis_limit                cone       {};         // cone: max = half-angle (min unused); not limited = free
    bool                                 is_exact   {true};     // false: the enforced shape differs from the authored limits
};

// A fixed axis (min == max) authored at a non-zero value is the same joint as
// frame A moved by that value and the axis fixed at zero: a fixed translation
// k at v puts frame B's origin at v along frame A's axis k, and a fixed
// rotation is a constant relative rotation. Both backends fix an axis at zero
// only, so they fold the values into frame A before building the joint, with
// this function, and report the authored values back in the enforced shape.
//
// Translation folds always. A rotation folds only while every translation
// axis is fixed: a rotated frame A would otherwise measure the remaining
// translation ranges along rotated axes. The fold rotation is the contract's
// own relative rotation for the fixed angles, q_swing(Y, Z) * q_twist(X), so
// any combination of fixed rotations folds exactly.
//
// Rewrites the folded axes to [0, 0] and returns which axes were folded;
// `exact` is false when a non-zero fixed rotation could not be folded.
class Fixed_axis_fold
{
public:
    std::array<bool, 6> folded{};
    bool                exact {true};
};

[[nodiscard]] auto fold_fixed_axis_values(Transform& frame_in_a, std::array<Constraint_axis_limit, 6>& limits) -> Fixed_axis_fold;

// Puts the authored values of the folded fixed axes back into a shape
// computed from the folded limits, so the shape reads in the authored frame A.
void restore_folded_fixed_values(
    Joint_limit_shape&                          shape,
    const std::array<Constraint_axis_limit, 6>& authored_limits,
    const Fixed_axis_fold&                      fold
);

// The force a velocity drive applies per unit of velocity error: its damping,
// times the effective mass of the axis in acceleration mode. Both backends'
// motors are velocity constraints bounded by a force, so they re-derive that
// bound each step as gain * |velocity_target - v| (and the drive's max_force),
// which makes the motor the finite-gain viscous coupling the drive states. A
// drive authored without damping would apply no force by the KHR formula; it
// is read as a hard motor bounded by its max force instead, which is what a
// velocity target with no gain can only mean: infinity.
[[nodiscard]] auto velocity_drive_gain(const Constraint_axis_drive& drive, float effective_mass) -> float;

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
//          at zero; a fixed axis holds its authored value (fold_fixed_axis_values).
//   Box3D: the six-DOF classification picks a weld, revolute (twist about
//          the hinge axis), prismatic, distance (a sphere of the translation
//          ranges, rotation free), spherical (twist about the classified twist
//          axis, one cone of the widest swing range) or filter joint; a fixed
//          axis holds its authored value.
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
    float                distance   {0.0f};          // |translation|
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
    std::array<bool, 3> translation_ok{true, true, true}; // sphere model: all three carry the distance check
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
