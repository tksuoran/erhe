#pragma once

#include "erhe_scene/trs_transform.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>
#include <vector>

namespace erhe::scene {
    class Node;
}

namespace editor {

// FABRIK solve (Aristidou & Lasenby 2011) on a chain of world-space joint
// positions. positions[0] is the fixed root; segment_lengths[i] is the
// distance from joint i to joint i+1 (positions.size() == lengths + 1).
// Positions are updated in place toward placing positions.back() at target
// without changing segment lengths. When the target is farther from the root
// than the total chain length, the chain is laid out straight toward the
// target in one pass (closest reachable point). Zero-length segments and
// degenerate directions are epsilon-guarded (never NaN).
void fabrik_solve(
    std::vector<glm::vec3>&   positions,
    const std::vector<float>& segment_lengths,
    glm::vec3                 target,
    float                     tolerance,
    int                       max_iterations
);

// Interactive IK state for one translate drag of a bone (see
// doc/fabrik-ik-requirements.md). Captures the chain and its drag-start pose
// in begin(); each apply() re-solves from that pose against an absolute
// world-space target and writes rotation-only changes back to the joint
// nodes (local translations never change, so bone lengths are preserved).
class Ik_drag
{
public:
    // Discover the chain for a dragged bone: ancestors are collected while
    // they are bones, stopping at (and including, as the fixed root) the
    // first ik_lock ancestor. Returns false - leaving the drag to plain FK -
    // when the effector is not a bone, is itself ik_lock, or has no bone
    // parent (chain of at least two joints required).
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
    glm::quat                               m_effector_world_rotation_before{1.0f, 0.0f, 0.0f, 0.0f};
    std::vector<glm::vec3>                  m_scratch_positions;
};

}
