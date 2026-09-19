#pragma once

#include "transform/ik_solver.hpp"

#include "erhe_scene/trs_transform.hpp"
#include "erhe_scene/xform_op.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>
#include <optional>
#include <vector>

namespace erhe::scene {
    class Xformable; using Node = Xformable;
}

namespace editor {

class Operation;

// What the effector's own orientation does while the chain bends under it
// (doc/plans/rigging/ik_drag_options.md section 1). The solver rotates every
// joint except the effector - the effector has no child segment in the chain -
// so this is a free choice:
//   keep_world          - the effector keeps its drag-start WORLD rotation;
//                         only its position follows the chain (Phase 1).
//   follow_last_segment - the effector keeps its drag-start LOCAL rotation, so
//                         it rides rigidly on its solved parent joint like any
//                         other child of that joint.
enum class Ik_effector_orientation : unsigned int {
    keep_world          = 0,
    follow_last_segment = 1
};

static constexpr const char* c_ik_effector_orientation_strings[] = {
    "Keep World",
    "Follow Last Segment"
};

// Interactive IK state for one translate drag of a bone (see
// doc/plans/rigging/fabrik_ik.md and doc/plans/rigging/ik_settings.md).
// Captures the chain, its drag-start pose, and the per-joint constraints
// (the joints' Ik.* values OR-ed with lock_rotation_* channel-lock flags)
// in begin(); each apply() re-solves from that pose against an absolute
// world-space target through the Ik_solver interface and writes
// rotation-only changes back to the joint nodes (local translations never
// change, so bone lengths are preserved).
class Ik_drag
{
public:
    // Discover the chain for a dragged node: ancestors are collected while
    // they are bones, stopping at (and including, as the fixed root) the
    // first ik_lock ancestor. The effector is either a bone, or a non-bone
    // drag handle parented under a bone (e.g. an Add Bone Tip Nodes tip) -
    // the handle joins the chain as the effector point, so its parent bone
    // rotates to aim at it. Returns false - leaving the drag to plain FK -
    // when the effector is an ik_lock bone, or when neither the effector nor
    // its parent is a bone (chain of at least two joints required).
    //
    // effector_orientation is captured for the whole gesture, beside the chain
    // and the pole, so every apply() of one drag solves under one mode and
    // reads no setting (ik_drag_options.md R2).
    auto begin(
        const std::shared_ptr<erhe::scene::Node>& effector,
        Ik_effector_orientation                   effector_orientation
    ) -> bool;

    // Solve against target and write the pose to the joint nodes. Restores
    // the drag-start pose first, so the target is absolute and dragging back
    // to the start position restores the starting pose exactly.
    void apply(glm::vec3 target_position_in_world);

    void reset();

    [[nodiscard]] auto is_active() const -> bool { return !m_joints.empty(); }

    // Joints in root..effector order (valid while active).
    [[nodiscard]] auto get_joints() const -> const std::vector<std::shared_ptr<erhe::scene::Node>>& { return m_joints; }

    // The pole governing this drag, discovered once by begin()
    // (doc/plans/rigging/pole_target.md R5-R10). get_pole_node() is null and
    // get_pole_angle() is 0 when the solve is unpoled.
    [[nodiscard]] auto has_pole     () const -> bool { return m_has_pole; }
    [[nodiscard]] auto get_pole_node() const -> std::shared_ptr<erhe::scene::Node> { return m_pole_node.lock(); }
    [[nodiscard]] auto get_pole_angle() const -> float { return m_pole_angle; }
    // The pole's world position as begin() captured it (R9): a drag-start
    // capture, so the chain visualization's pole line does not chase a pole
    // that moves during the gesture. Meaningless when has_pole() is false.
    [[nodiscard]] auto get_pole_position() const -> glm::vec3 { return m_pole_position; }

    // One Compound_operation of Node_transform_operation covering the joints
    // whose parent_from_node changed since begin(), so a complete gesture is
    // one undo step (R19, R22). Null when no joint moved. The interactive
    // Transform tool records its gesture through
    // Transform_tool::record_transform_operation() instead: that path owns the
    // autokey and physics-spring state a one-shot scripted gesture has none of,
    // and it covers the chain because try_translate_ik() appends the chain's
    // joints to the tool's transform entries.
    [[nodiscard]] auto make_transform_operation() const -> std::shared_ptr<Operation>;

private:
    // Fills the pole members from the chain joints' Ik.pole_target and
    // Ik.pole_angle values. Called
    // by begin() while every joint still sits at its drag-start transform, so
    // the pole's world position is a drag-start capture (R9, R12).
    void discover_pole();

    std::vector<std::shared_ptr<erhe::scene::Node>> m_joints; // root .. effector
    std::vector<erhe::scene::Trs_transform> m_parent_from_joint_before;
    std::vector<std::optional<erhe::scene::Xform_op_stack>> m_xform_op_stack_before;
    std::vector<glm::vec3>                  m_initial_positions;
    std::vector<float>                      m_lengths;
    std::vector<glm::quat>                  m_local_rotations_before;
    std::vector<glm::vec3>                  m_child_dir_local;
    std::vector<Ik_joint_constraint>        m_constraints;
    glm::quat                               m_root_parent_world_rotation{1.0f, 0.0f, 0.0f, 0.0f};
    bool                                    m_has_constraints{false};
    glm::quat                               m_effector_world_rotation_before{1.0f, 0.0f, 0.0f, 0.0f};
    Ik_effector_orientation                 m_effector_orientation{Ik_effector_orientation::keep_world};
    bool                                    m_has_pole{false};
    glm::vec3                               m_pole_position{0.0f};
    float                                   m_pole_angle{0.0f};
    std::weak_ptr<erhe::scene::Node>        m_pole_node; // weak: a drag never keeps a scene node alive
    Ik_chain                                m_chain;
    Fabrik_solver                           m_solver;
};

}
