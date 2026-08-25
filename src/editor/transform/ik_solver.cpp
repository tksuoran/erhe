#include "transform/ik_solver.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

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

// Clamps the candidate local rotation to the joint's constraint, relative
// to the drag-start local rotation (which defines the no-teleport extension
// of the constraint region). See doc/ik-settings-requirements.md section 4.
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
        lo[k] = limited[k] ? std::sin(0.5f * constraint.limit_min[axis]) : -1.0f;
        hi[k] = limited[k] ? std::sin(0.5f * constraint.limit_max[axis]) :  1.0f;
        // No-teleport extension (1-D part): the interval always contains
        // the drag-start component.
        lo[k] = std::min(lo[k], s0[k]);
        hi[k] = std::max(hi[k], s0[k]);
    }
    // Locks and limits on the twist axis are solve no-ops (the solver never
    // generates twist; pre-existing twist must not be clamped).

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
    } else {
        return candidate_local; // twist-only constraint: no-op
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

} // anonymous namespace

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
    if (cos_angle > 1.0f - c_epsilon) {
        return quat{1.0f, 0.0f, 0.0f, 0.0f};
    }
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
    const vec3 axis = normalize(cross(a, b));
    return angleAxis(std::acos(std::clamp(cos_angle, -1.0f, 1.0f)), axis);
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

auto Ik_chain::has_constraints() const -> bool
{
    for (const Ik_joint_constraint& constraint : constraints) {
        if (constraint.enabled && (constraint.twist_axis >= 0)) {
            return true;
        }
    }
    return false;
}

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
        return;
    }

    // Constrained FABRIK (doc/ik-settings-requirements.md section 4): forward pass
    // unconstrained; the backward pass enforces constraints with parent
    // world orientations propagated root to tip, so every iteration ends in
    // a constraint-satisfying pose. The unreachable-target shortcut is
    // skipped - a straight layout could violate limits; the iteration
    // converges to the constrained best effort and stops when the error
    // stops decreasing.
    const vec3 root = chain.positions.front();
    std::vector<quat> solved_locals = chain.local_rotations;
    float previous_error = std::numeric_limits<float>::max();

    for (int iteration = 0; iteration < chain.max_iterations; ++iteration) {
        const float error = distance(chain.positions.back(), chain.target);
        if (error <= chain.tolerance) {
            break;
        }
        if (error >= previous_error - (0.01f * chain.tolerance)) {
            if (iteration > 0) {
                break; // stalled (constrained target unreachable) - stable best effort
            }
        }
        previous_error = error;

        // Forward-reaching, unconstrained (Phase 1 math).
        chain.positions[joint_count - 1] = chain.target;
        for (std::size_t i = joint_count - 1; i > 0; --i) {
            const vec3 direction = ik_safe_direction(chain.positions[i - 1] - chain.positions[i], vec3{0.0f, 1.0f, 0.0f});
            chain.positions[i - 1] = chain.positions[i] + direction * chain.lengths[i - 1];
        }

        // Backward-reaching with constraint enforcement and root-to-tip
        // frame propagation.
        chain.positions[0] = root;
        quat parent_world = chain.root_parent_world_rotation;
        for (std::size_t i = 0; i + 1 < joint_count; ++i) {
            const quat world_rotation      = parent_world * solved_locals[i];
            const vec3 current_child_dir   = world_rotation * chain.child_dir_local[i];
            const vec3 desired_child_dir   = ik_safe_direction(chain.positions[i + 1] - chain.positions[i], current_child_dir);
            const quat delta               = ik_shortest_arc(current_child_dir, desired_child_dir, world_rotation);
            quat       candidate_local     = normalize(inverse(parent_world) * (delta * world_rotation));
            candidate_local = constrain_local_rotation(chain.constraints[i], chain.local_rotations[i], candidate_local);
            solved_locals[i] = candidate_local;
            const quat solved_world = parent_world * candidate_local;
            chain.positions[i + 1] =
                chain.positions[i] +
                ik_safe_direction(solved_world * chain.child_dir_local[i], vec3{0.0f, 1.0f, 0.0f}) * chain.lengths[i];
            parent_world = solved_world;
        }
    }

    for (std::size_t i = 0; i + 1 < joint_count; ++i) {
        chain.local_rotations[i] = solved_locals[i];
    }
}

} // namespace editor
