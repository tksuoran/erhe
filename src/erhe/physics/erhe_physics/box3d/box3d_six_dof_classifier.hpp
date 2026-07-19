#pragma once

#include "erhe_physics/iconstraint.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <string>

namespace erhe::physics {

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
    spherical, // three rotational degrees of freedom (cone + twist)
    filter     // six degrees of freedom: nothing to constrain
};

[[nodiscard]] auto c_str(Axis_state axis_state) -> const char*;
[[nodiscard]] auto c_str(Six_dof_joint_kind kind) -> const char*;

class Six_dof_classification
{
public:
    Six_dof_joint_kind        kind {Six_dof_joint_kind::weld};
    int                       axis {-1};   // 0..2, the single non-fixed axis for revolute / prismatic
    bool                      is_exact{true};
    std::array<Axis_state, 6> axis_states{}; // 0..2 translation XYZ, 3..5 rotation XYZ
};

[[nodiscard]] auto classify_axis(const Constraint_axis_limit& limit) -> Axis_state;

// Pure classification: depends only on the per-axis limits.
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
// Jolt) carry a stiffness. Converting needs a mass: for a spring-damper acting
// between two bodies the relevant one is the reduced mass, and
// hertz = sqrt(stiffness / reduced_mass) / (2 * pi).
// reduced_mass <= 0 (two static bodies, or an unknown mass) yields 0, which
// Box3D reads as a rigid (maximum stiffness) constraint.
[[nodiscard]] auto stiffness_to_hertz(float stiffness, float reduced_mass) -> float;

// 1 / (1/mass_a + 1/mass_b), treating a non-positive mass as infinite (static).
[[nodiscard]] auto reduced_mass(float mass_a, float mass_b) -> float;

} // namespace erhe::physics
