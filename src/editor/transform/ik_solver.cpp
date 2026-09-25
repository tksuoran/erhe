#include "transform/ik_solver.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

namespace editor {

using namespace glm;

namespace {

constexpr float c_epsilon = 1.0e-6f;

// Swing-twist decomposition of q about the local coordinate axis
// `twist_axis`: q == swing * twist, twist a rotation about that axis, the
// swing axis orthogonal to it. Canonicalized so swing.w >= 0. The only
// singularity is 180-degree swing (q.w ~ 0 with no twist component); there
// the twist is identity and the swing is q itself, deterministic.
void swing_twist_decompose(const quat& q, const int twist_axis, quat& swing, quat& twist)
{
    const vec3  r{q.x, q.y, q.z};
    vec3        axis{0.0f};
    axis[twist_axis] = 1.0f;
    const float proj     = dot(r, axis);
    const float twist_w  = q.w;
    const float len2     = (twist_w * twist_w) + (proj * proj);
    if (len2 < c_epsilon * c_epsilon) {
        twist = quat{1.0f, 0.0f, 0.0f, 0.0f};
        swing = q;
    } else {
        const float inv_len = 1.0f / std::sqrt(len2);
        twist = quat{twist_w * inv_len, proj * inv_len * axis.x, proj * inv_len * axis.y, proj * inv_len * axis.z};
        swing = q * inverse(twist);
    }
    if (swing.w < 0.0f) {
        swing = quat{-swing.w, -swing.x, -swing.y, -swing.z}; // same rotation, canonical hemisphere
    }
}

[[nodiscard]] auto swing_component(const quat& swing, const int axis) -> float
{
    return (axis == 0) ? swing.x : (axis == 1) ? swing.y : swing.z;
}

// Interval of sin(half-angle) values the joint admits about one local axis:
// the authored limit (or the whole range when the axis is not limited),
// extended to contain the drag-start component (the no-teleport rule of
// doc/plans/rigging/ik_settings.md section 4).
class Admitted_interval
{
public:
    float lo;
    float hi;
};

[[nodiscard]] auto admitted_interval(const Ik_joint_constraint& constraint, const int axis, const float start_component) -> Admitted_interval
{
    const float lo = constraint.limit[axis] ? std::sin(0.5f * constraint.limit_min[axis]) : -1.0f;
    const float hi = constraint.limit[axis] ? std::sin(0.5f * constraint.limit_max[axis]) :  1.0f;
    return Admitted_interval{.lo = std::min(lo, start_component), .hi = std::max(hi, start_component)};
}

// Twist component (sin(half-angle) about the twist axis) of a twist
// quaternion, in the canonical w >= 0 hemisphere.
[[nodiscard]] auto canonical_twist_component(const quat& twist, const int twist_axis) -> float
{
    const float t = (twist_axis == 0) ? twist.x : (twist_axis == 1) ? twist.y : twist.z;
    return (twist.w < 0.0f) ? -t : t;
}

// Clamps the candidate local rotation to the joint's constraint, relative
// to the drag-start local rotation (which defines the no-teleport extension
// of the constraint region). See doc/plans/rigging/ik_settings.md section 4.
[[nodiscard]] auto constrain_local_rotation(
    const Ik_joint_constraint& constraint,
    const quat&                start_local,
    const quat&                candidate_local
) -> quat
{
    if (!constraint.enabled || (constraint.twist_axis < 0)) {
        return candidate_local;
    }

    // Swing axes: the two non-twist coordinate axes, in X < Y < Z order.
    const int swing_axes[2] = {
        (constraint.twist_axis == 0) ? 1 : 0,
        (constraint.twist_axis == 2) ? 1 : 2
    };

    const quat rel   = normalize(inverse(constraint.rest_rotation) * candidate_local);
    const quat rel_0 = normalize(inverse(constraint.rest_rotation) * start_local);

    quat swing{1.0f, 0.0f, 0.0f, 0.0f};
    quat twist{1.0f, 0.0f, 0.0f, 0.0f};
    swing_twist_decompose(rel, constraint.twist_axis, swing, twist);
    quat swing_0{1.0f, 0.0f, 0.0f, 0.0f};
    quat twist_0{1.0f, 0.0f, 0.0f, 0.0f};
    swing_twist_decompose(rel_0, constraint.twist_axis, swing_0, twist_0);

    // Swing clamp space: sin(half-angle) quaternion components along the
    // swing axes (Blender's SphericalRangeParameters space) - a pure
    // rotation of angle theta about swing axis k has component
    // sin(theta / 2), monotone over theta in [-pi, pi], so authored angle
    // limits map through sin(limit / 2).
    float s  [2] = { swing_component(swing,   swing_axes[0]), swing_component(swing,   swing_axes[1]) };
    float s0 [2] = { swing_component(swing_0, swing_axes[0]), swing_component(swing_0, swing_axes[1]) };
    bool  locked [2]{};
    bool  limited[2]{};
    float lo[2];
    float hi[2];
    for (int k = 0; k < 2; ++k) {
        const int axis = swing_axes[k];
        locked [k] = constraint.lock[axis];
        // limited[] tracks the AUTHORED limit independent of the lock: a
        // locked axis clamps to its pinned value regardless, but its
        // authored radii still shape the other axis's ellipse
        // cross-section below. Branches test locked[] first, so lock still
        // wins over limit on the same axis.
        limited[k] = constraint.limit[axis];
        // No-teleport extension (1-D part): the interval always contains
        // the drag-start component.
        const Admitted_interval interval = admitted_interval(constraint, axis, s0[k]);
        lo[k] = interval.lo;
        hi[k] = interval.hi;
    }
    // Twist. Each backward-pass delta is a twist-free shortest arc in world
    // space, but composed onto a bent parent chain it does carry twist about
    // this joint's own twist axis relative to the rest frame, so a lock or a
    // limit on the twist axis has to be enforced here. A lock pins the twist
    // to its drag-start value; a limit clamps sin(half-angle) to the authored
    // interval, extended to contain the drag-start twist (no teleport).
    // Twist turns about the child direction, so clamping it never moves
    // this joint's child - only the frame the descendants are solved in.
    const bool twist_locked  = constraint.lock [constraint.twist_axis];
    const bool twist_limited = constraint.limit[constraint.twist_axis];
    if (twist_locked || twist_limited) {
        const float t0 = canonical_twist_component(twist_0, constraint.twist_axis);
        float       t  = canonical_twist_component(twist,   constraint.twist_axis);
        if (twist_locked) {
            t = t0;
        } else {
            const Admitted_interval interval = admitted_interval(constraint, constraint.twist_axis, t0);
            t = std::clamp(t, interval.lo, interval.hi);
        }
        vec3 twist_vector{0.0f};
        twist_vector[constraint.twist_axis] = t;
        twist = quat{std::sqrt(std::max(0.0f, 1.0f - (t * t))), twist_vector.x, twist_vector.y, twist_vector.z};
    }

    if (locked[0] && locked[1]) {
        s[0] = s0[0];
        s[1] = s0[1];
    } else if (locked[0] || locked[1]) {
        // One swing axis pinned to its drag-start component; the pinned
        // component is never modified by the clamp. The free component is
        // clamped to the constraint region's cross-section at the pinned
        // value.
        const int  pinned_k = locked[0] ? 0 : 1;
        const int  free_k   = locked[0] ? 1 : 0;
        const float pinned  = s0[pinned_k];
        s[pinned_k] = pinned;
        if (limited[free_k]) {
            float free_lo = lo[free_k];
            float free_hi = hi[free_k];
            if (limited[0] && limited[1]) {
                // Both axes carry limits (one of them also locked): the
                // cross-section of the per-quadrant ellipse at the pinned
                // value. Per sign of the free component the quadrant radii
                // are (a = radius along the pinned axis for sign(pinned),
                // b = radius along the free axis for that sign); the
                // half-interval is b * sqrt(1 - (pinned/a)^2), empty halves
                // collapse to 0. The drag-start point is inside the
                // extended region by construction, so the drag-start free
                // component always remains admissible.
                const float a = (pinned >= 0.0f) ? hi[pinned_k] : -lo[pinned_k];
                const float ratio2 = (a > c_epsilon) ? std::min(1.0f, (pinned / a) * (pinned / a)) : 1.0f;
                const float cross  = std::sqrt(std::max(0.0f, 1.0f - ratio2));
                free_hi = std::min(free_hi, (hi[free_k] > 0.0f ? hi[free_k] : 0.0f) * cross);
                free_lo = std::max(free_lo, (lo[free_k] < 0.0f ? lo[free_k] : 0.0f) * cross);
                // Extension: never exclude the drag-start free component.
                free_lo = std::min(free_lo, s0[free_k]);
                free_hi = std::max(free_hi, s0[free_k]);
            }
            s[free_k] = std::clamp(s[free_k], free_lo, free_hi);
        }
    } else if (limited[0] && limited[1]) {
        // Per-quadrant ellipse clamp (Blender's EllipseClamp shape):
        // asymmetric limits give each quadrant its own radii; the point is
        // scaled radially onto the ellipse when outside. The drag-start
        // point's quadrant is uniformly enlarged just enough to contain it
        // (no-teleport, both radii scaled by the same factor).
        float a = (s[0] >= 0.0f) ? hi[0] : -lo[0];
        float b = (s[1] >= 0.0f) ? hi[1] : -lo[1];
        const bool same_quadrant = ((s[0] >= 0.0f) == (s0[0] >= 0.0f)) && ((s[1] >= 0.0f) == (s0[1] >= 0.0f));
        if (same_quadrant) {
            const float a0 = (s0[0] >= 0.0f) ? hi[0] : -lo[0];
            const float b0 = (s0[1] >= 0.0f) ? hi[1] : -lo[1];
            const float u0 = (a0 > c_epsilon) ? (s0[0] / a0) : 0.0f;
            const float v0 = (b0 > c_epsilon) ? (s0[1] / b0) : 0.0f;
            const float norm_0 = std::sqrt((u0 * u0) + (v0 * v0));
            if (norm_0 > 1.0f) {
                a *= norm_0;
                b *= norm_0;
            }
        }
        if ((a < c_epsilon) || (b < c_epsilon)) {
            // Degenerate ellipse: fall back to independent interval clamps.
            s[0] = std::clamp(s[0], lo[0], hi[0]);
            s[1] = std::clamp(s[1], lo[1], hi[1]);
        } else {
            const float u = s[0] / a;
            const float v = s[1] / b;
            const float norm = std::sqrt((u * u) + (v * v));
            if (norm > 1.0f) {
                s[0] /= norm;
                s[1] /= norm;
            }
        }
    } else if (limited[0]) {
        s[0] = std::clamp(s[0], lo[0], hi[0]);
    } else if (limited[1]) {
        s[1] = std::clamp(s[1], lo[1], hi[1]);
    } else if (!twist_locked && !twist_limited) {
        return candidate_local; // nothing constrains this joint
    }

    // Rebuild the clamped swing; w from the unit constraint (canonical
    // w >= 0 hemisphere, consistent with the decomposition).
    vec3 swing_vector{0.0f};
    swing_vector[swing_axes[0]] = s[0];
    swing_vector[swing_axes[1]] = s[1];
    const float w2 = 1.0f - (s[0] * s[0]) - (s[1] * s[1]);
    const float w  = std::sqrt(std::max(0.0f, w2));
    const quat clamped_swing{w, swing_vector.x, swing_vector.y, swing_vector.z};
    return normalize(constraint.rest_rotation * (clamped_swing * twist));
}

// Whether the joint admits only its drag-start local rotation
// (doc/plans/rigging/ik_settings.md section 4, "Rigid joints"): every one of
// its three axes is locked, or limited to an interval - the authored limit
// extended to the drag-start value - narrower than the solver's numerical
// resolution c_epsilon (a range closed on the drag-start value).
[[nodiscard]] auto admits_only_start_rotation(const Ik_joint_constraint& constraint, const quat& start_local) -> bool
{
    if (!constraint.enabled || (constraint.twist_axis < 0)) {
        return false;
    }
    const quat rel_0 = normalize(inverse(constraint.rest_rotation) * start_local);
    quat swing_0{1.0f, 0.0f, 0.0f, 0.0f};
    quat twist_0{1.0f, 0.0f, 0.0f, 0.0f};
    swing_twist_decompose(rel_0, constraint.twist_axis, swing_0, twist_0);
    for (int axis = 0; axis < 3; ++axis) {
        if (constraint.lock[axis]) {
            continue;
        }
        if (!constraint.limit[axis]) {
            return false;
        }
        const float start_component = (axis == constraint.twist_axis)
            ? canonical_twist_component(twist_0, axis)
            : swing_component(swing_0, axis);
        const Admitted_interval interval = admitted_interval(constraint, axis, start_component);
        if ((interval.hi - interval.lo) >= c_epsilon) {
            return false;
        }
    }
    return true;
}

// Derives the links of the constrained passes from the chain's start pose
// (Ik_chain_links). A single-segment link keeps the segment's own direction
// and length, so a chain without rigid joints solves exactly as before; a
// link over rigid joints takes its start-to-end vector from the start local
// rotations of the rigid joints, the rotations they keep throughout the
// solve.
void build_chain_links(const Ik_chain& chain, Ik_chain_links& links)
{
    const std::size_t segment_count = chain.lengths.size();
    links.motion   .assign(segment_count, Ik_joint_motion::turns);
    links.end      .assign(segment_count, 0);
    links.dir_local.assign(segment_count, vec3{0.0f});
    links.length   .assign(segment_count, 0.0f);
    for (std::size_t i = 0; i < segment_count; ++i) {
        if (admits_only_start_rotation(chain.constraints[i], chain.local_rotations[i])) {
            links.motion[i] = Ik_joint_motion::rigid;
        }
    }
    for (std::size_t i = 0; i < segment_count; ++i) {
        std::size_t end = i + 1;
        while ((end < segment_count) && (links.motion[end] == Ik_joint_motion::rigid)) {
            ++end;
        }
        links.end[i] = end;
        if (!links.is_link_start(i) || (end == i + 1)) {
            links.dir_local[i] = chain.child_dir_local[i];
            links.length   [i] = chain.lengths[i];
            continue;
        }
        vec3 link_vector = chain.child_dir_local[i] * chain.lengths[i];
        quat rigid_frame{1.0f, 0.0f, 0.0f, 0.0f};
        for (std::size_t j = i + 1; j < end; ++j) {
            rigid_frame = rigid_frame * chain.local_rotations[j];
            link_vector += rigid_frame * (chain.child_dir_local[j] * chain.lengths[j]);
        }
        links.dir_local[i] = ik_safe_direction(link_vector, chain.child_dir_local[i]);
        links.length   [i] = length(link_vector);
    }
}

} // anonymous namespace

auto Ik_chain_links::is_link_start(const std::size_t joint) const -> bool
{
    return (joint == 0) || (motion[joint] == Ik_joint_motion::turns);
}

auto ik_safe_direction(const vec3 v, const vec3 fallback) -> vec3
{
    const float len = length(v);
    return (len > c_epsilon) ? (v / len) : fallback;
}

auto ik_shortest_arc(const vec3 a_in, const vec3 b_in, const quat& reference_orientation) -> quat
{
    const float len_a = length(a_in);
    const float len_b = length(b_in);
    if ((len_a < c_epsilon) || (len_b < c_epsilon)) {
        return quat{1.0f, 0.0f, 0.0f, 0.0f};
    }
    const vec3 a = a_in / len_a;
    const vec3 b = b_in / len_b;
    const float cos_angle = dot(a, b);
    if (cos_angle < -1.0f + c_epsilon) {
        const mat3 basis = mat3_cast(reference_orientation);
        vec3 axis{basis[0]};
        float best = std::abs(dot(axis, a));
        for (int i = 1; i < 3; ++i) {
            const float alignment = std::abs(dot(vec3{basis[i]}, a));
            if (alignment < best) {
                best = alignment;
                axis = vec3{basis[i]};
            }
        }
        axis = ik_safe_direction(axis - a * dot(axis, a), vec3{0.0f, 1.0f, 0.0f});
        return angleAxis(pi<float>(), axis);
    }
    // Half-angle form: (1 + cos, sin * axis) is the rotation's quaternion
    // scaled by 2 cos(angle / 2), so normalizing it is exact down to parallel
    // directions (where it is identity) - no acos, and no small angle that
    // rounds to identity.
    const vec3 axis_sin = cross(a, b);
    return normalize(quat{1.0f + cos_angle, axis_sin.x, axis_sin.y, axis_sin.z});
}

auto ik_apply_pole(
    std::vector<glm::vec3>& positions,
    const glm::vec3         pole_position,
    const float             pole_angle,
    const float             weight
) -> std::optional<float>
{
    const std::size_t joint_count = positions.size();
    if (joint_count < 3) {
        return {}; // no intermediate joint: the pole has no effect (R10)
    }
    const std::size_t last = joint_count - 1;

    // Relative epsilon, so every guard below is scale independent.
    float total_length = 0.0f;
    for (std::size_t i = 0; i + 1 < joint_count; ++i) {
        total_length += distance(positions[i], positions[i + 1]);
    }
    const float eps = std::max(1.0e-4f * total_length, c_epsilon);

    const vec3  root     = positions[0];
    const vec3  axis_raw = positions[last] - root;
    const float axis_len = length(axis_raw);
    if (axis_len < eps) {
        return {}; // folded onto the root: the swivel line does not exist
    }
    const vec3 a = axis_raw / axis_len;

    // Mean bend direction: the sum of the intermediate joints' perpendicular
    // offsets from the root-to-effector line, equally weighted.
    vec3 bend_raw{0.0f};
    for (std::size_t i = 1; i < last; ++i) {
        const vec3 offset = positions[i] - root;
        bend_raw += offset - (a * dot(offset, a));
    }
    if (length(bend_raw) < eps) {
        return {}; // straight chain (or cancelling offsets): no bend to aim
    }
    const vec3 b = normalize(bend_raw);

    const vec3 pole_offset = pole_position - root;
    const vec3 pole_raw    = pole_offset - (a * dot(pole_offset, a));
    if (length(pole_raw) < eps) {
        return {}; // the pole lies on the axis and names no direction
    }
    const vec3 d = normalize(pole_raw);

    // Right-handed rotation of the wanted direction about the axis, so a
    // positive pole_angle turns the bend counter-clockwise seen from the
    // effector looking back at the root.
    const vec3  d_target = angleAxis(pole_angle, a) * d;
    const float theta    = std::atan2(dot(cross(b, d_target), a), dot(b, d_target));
    const quat  q        = angleAxis(weight * theta, a);
    for (std::size_t i = 1; i < last; ++i) {
        positions[i] = root + (q * (positions[i] - root));
    }
    return theta;
}

void fabrik_solve(
    std::vector<glm::vec3>&   positions,
    const std::vector<float>& segment_lengths,
    const glm::vec3           target,
    const float               tolerance,
    const int                 max_iterations
)
{
    const std::size_t joint_count = positions.size();
    if ((joint_count < 2) || (segment_lengths.size() + 1 != joint_count)) {
        return;
    }
    const vec3 root = positions.front();

    float total_length = 0.0f;
    for (const float len : segment_lengths) {
        total_length += len;
    }

    // Unreachable target: lay the chain out straight toward it - the closest
    // reachable point - in one pass.
    if (distance(target, root) >= total_length) {
        const vec3 direction = ik_safe_direction(
            target - root,
            ik_safe_direction(positions[1] - positions[0], vec3{0.0f, 1.0f, 0.0f})
        );
        for (std::size_t i = 0; i + 1 < joint_count; ++i) {
            positions[i + 1] = positions[i] + direction * segment_lengths[i];
        }
        return;
    }

    for (int iteration = 0; iteration < max_iterations; ++iteration) {
        if (distance(positions.back(), target) <= tolerance) {
            break;
        }
        // Forward-reaching: snap the effector to the target and work toward
        // the root, preserving segment lengths.
        positions[joint_count - 1] = target;
        for (std::size_t i = joint_count - 1; i > 0; --i) {
            const vec3 direction = ik_safe_direction(positions[i - 1] - positions[i], vec3{0.0f, 1.0f, 0.0f});
            positions[i - 1] = positions[i] + direction * segment_lengths[i - 1];
        }
        // Backward-reaching: snap the root back to its fixed position and
        // work toward the tip.
        positions[0] = root;
        for (std::size_t i = 0; i + 1 < joint_count; ++i) {
            const vec3 direction = ik_safe_direction(positions[i + 1] - positions[i], vec3{0.0f, 1.0f, 0.0f});
            positions[i + 1] = positions[i] + direction * segment_lengths[i];
        }
    }
}

auto Ik_joint_constraint::has_stiffness() const -> bool
{
    return (stiffness.x != 0.0f) || (stiffness.y != 0.0f) || (stiffness.z != 0.0f);
}

auto Ik_joint_constraint::needs_constrained_solve() const -> bool
{
    return (enabled && (twist_axis >= 0)) || has_stiffness();
}

auto Ik_chain::has_constraints() const -> bool
{
    for (const Ik_joint_constraint& constraint : constraints) {
        if (constraint.needs_constrained_solve()) {
            return true;
        }
    }
    return false;
}

namespace {

// Whether a constrained backward pass scales each joint's change by the
// joint's stiffness: the solve's iterations do, the pole admissibility test
// does not (stiffness is a solve-quality bias, never a constraint).
enum class Stiffness_mode : unsigned int
{
    apply,
    ignore
};

// Scales the change from previous_local to candidate_local - the rotation
// vector of inverse(previous_local) * candidate_local, in the joint's own
// frame - per axis by (1 - stiffness) and returns previous_local turned by
// the scaled change (doc/plans/rigging/ik_settings.md section 4).
[[nodiscard]] auto apply_stiffness(const vec3 stiffness, const quat& previous_local, const quat& candidate_local) -> quat
{
    quat change = normalize(inverse(previous_local) * candidate_local);
    if (change.w < 0.0f) {
        change = quat{-change.w, -change.x, -change.y, -change.z}; // shorter way round
    }
    const vec3  change_sin{change.x, change.y, change.z};
    const float sin_half = length(change_sin);
    if (sin_half < c_epsilon) {
        return candidate_local; // no change to scale
    }
    const float angle           = 2.0f * std::atan2(sin_half, change.w);
    const vec3  rotation_vector = (change_sin / sin_half) * angle;
    const vec3  scaled_vector   = rotation_vector * (vec3{1.0f} - stiffness);
    const float scaled_angle    = length(scaled_vector);
    if (scaled_angle < c_epsilon) {
        return previous_local;
    }
    return normalize(previous_local * angleAxis(scaled_angle, scaled_vector / scaled_angle));
}

// The constraint-enforcing backward pass of the constrained solve
// (doc/plans/rigging/ik_settings.md section 4): from the fixed root toward
// the tip, each link start joint is turned by the shortest arc from its
// link's current direction (the parent's solved frame times the joint's entry
// in locals, applied to the link's local direction) toward the desired
// position of the link's far end - for a single-segment link its child; with
// Stiffness_mode::apply, the change from the joint's entry in locals is
// scaled by the joint's stiffness. A rigid joint keeps its drag-start local
// rotation. Each result is clamped to the joint's constraint, and the child is
// placed one segment length along the clamped direction. positions is read
// as the desired pose and overwritten with the solved one; locals holds the
// frames the pass starts from and receives the solved local rotations, so
// positions and locals leave it consistent and satisfying every constraint.
void constrained_backward_pass(
    const Ik_chain&       chain,
    const Ik_chain_links& links,
    const vec3            root,
    std::vector<vec3>&    positions,
    std::vector<quat>&    locals,
    const Stiffness_mode  stiffness_mode
)
{
    const std::size_t joint_count = positions.size();
    positions[0] = root;
    quat parent_world = chain.root_parent_world_rotation;
    for (std::size_t i = 0; i + 1 < joint_count; ++i) {
        const Ik_joint_constraint& constraint = chain.constraints[i];
        quat candidate_local = chain.local_rotations[i];
        if (links.motion[i] == Ik_joint_motion::turns) {
            const quat world_rotation   = parent_world * locals[i];
            const vec3 current_link_dir = world_rotation * links.dir_local[i];
            const vec3 desired_link_dir = ik_safe_direction(positions[links.end[i]] - positions[i], current_link_dir);
            const quat delta            = ik_shortest_arc(current_link_dir, desired_link_dir, world_rotation);
            candidate_local = normalize(inverse(parent_world) * (delta * world_rotation));
            if ((stiffness_mode == Stiffness_mode::apply) && constraint.has_stiffness()) {
                candidate_local = apply_stiffness(constraint.stiffness, locals[i], candidate_local);
            }
        }
        candidate_local = constrain_local_rotation(constraint, chain.local_rotations[i], candidate_local);
        locals[i] = candidate_local;
        const quat solved_world = parent_world * candidate_local;
        positions[i + 1] =
            positions[i] +
            ik_safe_direction(solved_world * chain.child_dir_local[i], vec3{0.0f, 1.0f, 0.0f}) * chain.lengths[i];
        parent_world = solved_world;
    }
}

// Search of the admissible pole fraction: the weighted swivel is walked from
// the solved pose in steps of at most c_pole_march_step radians, so a
// constraint that the swivel leaves and re-enters is noticed, and the last
// admissible step and the first inadmissible one are then bisected
// c_pole_bisection_steps times.
constexpr float c_pole_march_step      = 0.035f; // about 2 degrees
constexpr int   c_pole_bisection_steps = 12;

} // anonymous namespace

void Fabrik_solver::solve(Ik_chain& chain)
{
    const std::size_t joint_count = chain.positions.size();
    if ((joint_count < 2) || (chain.lengths.size() + 1 != joint_count)) {
        return;
    }
    if (!chain.has_constraints()) {
        // Bit-for-bit the Phase 1 path: positions only, local_rotations
        // untouched (the caller keeps its positional write-back).
        fabrik_solve(chain.positions, chain.lengths, chain.target, chain.tolerance, chain.max_iterations);
        // One pole application on the final positions is exact: a rigid
        // rotation about the root-to-effector line maps a converged solution
        // to another converged solution (R13).
        if (chain.has_pole) {
            ik_apply_pole(chain.positions, chain.pole_position, chain.pole_angle, chain.pole_weight);
        }
        return;
    }

    // Constrained FABRIK (doc/plans/rigging/ik_settings.md section 4): forward pass
    // unconstrained; the backward pass enforces constraints with parent
    // world orientations propagated root to tip, so every iteration ends in
    // a constraint-satisfying pose. The unreachable-target shortcut is
    // skipped - a straight layout could violate limits. The iteration runs
    // until the effector is within the tolerance or max_iterations is
    // reached and returns the best pose it saw: FABRIK's error is not
    // monotone under constraints, so an early stop on a rise would end a
    // solve that is still on its way to the target. The cap bounds the cost
    // of an unreachable target - max_iterations backward passes per solve,
    // a few microseconds for a limb-length chain. The pole takes no part in
    // the iteration; it is applied once to the solved pose
    // (apply_constrained_pole).
    const vec3 root = chain.positions.front();
    m_solved_locals.assign(chain.local_rotations.begin(), chain.local_rotations.end());
    build_chain_links(chain, m_links);

    // The start pose is the first best pose: it satisfies the constraints
    // (the no-teleport extension of ik_settings.md section 4 contains it).
    m_best_positions.assign(chain.positions.begin(), chain.positions.end());
    m_best_locals.assign(m_solved_locals.begin(), m_solved_locals.end());
    float best_error = distance(chain.positions.back(), chain.target);

    for (int iteration = 0; iteration < chain.max_iterations; ++iteration) {
        if (best_error <= chain.tolerance) {
            break;
        }

        // Forward-reaching, unconstrained (Phase 1 math) over the links: each
        // link start is placed one link length from its link's far end,
        // which is the Phase 1 pass itself when no joint is rigid. A rigid
        // joint inside a link keeps its position; the backward pass places it.
        chain.positions[joint_count - 1] = chain.target;
        std::size_t link_end = joint_count - 1;
        for (std::size_t i = joint_count - 1; i > 0; --i) {
            const std::size_t start = i - 1;
            if (!m_links.is_link_start(start)) {
                continue;
            }
            const vec3 direction = ik_safe_direction(chain.positions[start] - chain.positions[link_end], vec3{0.0f, 1.0f, 0.0f});
            chain.positions[start] = chain.positions[link_end] + direction * m_links.length[start];
            link_end = start;
        }

        // Backward-reaching with constraint enforcement and root-to-tip
        // frame propagation.
        constrained_backward_pass(chain, m_links, root, chain.positions, m_solved_locals, Stiffness_mode::apply);

        const float error = distance(chain.positions.back(), chain.target);
        if (error < best_error) {
            best_error = error;
            m_best_positions.assign(chain.positions.begin(), chain.positions.end());
            m_best_locals.assign(m_solved_locals.begin(), m_solved_locals.end());
        }
    }
    chain.positions.assign(m_best_positions.begin(), m_best_positions.end());
    m_solved_locals.assign(m_best_locals.begin(), m_best_locals.end());

    if (chain.has_pole) {
        apply_constrained_pole(chain, root);
    }

    for (std::size_t i = 0; i + 1 < joint_count; ++i) {
        chain.local_rotations[i] = m_solved_locals[i];
    }
}

auto Fabrik_solver::try_pole_fraction(const Ik_chain& chain, const vec3 root, const float fraction) -> bool
{
    m_pole_positions.assign(chain.positions.begin(), chain.positions.end());
    ik_apply_pole(m_pole_positions, chain.pole_position, chain.pole_angle, fraction * chain.pole_weight);
    m_pole_solved_positions.assign(m_pole_positions.begin(), m_pole_positions.end());
    m_pole_locals.assign(m_solved_locals.begin(), m_solved_locals.end());
    constrained_backward_pass(chain, m_links, root, m_pole_solved_positions, m_pole_locals, Stiffness_mode::ignore);
    for (std::size_t i = 0; i < m_pole_positions.size(); ++i) {
        if (distance(m_pole_positions[i], m_pole_solved_positions[i]) > chain.tolerance) {
            return false;
        }
    }
    return true;
}

void Fabrik_solver::apply_constrained_pole(Ik_chain& chain, const vec3 root)
{
    // Weight 0 measures the full swivel without moving anything; an
    // undefined swivel (R11's degenerate cases) or a zero weight leaves the
    // solved pose as it is.
    const std::optional<float> full_swivel = ik_apply_pole(chain.positions, chain.pole_position, chain.pole_angle, 0.0f);
    if (!full_swivel.has_value() || (chain.pole_weight <= 0.0f)) {
        return;
    }

    // The solved pose (fraction 0) is admissible by construction: it is the
    // backward pass's own output. Walk toward the full weighted swivel and
    // stop at the first inadmissible pose, so every pose along [0, f] is
    // admissible - a later fraction the constraints happen to allow again
    // is never reached by jumping over the poses between.
    const float weighted_angle = std::abs(chain.pole_weight * full_swivel.value());
    const int   march_steps    = std::max(1, static_cast<int>(std::ceil(weighted_angle / c_pole_march_step)));
    float admissible   = 0.0f;
    float inadmissible = -1.0f;
    for (int step = 1; step <= march_steps; ++step) {
        const float fraction = static_cast<float>(step) / static_cast<float>(march_steps);
        if (!try_pole_fraction(chain, root, fraction)) {
            inadmissible = fraction;
            break;
        }
        admissible = fraction;
    }
    if (inadmissible >= 0.0f) {
        for (int step = 0; step < c_pole_bisection_steps; ++step) {
            const float middle = 0.5f * (admissible + inadmissible);
            if (try_pole_fraction(chain, root, middle)) {
                admissible = middle;
            } else {
                inadmissible = middle;
            }
        }
        if (admissible == 0.0f) {
            return; // the constraints allow no swivel: the solved pose stands
        }
        // Re-run the admissible fraction so the scratch holds its pose; the
        // last probe may have been an inadmissible one.
        static_cast<void>(try_pole_fraction(chain, root, admissible));
    }
    chain.positions.assign(m_pole_solved_positions.begin(), m_pole_solved_positions.end());
    m_solved_locals.assign(m_pole_locals.begin(), m_pole_locals.end());
}

namespace {

// Three-axis cross of half-length arm at position, in the world axes.
void add_ik_drag_marker(
    std::vector<Ik_drag_line>& lines,
    const vec3                 position,
    const float                arm,
    const vec4&                color
)
{
    if (!(arm > 0.0f)) {
        return;
    }
    lines.push_back(Ik_drag_line{.p0 = position - vec3{arm, 0.0f, 0.0f}, .p1 = position + vec3{arm, 0.0f, 0.0f}, .color = color});
    lines.push_back(Ik_drag_line{.p0 = position - vec3{0.0f, arm, 0.0f}, .p1 = position + vec3{0.0f, arm, 0.0f}, .color = color});
    lines.push_back(Ik_drag_line{.p0 = position - vec3{0.0f, 0.0f, arm}, .p1 = position + vec3{0.0f, 0.0f, arm}, .color = color});
}

} // anonymous namespace

void build_ik_drag_lines(const Ik_drag_line_input& input, Ik_drag_line_buffer& buffer)
{
    buffer.clear(); // capacity kept: this runs every frame of a drag

    const std::span<const glm::vec3>& positions  = input.joint_positions;
    const std::size_t                 joint_count = positions.size();
    if (joint_count < 2) {
        return;
    }

    float reach = 0.0f;
    for (std::size_t i = 1; i < joint_count; ++i) {
        reach += distance(positions[i - 1], positions[i]);
        buffer.path_lines.push_back(Ik_drag_line{.p0 = positions[i - 1], .p1 = positions[i], .color = input.chain_color});
    }
    const std::span<const glm::vec3>& lower_positions = input.lower_joint_positions;
    const std::size_t                 lower_count     = lower_positions.size();
    for (std::size_t i = 1; i < lower_count; ++i) {
        reach += distance(lower_positions[i - 1], lower_positions[i]);
        buffer.path_lines.push_back(Ik_drag_line{.p0 = lower_positions[i - 1], .p1 = lower_positions[i], .color = input.chain_color});
    }

    const float arm = reach * input.marker_scale;
    add_ik_drag_marker(buffer.marker_lines, positions[0],               arm, input.root_color);
    add_ik_drag_marker(buffer.marker_lines, positions[joint_count - 1], arm, input.chain_color);
    if (lower_count >= 2) {
        // The pinned end joint is held fixed the way the root is (R24).
        add_ik_drag_marker(buffer.marker_lines, lower_positions[lower_count - 1], arm, input.root_color);
    }

    if (input.pole_position.has_value()) {
        const vec3 pole_position = input.pole_position.value();
        buffer.path_lines.push_back(Ik_drag_line{.p0 = pole_position, .p1 = positions[0], .color = input.pole_color});
        add_ik_drag_marker(buffer.marker_lines, pole_position, arm, input.pole_color);
    }
}

} // namespace editor
