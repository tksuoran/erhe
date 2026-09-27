#pragma once

#include "erhe_physics/iconstraint.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <optional>
#include <string>

namespace erhe::physics {

// Compiled into every build (not only the Box3D backend), so an editor built
// with any backend can tell whether a joint's settings are portable to
// Box3D (describe_box3d_incompatibility below).
//
// Box3D has no generic six degree of freedom joint. The erhe six-DOF settings
// (filled from KHR_physics_rigid_bodies joint descriptions) are therefore
// classified into the closest Box3D joint type. Patterns that are not exactly
// representable are reported through Six_dof_classification::is_exact so the
// caller can log which joint lost fidelity and how.

enum class Axis_state : int {
    fixed,   // limited && min == max
    limited, // limited && min <  max
    free     // !limited
};

enum class Six_dof_joint_kind : int {
    weld,      // no degrees of freedom
    revolute,  // one rotational degree of freedom
    prismatic, // one translational degree of freedom
    distance,  // rotation free, the frame origins kept within a distance range
    spherical, // three rotational degrees of freedom (cone + twist)
    filter     // six degrees of freedom: nothing to constrain
};

[[nodiscard]] auto c_str(Axis_state axis_state) -> const char*;
[[nodiscard]] auto c_str(Six_dof_joint_kind kind) -> const char*;

class Six_dof_classification
{
public:
    Six_dof_joint_kind        kind {Six_dof_joint_kind::weld};
    int                       axis {-1};   // 0..2: revolute / prismatic: the single non-fixed axis; spherical: the twist axis
    bool                      is_exact{true};
    std::array<Axis_state, 6> axis_states{}; // 0..2 translation XYZ, 3..5 rotation XYZ
    // distance: the length range of the Box3D distance joint. The three
    // translation ranges are read as one radial range: the inscribed sphere,
    // max = the smallest of max(|min_k|, |max_k|) over the limited axes, and
    // min = the largest non-negative min_k. Exact when the three ranges are
    // one range with min >= 0, which is how a KHR_physics_rigid_bodies 3D
    // linear limit imports.
    float                     min_distance{0.0f};
    float                     max_distance{0.0f};
};

[[nodiscard]] auto classify_axis(const Constraint_axis_limit& limit) -> Axis_state;

// Pure classification: depends only on the per-axis limits. The caller folds
// fixed non-zero values into frame A first (fold_fixed_axis_values); a fixed
// axis is a fixed axis here whatever its value.
//
// The spherical twist axis (`axis`) is the rotation axis Box3D's twist limit
// takes, the cone being about it: the one fixed rotation axis of a universal
// joint (its twist limit is then [0, 0]), the one limited axis among two free
// ones, the one free axis among two limited ones, the odd one out of three
// limited ranges, and Z otherwise.
[[nodiscard]] auto classify_six_dof(const std::array<Constraint_axis_limit, 6>& limits) -> Six_dof_classification;

// Compact "TTT RRR" rendering of the axis states for log messages, using
// F (fixed), L (limited) and - (free), e.g. "FFF F-F".
[[nodiscard]] auto describe_axis_states(const std::array<Axis_state, 6>& axis_states) -> std::string;

// Box3D joint frames do not use the same reference axis for every joint type:
// a revolute joint rotates about its local frame Z and a prismatic joint slides
// along its local frame X (box3d/types.h). erhe names the axis by index, so the
// joint frames must be rotated to carry the selected erhe axis onto the axis
// Box3D expects. Post-multiply both local frame rotations by this quaternion.
[[nodiscard]] auto revolute_frame_rotation (int axis) -> glm::quat; // carries axis onto +Z
[[nodiscard]] auto prismatic_frame_rotation(int axis) -> glm::quat; // carries axis onto +X

// Box3D springs are specified as a frequency and damping ratio, while erhe (and
// Jolt) carry a stiffness. Converting needs a mass: the effective mass of the
// degree of freedom the spring acts on (the reduced mass along a translation
// axis, the effective inertia about a rotation axis), and
// hertz = sqrt(stiffness / effective_mass) / (2 * pi).
// effective_mass <= 0 (two static bodies, or an unknown mass) yields 0, which
// Box3D reads as a rigid (maximum stiffness) constraint.
[[nodiscard]] auto stiffness_to_hertz(float stiffness, float effective_mass) -> float;

// The damping ratio of a damping coefficient (N s/m or N m s/rad) for a spring
// of that stiffness acting on that effective mass: c / (2 sqrt(k m)). Zero
// when the spring has no stiffness or mass.
[[nodiscard]] auto damping_to_ratio(float damping, float stiffness, float effective_mass) -> float;

// 1 / (1/mass_a + 1/mass_b), treating a non-positive mass as infinite (static).
[[nodiscard]] auto reduced_mass(float mass_a, float mass_b) -> float;

// A drive as a Box3D spring: frequency and damping ratio. An acceleration mode
// drive is that parametrization already (hertz = sqrt(stiffness) / (2 pi),
// ratio = damping / (2 sqrt(stiffness))); a force mode drive is converted with
// the effective mass.
class Box3d_spring
{
public:
    float hertz        {0.0f};
    float damping_ratio{0.0f};
};

[[nodiscard]] auto drive_to_box3d_spring(const Constraint_axis_drive& drive, float effective_mass) -> Box3d_spring;

// Why Box3D would simulate these six-DOF limits differently from the erhe
// joint contract (joint_limits.hpp), or nullopt when it simulates them
// exactly: the axis pattern has no exact Box3D joint (it becomes the closest
// weld / revolute / prismatic / distance / spherical / filter joint, a
// limited ball joint becoming one cone plus a twist), or a fixed rotation at
// a non-zero value that cannot be folded into frame A. For the user
// interface; builds a message.
[[nodiscard]] auto describe_box3d_incompatibility(const std::array<Constraint_axis_limit, 6>& limits) -> std::optional<std::string>;

} // namespace erhe::physics
