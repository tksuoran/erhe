#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <vector>

namespace editor {

// Pure IK solver interface and data (no scene dependencies, unit-testable;
// doc/ik-settings-requirements.md sections 3-4). Ik_drag owns chain discovery and
// node write-back; the solver poses an Ik_chain.

// FABRIK solve (Aristidou & Lasenby 2011) on a chain of world-space joint
// positions. positions[0] is the fixed root; segment_lengths[i] is the
// distance from joint i to joint i+1 (positions.size() == lengths + 1).
// Positions are updated in place toward placing positions.back() at target
// without changing segment lengths. When the target is farther from the root
// than the total chain length, the chain is laid out straight toward the
// target in one pass (closest reachable point). Zero-length segments and
// degenerate directions are epsilon-guarded (never NaN). This is the
// unchanged Phase 1 unconstrained path.
void fabrik_solve(
    std::vector<glm::vec3>&   positions,
    const std::vector<float>& segment_lengths,
    glm::vec3                 target,
    float                     tolerance,
    int                       max_iterations
);

[[nodiscard]] auto ik_safe_direction(glm::vec3 v, glm::vec3 fallback) -> glm::vec3;

// Minimal rotation taking direction a to direction b (both non-unit, world
// space). Identity when either is degenerate. In the antiparallel case the
// shortest-arc axis is undefined; the axis of reference_orientation's basis
// most orthogonal to a (projected into a's orthogonal plane) makes the 180
// degree flip deterministic (roll preservation is forfeited there - see
// doc/fabrik-ik-requirements.md).
[[nodiscard]] auto ik_shortest_arc(glm::vec3 a, glm::vec3 b, const glm::quat& reference_orientation) -> glm::quat;

// Per-joint constraint, resolved at drag start: Ik_settings attachment
// fields OR-ed with the node's lock_rotation_* channel-lock flags, plus the
// derived twist axis. Enforcement is swing/twist relative to rest_rotation
// (doc section 4): the twist component is never generated nor clamped by the
// solver; swing is clamped in sin(half-angle) quaternion-component space
// (Blender's SphericalRangeParameters / EllipseClamp space) with the
// constraint region extended to include the drag-start state (no-teleport).
class Ik_joint_constraint
{
public:
    bool                enabled{false}; // any lock or limit present and twist axis defined
    std::array<bool, 3> lock {false, false, false};
    std::array<bool, 3> limit{false, false, false};
    glm::vec3           limit_min{0.0f}; // radians, [-pi, 0], per local axis
    glm::vec3           limit_max{0.0f}; // radians, [0, pi], per local axis
    glm::quat           rest_rotation{1.0f, 0.0f, 0.0f, 0.0f}; // parent-space zero of the limits
    int                 twist_axis{-1}; // 0/1/2; -1 = undefined (joint unconstrained)
};

// Chain in / posed chain out. Joints in root..effector order. The effector
// entry of local_rotations / constraints is unused (the effector's
// orientation is restored by the caller); child_dir_local has one entry per
// non-effector joint.
class Ik_chain
{
public:
    std::vector<glm::vec3>           positions;       // in: drag-start world; out: solved world
    std::vector<float>               lengths;         // world segment lengths
    std::vector<glm::quat>           local_rotations; // in: drag-start parent-space; out: solved (constrained path)
    std::vector<glm::vec3>           child_dir_local; // unit child offset in each joint's local frame
    std::vector<Ik_joint_constraint> constraints;
    glm::quat                        root_parent_world_rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3                        target{0.0f};
    float                            tolerance{1.0e-4f};
    int                              max_iterations{16};

    [[nodiscard]] auto has_constraints() const -> bool;
};

class Ik_solver
{
public:
    virtual ~Ik_solver() noexcept = default;
    virtual void solve(Ik_chain& chain) = 0;
};

// FABRIK implementation. Without constraints this is exactly the Phase 1
// positional solve (local_rotations untouched - the caller keeps its
// positional write-back). With constraints, each iteration runs the forward
// pass unconstrained and enforces constraints in the backward pass with
// parent frames propagated root to tip; the solved pose is returned in both
// positions and local_rotations, already satisfying the constraints.
class Fabrik_solver final : public Ik_solver
{
public:
    void solve(Ik_chain& chain) override;
};

} // namespace editor
