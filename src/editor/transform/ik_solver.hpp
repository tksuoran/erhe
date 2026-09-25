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
// always finite. Allocation-free: the constrained solve calls it once per
// probe of its admissible pole fraction.
//
// weight scales the swivel (doc/plans/rigging/ik_drag_options.md R28): the
// rotation about the root-to-effector line is weight times the full angle,
// so 0 leaves the positions untouched and 1 aims the bend at the pole.
// Returns the full swivel angle (radians, right-handed about the
// root-to-effector direction), or nothing when the swivel is undefined.
auto ik_apply_pole(
    std::vector<glm::vec3>& positions,
    glm::vec3               pole_position,
    float                   pole_angle,
    float                   weight
) -> std::optional<float>;

// Minimal rotation taking direction a to direction b (both non-unit, world
// space). Identity when either is degenerate or the two are parallel; every
// other angle, however small, gives its exact rotation (a rotation dropped
// below some angle would stall the constrained solve short of its
// tolerance). In the antiparallel case the
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
//
// stiffness is the joint's Ik.stiffness per local axis, [0, 0.99]: in each
// constrained iteration's backward pass the joint's change of local rotation
// since the previous iteration, as a rotation vector in the joint's own
// frame, is scaled per axis by (1 - stiffness) before the clamp
// (doc/plans/rigging/ik_settings.md section 4). It biases the solve toward
// the free joints and is never a constraint.
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
    glm::vec3           stiffness{0.0f}; // per local axis, [0, 0.99]; 0 = free

    [[nodiscard]] auto has_stiffness() const -> bool;
    // A lock or limit to enforce, or a nonzero stiffness: either routes the
    // chain into the constrained solver.
    [[nodiscard]] auto needs_constrained_solve() const -> bool;
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
    // Fraction of the full swivel onto the pole, [0, 1]
    // (doc/plans/rigging/ik_drag_options.md R28); 1 is the snap of
    // pole_target.md.
    float                            pole_weight{1.0f};
    float                            tolerance{1.0e-4f};
    int                              max_iterations{16};

    [[nodiscard]] auto has_constraints() const -> bool;
};

// How a joint moves in the constrained solve (doc/plans/rigging/ik_settings.md
// section 4, "Rigid joints"): a rigid joint admits only its drag-start local
// rotation.
enum class Ik_joint_motion : unsigned int
{
    turns,
    rigid
};

// The links the constrained FABRIK passes see, derived per solve from the
// chain's start pose. A link starts at the root and at every non-rigid joint
// and spans the rigid joints after it up to the next link start or the
// effector: the rigid joints carry their segments with the link's first
// joint as one rigid body. Entries are per non-effector joint; the link
// entries of a rigid joint inside a link are unused.
class Ik_chain_links
{
public:
    std::vector<Ik_joint_motion> motion;         // per non-effector joint
    std::vector<std::size_t>     end;            // joint index at the link's far end
    std::vector<glm::vec3>       dir_local;      // unit start-to-end vector in the link start joint's local frame
    std::vector<float>           length;         // start-to-end distance

    [[nodiscard]] auto is_link_start(std::size_t joint) const -> bool;
};

class Ik_solver
{
public:
    virtual ~Ik_solver() noexcept = default;
    virtual void solve(Ik_chain& chain) = 0;
};

// FABRIK implementation. Without constraints this is exactly the Phase 1
// positional solve (local_rotations untouched - the caller keeps its
// positional write-back). With constraints (a lock, a limit or a nonzero
// stiffness - Ik_joint_constraint::needs_constrained_solve), each iteration
// runs the forward pass unconstrained and, in the backward pass with parent
// frames propagated root to tip, scales each stiff joint's change by its
// stiffness and then enforces its constraints; the solved pose is returned
// in both positions and local_rotations, already satisfying the constraints.
// The constrained iteration returns the best pose it saw
// (doc/plans/rigging/ik_settings.md section 4). A rigid joint (every axis
// admits only its drag-start value) merges its segment into its parent's
// link, so both passes aim the rigid part's far end (Ik_chain_links).
// A pole (Ik_chain::has_pole) is applied once, after the solve, as a rigid
// swivel about the solved root-to-effector line by pole_weight times the full
// angle (doc/plans/rigging/pole_target.md R13, R14; ik_drag_options.md R28).
// The unconstrained path swivels its final positions by that angle. The
// constrained path iterates without the pole and then swivels by the largest
// fraction of that angle whose pose the constraint-enforcing backward pass
// reproduces, and returns that pass's positions and local_rotations: limits
// and locks win over the pole, the effector stays where the solve put it,
// and the result follows the target continuously.
class Fabrik_solver final : public Ik_solver
{
public:
    void solve(Ik_chain& chain) override;

private:
    void apply_constrained_pole(Ik_chain& chain, glm::vec3 root);
    // Swivels the solved pose by fraction times the weighted swivel into
    // m_pole_positions, runs the constraint-enforcing backward pass on it
    // (without stiffness: it tests admissibility, and stiffness is no
    // constraint) into m_pole_solved_positions / m_pole_locals, and returns
    // whether the pass reproduced every joint within chain.tolerance.
    [[nodiscard]] auto try_pole_fraction(const Ik_chain& chain, glm::vec3 root, float fraction) -> bool;

    // Per-solve scratch, refilled in place so a steady-state drag allocates
    // nothing once the chain length's high-water mark is reached.
    std::vector<glm::quat> m_solved_locals;
    std::vector<glm::vec3> m_pole_positions;
    std::vector<glm::vec3> m_pole_solved_positions;
    std::vector<glm::quat> m_pole_locals;
    std::vector<glm::vec3> m_best_positions;
    std::vector<glm::quat> m_best_locals;
    Ik_chain_links         m_links;
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
    // Effector .. end joint of a Pin Chain End drag's lower chain
    // (ik_drag_options.md R24); empty (or a single position) without one.
    std::span<const glm::vec3> lower_joint_positions;
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
// the pole. A lower chain of at least two positions continues the polyline
// from the effector to the end joint and puts a cross in the root color at
// the end joint, which is held fixed the way the root is. Marker arms are
// marker_scale times the reach of the drawn polyline (the sum of its segment
// lengths), so they scale with the rig rather than with the scene's units. Fewer than two joints, or a chain of zero reach, leaves the
// corresponding lines out; nothing else is drawn and no scene is touched.
void build_ik_drag_lines(const Ik_drag_line_input& input, Ik_drag_line_buffer& buffer);

} // namespace editor
