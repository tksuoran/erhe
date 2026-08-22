#include "transform/ik_drag.hpp"

#include "erhe_item/item.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_utility/bit_helpers.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace editor {

using namespace glm;

namespace {

constexpr float c_epsilon        = 1.0e-6f;
constexpr float c_solve_tolerance = 1.0e-4f;
constexpr int   c_max_iterations  = 16;

[[nodiscard]] auto safe_direction(const vec3 v, const vec3 fallback) -> vec3
{
    const float len = length(v);
    return (len > c_epsilon) ? (v / len) : fallback;
}

// Minimal rotation taking direction a to direction b (both non-unit, world
// space). Identity when either is degenerate. In the antiparallel case the
// shortest-arc axis is undefined; the axis of reference_orientation's basis
// most orthogonal to a (projected into a's orthogonal plane) makes the 180
// degree flip deterministic (roll preservation is forfeited there - see
// doc/fabrik-ik-requirements.md).
[[nodiscard]] auto shortest_arc(const vec3 a_in, const vec3 b_in, const quat& reference_orientation) -> quat
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
        axis = safe_direction(axis - a * dot(axis, a), vec3{0.0f, 1.0f, 0.0f});
        return angleAxis(pi<float>(), axis);
    }
    const vec3 axis = normalize(cross(a, b));
    return angleAxis(std::acos(std::clamp(cos_angle, -1.0f, 1.0f)), axis);
}

[[nodiscard]] auto has_ik_lock(const erhe::scene::Node& node) -> bool
{
    return erhe::utility::test_bit_set(node.get_flag_bits(), erhe::Item_flags::ik_lock);
}

} // anonymous namespace

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
        const vec3 direction = safe_direction(
            target - root,
            safe_direction(positions[1] - positions[0], vec3{0.0f, 1.0f, 0.0f})
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
            const vec3 direction = safe_direction(positions[i - 1] - positions[i], vec3{0.0f, 1.0f, 0.0f});
            positions[i - 1] = positions[i] + direction * segment_lengths[i - 1];
        }
        // Backward-reaching: snap the root back to its fixed position and
        // work toward the tip.
        positions[0] = root;
        for (std::size_t i = 0; i + 1 < joint_count; ++i) {
            const vec3 direction = safe_direction(positions[i + 1] - positions[i], vec3{0.0f, 1.0f, 0.0f});
            positions[i + 1] = positions[i] + direction * segment_lengths[i];
        }
    }
}

auto Ik_drag::begin(const std::shared_ptr<erhe::scene::Node>& effector) -> bool
{
    reset();
    if (!effector) {
        return false;
    }
    if (erhe::scene::is_bone(effector.get())) {
        if (has_ik_lock(*effector)) {
            return false;
        }
    } else {
        // Non-bone drag handle (an Add Bone Tip Nodes tip, or any node
        // parented under a bone): it joins the chain as the effector point,
        // so the parent bone rotates to aim at it - which a bone-effector
        // drag never does (the effector keeps its own orientation).
        const std::shared_ptr<erhe::scene::Node> parent = effector->get_parent_node();
        if (!parent || !erhe::scene::is_bone(parent.get())) {
            return false;
        }
    }

    // Collect effector..root, then reverse. The walk stops after collecting
    // an ik_lock ancestor: it joins the chain as the fixed root.
    std::vector<std::shared_ptr<erhe::scene::Node>> joints;
    joints.push_back(effector);
    std::shared_ptr<erhe::scene::Node> current = effector;
    while (true) {
        std::shared_ptr<erhe::scene::Node> parent = current->get_parent_node();
        if (!parent || !erhe::scene::is_bone(parent.get())) {
            break;
        }
        joints.push_back(parent);
        if (has_ik_lock(*parent)) {
            break;
        }
        current = parent;
    }
    if (joints.size() < 2) {
        return false;
    }
    std::reverse(joints.begin(), joints.end());

    m_joints = std::move(joints);
    m_parent_from_joint_before.reserve(m_joints.size());
    m_initial_positions.reserve(m_joints.size());
    for (const std::shared_ptr<erhe::scene::Node>& joint : m_joints) {
        m_parent_from_joint_before.push_back(joint->parent_from_node_transform());
        m_initial_positions.push_back(vec3{joint->position_in_world()});
    }
    m_lengths.reserve(m_joints.size() - 1);
    for (std::size_t i = 0; i + 1 < m_joints.size(); ++i) {
        m_lengths.push_back(distance(m_initial_positions[i], m_initial_positions[i + 1]));
    }
    m_effector_world_rotation_before = m_joints.back()->world_from_node_transform().get_rotation();
    return true;
}

void Ik_drag::apply(const glm::vec3 target_position_in_world)
{
    if (!is_active()) {
        return;
    }

    // Restore the drag-start pose: the target is absolute, so each solve
    // starts from the same pose and dragging back to the start restores it.
    for (std::size_t i = 0; i < m_joints.size(); ++i) {
        m_joints[i]->set_parent_from_node(m_parent_from_joint_before[i]);
    }

    m_scratch_positions = m_initial_positions;
    fabrik_solve(m_scratch_positions, m_lengths, target_position_in_world, c_solve_tolerance, c_max_iterations);

    // Rotation-only write-back, sequentially root to effector: each joint's
    // child direction is re-read under the already-updated ancestors before
    // computing that joint's world-space shortest-arc delta (computing all
    // deltas against the pre-solve pose simultaneously would be wrong).
    //
    // Cache refreshes are explicit: a transform setter updates only the SET
    // node's cached world transform - descendants wait for the scene's next
    // update_node_transforms() pass (Node::handle_transform_update). Without
    // the update_world_from_node() calls below, each joint's world read here
    // would be its pre-solve state, and set_world_from_node would bake that
    // stale translation back in - pinning every joint at its old position
    // (rotating but never translating).
    for (std::size_t i = 0; i + 1 < m_joints.size(); ++i) {
        erhe::scene::Node& joint = *m_joints[i];
        erhe::scene::Node& child = *m_joints[i + 1];
        joint.update_world_from_node(); // ancestors (i-1 and up) are final
        child.update_world_from_node(); // reflect ancestors up to and including joint's current (pre-delta) state
        const vec3 joint_position = vec3{joint.position_in_world()};
        const vec3 child_position = vec3{child.position_in_world()};
        const erhe::scene::Trs_transform& world_from_joint = joint.world_from_node_transform();
        const quat rotation_delta = shortest_arc(
            child_position - joint_position,
            m_scratch_positions[i + 1] - joint_position,
            world_from_joint.get_rotation()
        );
        joint.set_world_from_node(erhe::scene::rotate(world_from_joint, rotation_delta));
    }

    // The effector keeps its drag-start world orientation; only its position
    // follows the chain.
    erhe::scene::Node& effector = *m_joints.back();
    effector.update_world_from_node(); // its parent joint is final
    const quat effector_rotation = effector.world_from_node_transform().get_rotation();
    const quat restore_delta = m_effector_world_rotation_before * inverse(effector_rotation);
    effector.set_world_from_node(erhe::scene::rotate(effector.world_from_node_transform(), restore_delta));
}

void Ik_drag::reset()
{
    m_joints.clear();
    m_parent_from_joint_before.clear();
    m_initial_positions.clear();
    m_lengths.clear();
    m_scratch_positions.clear();
    m_effector_world_rotation_before = quat{1.0f, 0.0f, 0.0f, 0.0f};
}

}
