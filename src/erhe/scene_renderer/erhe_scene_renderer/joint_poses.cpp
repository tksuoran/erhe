#include "erhe_scene_renderer/joint_poses.hpp"

#include "erhe_profile/profile.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/gtx/matrix_operation.hpp>

namespace erhe::scene_renderer {

void Joint_poses::capture(const std::span<const std::shared_ptr<erhe::scene::Skin>> skins)
{
    ERHE_PROFILE_FUNCTION();

    m_poses.clear();
    uint32_t joint_index = 0;
    for (const std::shared_ptr<erhe::scene::Skin>& skin : skins) {
        ERHE_VERIFY(skin);
        erhe::scene::Skin_data& skin_data = skin->skin_data;
        skin_data.joint_buffer_index = joint_index;
        for (std::size_t i = 0, end_i = skin_data.joints.size(); i < end_i; ++i) {
            const std::shared_ptr<erhe::scene::Node>& joint = skin_data.joints[i];
            const glm::mat4 joint_from_bind  = skin_data.inverse_bind_matrices[i];
            const glm::mat4 world_from_joint = joint->world_from_node();
            const glm::mat4 world_from_bind  = world_from_joint * joint_from_bind;
            m_poses.push_back(
                Joint_pose{
                    .world_from_bind  = world_from_bind,
                    .normal_transform = glm::transpose(glm::adjugate(world_from_bind)), // TODO compute shader pass?
                    .joint            = joint.get()
                }
            );
            ++joint_index;
        }
    }
}

void Joint_poses::clear()
{
    m_poses.clear();
}

} // namespace erhe::scene_renderer
