#pragma once

#include "transform/ik_solver.hpp"

#include "erhe_scene/trs_transform.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>
#include <vector>

namespace erhe::scene {
    class Node;
}

namespace editor {

// Interactive IK state for one translate drag of a bone (see
// doc/fabrik-ik-requirements.md and doc/ik-settings-requirements.md).
// Captures the chain, its drag-start pose, and the per-joint constraints
// (Ik_settings attachments OR-ed with lock_rotation_* channel-lock flags)
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
    auto begin(const std::shared_ptr<erhe::scene::Node>& effector) -> bool;

    // Solve against target and write the pose to the joint nodes. Restores
    // the drag-start pose first, so the target is absolute and dragging back
    // to the start position restores the starting pose exactly.
    void apply(glm::vec3 target_position_in_world);

    void reset();

    [[nodiscard]] auto is_active() const -> bool { return !m_joints.empty(); }

    // Joints in root..effector order (valid while active).
    [[nodiscard]] auto get_joints() const -> const std::vector<std::shared_ptr<erhe::scene::Node>>& { return m_joints; }

private:
    std::vector<std::shared_ptr<erhe::scene::Node>> m_joints; // root .. effector
    std::vector<erhe::scene::Trs_transform> m_parent_from_joint_before;
    std::vector<glm::vec3>                  m_initial_positions;
    std::vector<float>                      m_lengths;
    std::vector<glm::quat>                  m_local_rotations_before;
    std::vector<glm::vec3>                  m_child_dir_local;
    std::vector<Ik_joint_constraint>        m_constraints;
    glm::quat                               m_root_parent_world_rotation{1.0f, 0.0f, 0.0f, 0.0f};
    bool                                    m_has_constraints{false};
    glm::quat                               m_effector_world_rotation_before{1.0f, 0.0f, 0.0f, 0.0f};
    std::vector<glm::vec3>                  m_scratch_positions;
    Ik_chain                                m_chain;
    Fabrik_solver                           m_solver;
};

}
