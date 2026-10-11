#pragma once

#include <glm/glm.hpp>

#include <memory>
#include <span>
#include <vector>

namespace erhe::scene { class Xformable; using Node = Xformable; class Skin; }

namespace erhe::scene_renderer {

// One joint of one skin, posed: the matrices Joint_buffer::update() writes for
// the joint's slot, plus the joint node as an identity key for the weight
// display's debug target (compared, never dereferenced).
class Joint_pose
{
public:
    glm::mat4                world_from_bind {1.0f}; // world_from_joint * inverse_bind
    glm::mat4                normal_transform{1.0f}; // transpose(adjugate(world_from_bind))
    const erhe::scene::Node* joint           {nullptr};
};

// The posed joints of a scene's skins, copied for a render phase: one
// Joint_pose per joint of every skin, in skin order, with each skin's
// Skin_data::joint_buffer_index set to its first entry. capture() is called
// by the thread that owns the skins and their joint nodes, after the frame's
// node transforms are final (the editor's main thread in its prepare phase);
// Joint_buffer::update() copies the entries into the frame's joint buffer, so
// a render phase on another thread reads no joint node. Keeps its capacity.
class Joint_poses
{
public:
    void capture(std::span<const std::shared_ptr<erhe::scene::Skin>> skins);
    void clear();

    [[nodiscard]] auto get_poses() const -> std::span<const Joint_pose> { return std::span<const Joint_pose>{m_poses}; }

private:
    std::vector<Joint_pose> m_poses;
};

} // namespace erhe::scene_renderer
