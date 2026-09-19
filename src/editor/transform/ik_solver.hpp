#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <optional>
#include <span>
#include <vector>

namespace editor {

// Pure IK solver interface and data (no scene dependencies, unit-testable;
// doc/plans/rigging/ik_settings.md sections 3-4). Ik_drag owns chain discovery and
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

// Pole reprojection (doc/plans/rigging/pole_target.md R11) on world-space
// joint positions: rotates the intermediate joints rigidly about the line
// from the root positions[0] to the effector positions[n] until the chain's
// mean bend direction - the sum of the intermediate joints' perpendicular
// offsets from that line - aims at pole_position, turned by pole_angle
// (radians, right-handed about the root-to-effector direction). Neither the
// root nor the effector moves and no segment length changes, so a converged
// solution maps to another converged solution. Returns the positions
// unchanged when the swivel is undefined: fewer than three joints (no
// intermediate joint), a chain folded onto its root, a straight chain (or
// one whose intermediate offsets cancel), or a pole on the root-to-effector
// line. The three guarded lengths are the only divisions, so the result is
// always finite. Allocation-free: it is called per solver iteration.
void ik_apply_pole(std::vector<glm::vec3>& positions, glm::vec3 pole_position, float pole_angle);

// Minimal rotation taking direction a to direction b (both non-unit, world
// space). Identity when either is degenerate. In the antiparallel case the
// shortest-arc axis is undefined; the axis of reference_orientation's basis
// most orthogonal to a (projected into a's orthogonal plane) makes the 180
// degree flip deterministic (roll preservation is forfeited there - see
// doc/plans/rigging/fabrik_ik.md).
[[nodiscard]] auto ik_shortest_arc(glm::vec3 a, glm::vec3 b, const glm::quat& reference_orientation) -> glm::quat;

// Per-joint constraint, resolved at drag start: the joint node's Ik.*
// values OR-ed with its lock_rotation_* channel-lock flags, plus the
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
    // Pole target (doc/plans/rigging/pole_target.md R12). When has_pole is
    // false the solve is the unpoled one. pole_position is world space, the
    // same space as positions and target; pole_angle is in radians.
    bool                             has_pole{false};
    glm::vec3                        pole_position{0.0f};
    float                            pole_angle{0.0f};
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
// A pole (Ik_chain::has_pole) is applied once on the final positions of the
// unconstrained path and inside every iteration of the constrained path,
// between the forward and the backward pass, so the constraint-clamping
// backward pass always runs last and limits and locks win over the pole
// (doc/plans/rigging/pole_target.md R13, R14).
class Fabrik_solver final : public Ik_solver
{
public:
    void solve(Ik_chain& chain) override;
};

// Chain visualization of a running IK drag
// (doc/plans/rigging/ik_drag_options.md section 2). The geometry is built by
// the pure function below, with no scene access, so it is unit testable
// without a display; the drawing owner (Transform_tool::tool_render) hands the
// result to erhe::renderer::Primitive_renderer.

// One world-space coloured line segment.
class Ik_drag_line
{
public:
    glm::vec3 p0{0.0f};
    glm::vec3 p1{0.0f};
    glm::vec4 color{1.0f};
};

// Caller-owned line record, cleared and refilled at the point of use so a
// steady-state drag frame allocates nothing once the high-water mark is
// reached (AGENTS.md "Run-time Memory Allocation Discipline"). The two
// vectors are the two line widths the drawing owner uses: path_lines is drawn
// at Debug_visualizations_style::ik_chain_width and marker_lines at
// ik_marker_width.
class Ik_drag_line_buffer
{
public:
    std::vector<Ik_drag_line> path_lines;   // chain polyline + pole line
    std::vector<Ik_drag_line> marker_lines; // root, effector and pole crosses

    void clear() // keeps capacity
    {
        path_lines.clear();
        marker_lines.clear();
    }

    [[nodiscard]] auto line_count() const -> std::size_t
    {
        return path_lines.size() + marker_lines.size();
    }
};

// Input of build_ik_drag_lines(): everything the visualization needs, in
// world space, taken from the state the drag already holds.
class Ik_drag_line_input
{
public:
    std::span<const glm::vec3> joint_positions;      // root .. effector
    std::optional<glm::vec3>   pole_position;        // unset = unpoled drag
    float                      marker_scale{0.05f};  // marker arm / chain reach
    glm::vec4                  chain_color{0.2f, 0.9f, 1.0f, 1.0f};
    glm::vec4                  root_color {1.0f, 0.4f, 0.1f, 1.0f};
    glm::vec4                  pole_color {1.0f, 0.2f, 0.8f, 1.0f};
};

// Builds the drag visualization's line list into buffer, which is cleared
// first. Produces one line per chain segment through joint_positions, a
// three-axis cross at the root and at the effector, and - when
// pole_position is set - a line from the pole to the root plus a cross at
// the pole. Marker arms are marker_scale times the chain's reach (the sum of
// its segment lengths), so they scale with the rig rather than with the
// scene's units. Fewer than two joints, or a chain of zero reach, leaves the
// corresponding lines out; nothing else is drawn and no scene is touched.
void build_ik_drag_lines(const Ik_drag_line_input& input, Ik_drag_line_buffer& buffer);

} // namespace editor
