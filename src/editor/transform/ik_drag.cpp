#include "transform/ik_drag.hpp"

#include "scene/node_ik_settings.hpp"

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

constexpr float c_epsilon         = 1.0e-6f;
constexpr float c_solve_tolerance = 1.0e-4f;
constexpr int   c_max_iterations  = 16;

[[nodiscard]] auto has_ik_lock(const erhe::scene::Node& node) -> bool
{
    return erhe::utility::test_bit_set(node.get_flag_bits(), erhe::Item_flags::ik_lock);
}

// Twist axis (doc/ik-settings-requirements.md section 4): the local coordinate
// axis closest to the joint's child direction in the joint's own frame
// (pose-invariant - the child's local translation does not change with the
// joint's rotation), ties broken in X, Y, Z priority order. -1 when the
// child offset is (near) zero length - such a joint is unconstrained,
// consistent with the zero-length-segment skip rule.
[[nodiscard]] auto derive_twist_axis(const vec3 child_offset_local) -> int
{
    if (length(child_offset_local) < c_epsilon) {
        return -1;
    }
    int   axis = 0;
    float best = std::abs(child_offset_local.x);
    if (std::abs(child_offset_local.y) > best) {
        axis = 1;
        best = std::abs(child_offset_local.y);
    }
    if (std::abs(child_offset_local.z) > best) {
        axis = 2;
    }
    return axis;
}

// Per-joint constraint from the Ik_settings attachment OR-ed with the
// node's lock_rotation_* channel-lock flags. When only channel locks are
// present (no attachment), the drag-start local rotation serves as the
// rest orientation (doc section 2 frame note).
[[nodiscard]] auto resolve_constraint(
    const erhe::scene::Node& joint,
    const quat&              local_rotation_before,
    const int                twist_axis
) -> Ik_joint_constraint
{
    Ik_joint_constraint constraint;
    constraint.twist_axis    = twist_axis;
    constraint.rest_rotation = local_rotation_before;

    const uint64_t flags = joint.get_flag_bits();
    constraint.lock[0] = erhe::utility::test_bit_set(flags, erhe::Item_flags::lock_rotation_x);
    constraint.lock[1] = erhe::utility::test_bit_set(flags, erhe::Item_flags::lock_rotation_y);
    constraint.lock[2] = erhe::utility::test_bit_set(flags, erhe::Item_flags::lock_rotation_z);

    const std::shared_ptr<Ik_settings> ik_settings = erhe::scene::get_attachment<Ik_settings>(&joint);
    if (ik_settings) {
        const Ik_settings_data& data = ik_settings->data;
        for (int axis = 0; axis < 3; ++axis) {
            constraint.lock [axis] = constraint.lock[axis] || data.lock[axis];
            constraint.limit[axis] = data.limit[axis];
        }
        constraint.limit_min     = data.limit_min;
        constraint.limit_max     = data.limit_max;
        constraint.rest_rotation = data.rest_rotation;
    }

    // A constraint on only the twist axis is a solve no-op (the solver
    // never generates twist), so it must not route the chain into the
    // constrained solver - that would silently change unconstrained
    // behavior (e.g. lose the Phase 1 unreachable-target straight layout).
    bool any_swing_constraint = false;
    for (int axis = 0; axis < 3; ++axis) {
        if (axis == twist_axis) {
            continue;
        }
        if (constraint.lock[axis] || constraint.limit[axis]) {
            any_swing_constraint = true;
        }
    }
    constraint.enabled = (twist_axis >= 0) && any_swing_constraint;
    return constraint;
}

} // anonymous namespace

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
    m_local_rotations_before.reserve(m_joints.size());
    for (const std::shared_ptr<erhe::scene::Node>& joint : m_joints) {
        m_parent_from_joint_before.push_back(joint->parent_from_node_transform());
        m_initial_positions.push_back(vec3{joint->position_in_world()});
        m_local_rotations_before.push_back(m_parent_from_joint_before.back().get_rotation());
    }
    m_lengths.reserve(m_joints.size() - 1);
    m_child_dir_local.reserve(m_joints.size() - 1);
    m_constraints.reserve(m_joints.size());
    for (std::size_t i = 0; i + 1 < m_joints.size(); ++i) {
        m_lengths.push_back(distance(m_initial_positions[i], m_initial_positions[i + 1]));
        const vec3 child_offset_local = m_parent_from_joint_before[i + 1].get_translation();
        m_child_dir_local.push_back(
            ik_safe_direction(child_offset_local, vec3{0.0f, 1.0f, 0.0f})
        );
        m_constraints.push_back(
            resolve_constraint(*m_joints[i], m_local_rotations_before[i], derive_twist_axis(child_offset_local))
        );
    }
    m_constraints.push_back(Ik_joint_constraint{}); // effector entry, unused

    const std::shared_ptr<erhe::scene::Node> root_parent = m_joints.front()->get_parent_node();
    m_root_parent_world_rotation = root_parent
        ? root_parent->world_from_node_transform().get_rotation()
        : quat{1.0f, 0.0f, 0.0f, 0.0f};

    m_has_constraints = false;
    for (const Ik_joint_constraint& constraint : m_constraints) {
        if (constraint.enabled && (constraint.twist_axis >= 0)) {
            m_has_constraints = true;
            break;
        }
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

    if (m_has_constraints) {
        // Constrained path: the solver returns constraint-satisfying local
        // rotations; write them back directly (translations and scales stay
        // at drag-start values - bone lengths never change).
        m_chain.positions       = m_initial_positions;
        m_chain.lengths         = m_lengths;
        m_chain.local_rotations = m_local_rotations_before;
        m_chain.child_dir_local = m_child_dir_local;
        m_chain.constraints     = m_constraints;
        m_chain.root_parent_world_rotation = m_root_parent_world_rotation;
        m_chain.target          = target_position_in_world;
        m_chain.tolerance       = c_solve_tolerance;
        m_chain.max_iterations  = c_max_iterations;
        m_solver.solve(m_chain);

        // Cache refreshes are explicit (see the unconstrained path below):
        // root-to-tip order keeps each parent's world transform fresh
        // before the child reads it.
        for (std::size_t i = 0; i + 1 < m_joints.size(); ++i) {
            erhe::scene::Trs_transform parent_from_joint = m_parent_from_joint_before[i];
            parent_from_joint.set_rotation(m_chain.local_rotations[i]);
            m_joints[i]->set_parent_from_node(parent_from_joint);
            m_joints[i]->update_world_from_node();
        }
    } else {
        // Unconstrained: bit-for-bit the Phase 1 path.
        m_scratch_positions = m_initial_positions;
        fabrik_solve(m_scratch_positions, m_lengths, target_position_in_world, c_solve_tolerance, c_max_iterations);

        // Rotation-only write-back, sequentially root to effector: each
        // joint's child direction is re-read under the already-updated
        // ancestors before computing that joint's world-space shortest-arc
        // delta (computing all deltas against the pre-solve pose
        // simultaneously would be wrong).
        //
        // Cache refreshes are explicit: a transform setter updates only the
        // SET node's cached world transform - descendants wait for the
        // scene's next update_node_transforms() pass
        // (Node::handle_transform_update). Without the
        // update_world_from_node() calls below, each joint's world read
        // here would be its pre-solve state, and set_world_from_node would
        // bake that stale translation back in - pinning every joint at its
        // old position (rotating but never translating).
        for (std::size_t i = 0; i + 1 < m_joints.size(); ++i) {
            erhe::scene::Node& joint = *m_joints[i];
            erhe::scene::Node& child = *m_joints[i + 1];
            joint.update_world_from_node(); // ancestors (i-1 and up) are final
            child.update_world_from_node(); // reflect ancestors up to and including joint's current (pre-delta) state
            const vec3 joint_position = vec3{joint.position_in_world()};
            const vec3 child_position = vec3{child.position_in_world()};
            const erhe::scene::Trs_transform& world_from_joint = joint.world_from_node_transform();
            const quat rotation_delta = ik_shortest_arc(
                child_position - joint_position,
                m_scratch_positions[i + 1] - joint_position,
                world_from_joint.get_rotation()
            );
            joint.set_world_from_node(erhe::scene::rotate(world_from_joint, rotation_delta));
        }
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
    m_local_rotations_before.clear();
    m_child_dir_local.clear();
    m_constraints.clear();
    m_root_parent_world_rotation = quat{1.0f, 0.0f, 0.0f, 0.0f};
    m_has_constraints = false;
    m_scratch_positions.clear();
    m_effector_world_rotation_before = quat{1.0f, 0.0f, 0.0f, 0.0f};
}

}
