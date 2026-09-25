#include "transform/ik_drag.hpp"

#include "editor_log.hpp"
#include "operations/compound_operation.hpp"
#include "operations/node_transform_operation.hpp"
#include "scene/ik_properties.hpp"

#include "erhe_item/hierarchy.hpp"
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

// Twist axis (doc/plans/rigging/ik_settings.md section 4): the local coordinate
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

// Per-joint constraint from the node's Ik.* values OR-ed with its
// lock_rotation_* channel-lock flags. A joint whose locks and limits are
// all off is unconstrained; a joint constrained by channel locks alone takes
// the drag-start local rotation as its rest orientation (the rest-frame rule
// of doc/plans/rigging/ik_settings.md section 3).
[[nodiscard]] auto resolve_constraint(
    const erhe::scene::Node& joint,
    const quat&              local_rotation_before,
    const int                twist_axis
) -> Ik_joint_constraint
{
    const Ik_settings_data data = read_ik_settings(joint);

    Ik_joint_constraint constraint;
    constraint.twist_axis = twist_axis;

    const uint64_t flags = joint.get_flag_bits();
    constraint.lock[0] = erhe::utility::test_bit_set(flags, erhe::Item_flags::lock_rotation_x);
    constraint.lock[1] = erhe::utility::test_bit_set(flags, erhe::Item_flags::lock_rotation_y);
    constraint.lock[2] = erhe::utility::test_bit_set(flags, erhe::Item_flags::lock_rotation_z);
    bool any_ik_constraint = false;
    for (int axis = 0; axis < 3; ++axis) {
        constraint.lock [axis] = constraint.lock[axis] || data.lock[axis];
        constraint.limit[axis] = data.limit[axis];
        any_ik_constraint = any_ik_constraint || data.lock[axis] || data.limit[axis];
    }
    constraint.limit_min = data.limit_min;
    constraint.limit_max = data.limit_max;

    // The limits frame. A joint with any Ik lock or limit on takes the
    // effective Ik.rest_rotation - a local value, a style, or the per-object
    // default, which is the bind pose and otherwise identity: a fixed
    // zero, so the limits do not drift with the pose. The drag-start local
    // rotation is the rest only for a joint constrained by channel-lock flags
    // alone, which is the section 2 frame note's case.
    constraint.rest_rotation = any_ik_constraint ? data.rest_rotation : local_rotation_before;

    // Any lock or limit routes the chain into the constrained solver, the
    // twist axis included: world-space shortest arcs composed onto a bent
    // parent chain do turn a joint about its own twist axis.
    bool any_constraint = false;
    for (int axis = 0; axis < 3; ++axis) {
        if (constraint.lock[axis] || constraint.limit[axis]) {
            any_constraint = true;
        }
    }
    constraint.enabled = (twist_axis >= 0) && any_constraint;
    return constraint;
}

} // anonymous namespace

// Ik_drag_chain

void Ik_drag_chain::capture(std::vector<std::shared_ptr<erhe::scene::Node>>&& joints)
{
    reset();
    m_joints = std::move(joints);
    m_parent_from_joint_before.reserve(m_joints.size());
    m_xform_op_stack_before.reserve(m_joints.size());
    m_initial_positions.reserve(m_joints.size());
    m_local_rotations_before.reserve(m_joints.size());
    for (const std::shared_ptr<erhe::scene::Node>& joint : m_joints) {
        m_parent_from_joint_before.push_back(joint->parent_from_node_transform());
        m_xform_op_stack_before.push_back(joint->copy_xform_op_stack());
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
    m_constraints.push_back(Ik_joint_constraint{}); // tip entry, unused

    m_has_constraints = false;
    for (const Ik_joint_constraint& constraint : m_constraints) {
        if (constraint.enabled && (constraint.twist_axis >= 0)) {
            m_has_constraints = true;
            break;
        }
    }
}

void Ik_drag_chain::reset()
{
    m_joints.clear();
    m_parent_from_joint_before.clear();
    m_xform_op_stack_before.clear();
    m_initial_positions.clear();
    m_lengths.clear();
    m_local_rotations_before.clear();
    m_child_dir_local.clear();
    m_constraints.clear();
    m_has_constraints = false;
    m_has_pole        = false;
    m_pole_position   = vec3{0.0f};
    m_pole_angle      = 0.0f;
    m_pole_node.reset();
}

auto Ik_drag_chain::contains(const erhe::scene::Node& node) const -> bool
{
    for (const std::shared_ptr<erhe::scene::Node>& joint : m_joints) {
        if (joint.get() == &node) {
            return true;
        }
    }
    return false;
}

auto Ik_drag_chain::reach() const -> float
{
    float sum = 0.0f;
    for (const float segment_length : m_lengths) {
        sum += segment_length;
    }
    return sum;
}

void Ik_drag_chain::restore_drag_start_pose(const std::size_t first_index)
{
    for (std::size_t i = first_index; i < m_joints.size(); ++i) {
        m_joints[i]->set_parent_from_node(m_parent_from_joint_before[i]);
    }
}

void Ik_drag_chain::load_drag_start_pose()
{
    // Copy assignment into the chain's existing storage: no allocation once
    // the first apply() sized it.
    m_chain.positions       = m_initial_positions;
    m_chain.local_rotations = m_local_rotations_before;
}

void Ik_drag_chain::load_current_pose()
{
    // Root-to-tip refreshes make every cached world transform read here
    // current (a transform setter refreshes only the node it sets).
    const std::size_t joint_count = m_joints.size();
    m_chain.positions      .resize(joint_count);
    m_chain.local_rotations.resize(joint_count);
    for (std::size_t i = 0; i < joint_count; ++i) {
        erhe::scene::Node& joint = *m_joints[i];
        joint.update_world_from_node();
        m_chain.positions      [i] = vec3{joint.position_in_world()};
        m_chain.local_rotations[i] = joint.parent_from_node_transform().get_rotation();
    }
}

void Ik_drag_chain::set_pole(const std::shared_ptr<erhe::scene::Node>& pole, const glm::vec3 pole_position, const float pole_angle)
{
    m_has_pole      = true;
    m_pole_node     = pole;
    m_pole_position = pole_position;
    m_pole_angle    = pole_angle;
}

void Ik_drag_chain::solve_and_write_back(
    const glm::vec3 target,
    const glm::quat root_parent_world_rotation,
    const float     pole_weight
)
{
    // Both paths solve through the Ik_solver interface, so the pole step has
    // one place to act (doc/plans/rigging/pole_target.md R15). A chain with
    // neither constraints nor a pole reaches the untouched Phase 1
    // fabrik_solve inside Fabrik_solver::solve, with the same arguments, so
    // it still produces Phase 1 results bit for bit.
    m_chain.lengths         = m_lengths;
    m_chain.child_dir_local = m_child_dir_local;
    m_chain.constraints     = m_constraints;
    m_chain.root_parent_world_rotation = root_parent_world_rotation;
    m_chain.target          = target;
    m_chain.tolerance       = c_solve_tolerance;
    m_chain.max_iterations  = c_max_iterations;
    // The pole was discovered once, by Ik_drag::begin() (R12).
    m_chain.has_pole        = m_has_pole;
    m_chain.pole_position   = m_pole_position;
    m_chain.pole_angle      = m_pole_angle;
    m_chain.pole_weight     = pole_weight;
    m_solver.solve(m_chain);

    if (m_has_constraints) {
        // Constrained path: the solver returns constraint-satisfying local
        // rotations; write them back directly (translations and scales stay
        // at drag-start values - bone lengths never change).
        //
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
        // Unconstrained: the solved positions drive a rotation-only
        // write-back (local_rotations were left untouched by the solver).
        //
        // Rotation-only write-back, sequentially root to tip: each joint's
        // child direction is re-read under the already-updated ancestors
        // before computing that joint's world-space shortest-arc delta
        // (computing all deltas against the pre-solve pose simultaneously
        // would be wrong).
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
                m_chain.positions[i + 1] - joint_position,
                world_from_joint.get_rotation()
            );
            joint.set_world_from_node(erhe::scene::rotate(world_from_joint, rotation_delta));
        }
    }

    // The tip's local transform is the caller's (an effector orientation, or
    // the lower chain's pinned end); its cached world transform follows its
    // now final parent.
    m_joints.back()->update_world_from_node();
}

void Ik_drag_chain::append_transform_operations(
    const std::size_t                        first_index,
    std::vector<std::shared_ptr<Operation>>& operations
) const
{
    for (std::size_t i = first_index; i < m_joints.size(); ++i) {
        const erhe::scene::Trs_transform parent_from_joint_after = m_joints[i]->parent_from_node_transform();
        if (parent_from_joint_after == m_parent_from_joint_before[i]) {
            continue;
        }
        operations.push_back(
            std::make_shared<Node_transform_operation>(
                Node_transform_operation::Parameters{
                    .node                    = m_joints[i],
                    .parent_from_node_before = m_parent_from_joint_before[i],
                    .parent_from_node_after  = parent_from_joint_after,
                    .xform_op_stack_before   = m_xform_op_stack_before[i]
                }
            )
        );
    }
}

// Ik_drag

auto Ik_drag::begin(
    const std::shared_ptr<erhe::scene::Node>& effector,
    const Ik_drag_options&                    options
) -> bool
{
    reset();
    m_options = options;
    if (!effector) {
        return false;
    }
    const bool effector_is_bone = erhe::scene::is_bone(effector.get());
    if (effector_is_bone) {
        if (has_ik_lock(*effector)) {
            return false;
        }
    } else {
        // Non-bone drag handle (an Add Bone Tip Nodes tip, or any node
        // parented under a bone): it joins the chain as the effector point,
        // so the parent bone rotates to aim at it - which a bone-effector
        // drag never does (the effector is never aimed at anything; what its
        // own orientation does is the Ik_effector_orientation choice).
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
    m_upper.capture(std::move(joints));

    const std::shared_ptr<erhe::scene::Node> root_parent = m_upper.get_joints().front()->get_parent_node();
    m_root_parent_world_rotation = root_parent
        ? root_parent->world_from_node_transform().get_rotation()
        : quat{1.0f, 0.0f, 0.0f, 0.0f};
    m_effector_world_rotation_before = effector->world_from_node_transform().get_rotation();

    // The lower chain (ik_drag_options.md R21): from the effector down through
    // each bone's only bone child, ending at the first bone with no bone
    // child or with several (the end joint). It exists with at least two
    // joints; a non-bone drag handle never has one.
    if ((options.mid_chain_drag == Ik_mid_chain_drag::pin_chain_end) && effector_is_bone) {
        std::vector<std::shared_ptr<erhe::scene::Node>> lower_joints;
        lower_joints.push_back(effector);
        std::shared_ptr<erhe::scene::Node> lower_current = effector;
        while (true) {
            std::shared_ptr<erhe::scene::Node> only_bone_child;
            std::size_t                        bone_child_count = 0;
            for (const std::shared_ptr<erhe::Hierarchy>& child : lower_current->get_children()) {
                const std::shared_ptr<erhe::scene::Node> child_node = std::dynamic_pointer_cast<erhe::scene::Node>(child);
                if (!child_node || !erhe::scene::is_bone(child_node.get())) {
                    continue;
                }
                ++bone_child_count;
                only_bone_child = child_node;
            }
            if (bone_child_count != 1) {
                break;
            }
            lower_joints.push_back(only_bone_child);
            lower_current = only_bone_child;
        }
        if (lower_joints.size() >= 2) {
            m_end_world_rotation_before = lower_joints.back()->world_from_node_transform().get_rotation();
            m_lower.capture(std::move(lower_joints));
        }
    }

    // Poles after both chains are captured: a pole that is a joint of either
    // chain is inadmissible (R8).
    discover_pole(m_upper, "");
    if (m_lower.is_active()) {
        discover_pole(m_lower, " (lower chain)");
    }
    return true;
}

void Ik_drag::discover_pole(Ik_drag_chain& chain, const char* const chain_label)
{
    const std::vector<std::shared_ptr<erhe::scene::Node>>& joints = chain.get_joints();

    // R10: a two-joint chain has no intermediate joint, so a pole could not
    // act on it. That is a legitimate short chain, so it is left unpoled
    // without a warning and without reading any pole value.
    if (joints.size() < 3) {
        return;
    }

    const erhe::scene::Node&  effector           = *m_upper.get_joints().back();
    const erhe::Item_host*    effector_host      = effector.get_item_host();
    const erhe::scene::Node*  rejected_pole      = nullptr;
    const char*               rejected_reason    = nullptr;

    // R5: scan tip toward the root and take the first joint node whose
    // Ik.pole_target resolves to an admissible pole, so a pole authored on
    // the effector - where Blender's IK constraint itself lives - wins. The
    // lower chain scans from its end joint toward the effector
    // (ik_drag_options.md R22).
    for (std::size_t i = joints.size(); i-- > 0;) {
        const std::shared_ptr<erhe::scene::Node> pole = get_ik_pole_target(*joints[i]);
        if (!pole) {
            continue; // nothing authored here (or the reference died): keep scanning
        }

        // R8: admissibility, evaluated once per drag.
        const char* reason = nullptr;
        if (pole->get_item_host() != effector_host) {
            reason = "it is hosted by another scene";
        } else if (!pole->get_value(erhe::Item_base::active_property) || !pole->is_active()) {
            reason = "it is not active";
        } else if (m_upper.contains(*pole) || m_lower.contains(*pole)) {
            reason = "it is itself a joint of the dragged chain";
        }
        if (reason != nullptr) {
            if (rejected_reason == nullptr) {
                rejected_pole   = pole.get();
                rejected_reason = reason;
            }
            continue;
        }

        chain.set_pole(
            pole,
            vec3{pole->position_in_world()},           // R9: captured once, at drag start
            joints[i]->get_value(Ik::pole_angle_property) // R6: the same node's effective angle
        );
        return;
    }

    // R8: one warning per drag, never per solver update.
    if (rejected_reason != nullptr) {
        log_trs_tool->warn(
            "IK drag on '{}'{}: pole target '{}' is ignored because {}; solving without a pole",
            effector.get_name(), chain_label, rejected_pole->get_name(), rejected_reason
        );
    }
}

auto Ik_drag::make_transform_operation() const -> std::shared_ptr<Operation>
{
    Compound_operation::Parameters parameters;
    m_upper.append_transform_operations(0, parameters.operations);
    // The lower chain's first joint is the effector, already covered above.
    m_lower.append_transform_operations(1, parameters.operations);
    if (parameters.operations.empty()) {
        return {};
    }
    return std::make_shared<Compound_operation>(std::move(parameters));
}

auto Ik_drag::pole_weight(const glm::vec3 target_position_in_world) const -> float
{
    // Snap: the full swivel from the first apply() (ik_drag_options.md R27).
    if (m_options.pole_alignment == Ik_pole_alignment::snap) {
        return 1.0f;
    }
    // Ease In (R28): w = clamp(d / (pole_ease_distance * reach), 0, 1), d the
    // distance from the effector's drag-start position to the target and
    // reach the upper chain's. It depends on the target only, so a Drag Start
    // drag stays path independent and dragging back to the start eases the
    // pole back out. The lower chain solves with the same weight: its own
    // target never moves, so the drag's progress is the only measure there is.
    const float ease_length = m_options.pole_ease_distance * m_upper.reach();
    if (ease_length <= 0.0f) {
        return 1.0f; // a chain of zero-length segments: nothing to ease over
    }
    const std::size_t effector_index = m_upper.get_joints().size() - 1;
    const float d = distance(m_upper.get_initial_position(effector_index), target_position_in_world);
    return std::clamp(d / ease_length, 0.0f, 1.0f);
}

void Ik_drag::apply(const glm::vec3 target_position_in_world)
{
    if (!is_active()) {
        return;
    }

    const std::size_t effector_index  = m_upper.get_joints().size() - 1;
    const bool        has_lower_chain = m_lower.is_active();

    // The pose this step solves from (ik_drag_options.md R25, R26). The
    // target is absolute either way.
    const bool solve_from_previous_step =
        (m_options.solve_from == Ik_solve_from::previous_step) && m_has_previous_step;
    m_has_previous_step = true;
    if (solve_from_previous_step) {
        // Previous Step: the solved joints keep the pose the previous apply()
        // left, and the solve's input is that pose - the joints' current world
        // positions (all the unconstrained path reads) and their current local
        // rotations (the constrained path's start, from which the no-teleport
        // extension of a limit is taken). Without a lower chain the effector
        // is not a solved joint: its local transform goes back to its
        // drag-start value, so its own orientation is decided by the effector
        // orientation alone (R3, R4) and never accumulates. With a lower chain
        // the effector is the lower chain's root, a solved joint, and keeps
        // its previous step too (R26).
        if (!has_lower_chain) {
            m_upper.restore_drag_start_pose(effector_index);
        }
        m_upper.load_current_pose();
    } else {
        // Drag Start, and the first step of Previous Step: restore the
        // drag-start pose of both chains, so each solve starts from the same
        // pose and dragging back to the start restores it.
        m_upper.restore_drag_start_pose(0);
        if (has_lower_chain) {
            m_lower.restore_drag_start_pose(1);
        }
        m_upper.load_drag_start_pose();
    }

    m_pole_weight = pole_weight(target_position_in_world);
    m_upper.solve_and_write_back(target_position_in_world, m_root_parent_world_rotation, m_pole_weight);

    erhe::scene::Node& effector = *m_upper.get_joints()[effector_index];

    if (has_lower_chain) {
        // Pin Chain End (ik_drag_options.md R22, R23): the lower chain is a
        // chain of its own rooted at the effector's solved position, solved
        // from the pose it rides in after the upper solve toward the end
        // joint's drag-start world position. The effector is its root, so the
        // lower solve sets the effector's rotation and the effector
        // orientation does not apply. The effector's parent - the upper
        // chain's last solved joint - is final, so its world rotation is the
        // lower root's parent frame for this step.
        const erhe::scene::Node& effector_parent = *m_upper.get_joints()[effector_index - 1];
        const std::size_t        end_index       = m_lower.get_joints().size() - 1;
        m_lower.load_current_pose();
        m_lower.solve_and_write_back(
            m_lower.get_initial_position(end_index),
            effector_parent.world_from_node_transform().get_rotation(),
            m_pole_weight
        );

        // The end joint keeps its drag-start world rotation, so everything
        // below it stays in place in the world.
        erhe::scene::Node& end_joint = *m_lower.get_joints()[end_index];
        const quat end_rotation   = end_joint.world_from_node_transform().get_rotation();
        const quat restore_delta  = m_end_world_rotation_before * inverse(end_rotation);
        end_joint.set_world_from_node(erhe::scene::rotate(end_joint.world_from_node_transform(), restore_delta));
        return;
    }

    // The effector's own orientation (ik_drag_options.md R3, R4). Its local
    // rotation was never touched: the write-back stops before it, and the top
    // of apply() put its drag-start value back. So follow_last_segment only
    // needs the refresh of the effector's cached world transform under its
    // solved parent that solve_and_write_back() made - it then rides rigidly
    // on that parent like any other child. keep_world additionally rotates it
    // back to its drag-start world rotation, so only its position follows the
    // chain.
    if (m_options.effector_orientation == Ik_effector_orientation::keep_world) {
        const quat effector_rotation = effector.world_from_node_transform().get_rotation();
        const quat restore_delta = m_effector_world_rotation_before * inverse(effector_rotation);
        effector.set_world_from_node(erhe::scene::rotate(effector.world_from_node_transform(), restore_delta));
    }
}

void Ik_drag::reset()
{
    m_upper.reset();
    m_lower.reset();
    m_root_parent_world_rotation     = quat{1.0f, 0.0f, 0.0f, 0.0f};
    m_effector_world_rotation_before = quat{1.0f, 0.0f, 0.0f, 0.0f};
    m_end_world_rotation_before      = quat{1.0f, 0.0f, 0.0f, 0.0f};
    m_options           = Ik_drag_options{};
    m_has_previous_step = false;
    m_pole_weight       = 1.0f;
}

}
