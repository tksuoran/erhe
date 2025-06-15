#pragma once

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/shader_resource.hpp"

#include <glm/glm.hpp>

namespace erhe::scene {
    class Skin;
}

namespace erhe::scene_renderer {

class Joint_struct
{
public:
    std::size_t world_from_bind;    // mat4 16 * 4 bytes
    std::size_t normal_transform;   // mat4 16 * 4 bytes
};

class Joint_block
{
public:
    std::size_t  debug_joint_indices;     // uvec4
    std::size_t  debug_joint_color_count; // uint
    std::size_t  extra1;                  // uint
    std::size_t  extra2;                  // uint
    std::size_t  extra3;                  // uint
    std::size_t  debug_joint_colors;
    Joint_struct joint;
    std::size_t  joint_struct;
};

class Joint_interface
{
public:
    explicit Joint_interface(erhe::graphics::Device& graphics_device);

    erhe::graphics::Shader_resource joint_block;
    erhe::graphics::Shader_resource joint_struct;
    Joint_block                     offsets;
    std::size_t                     max_joint_count{1000};
};

class Joint_buffer : public erhe::graphics::GPU_ring_buffer_client
{
public:
    Joint_buffer(erhe::graphics::Device& graphics_device, Joint_interface& joint_interface);

    auto update(
        const glm::uvec4&                                          debug_joint_indices,
        const std::span<glm::vec4>&                                debug_joint_colors,
        const std::span<const std::shared_ptr<erhe::scene::Skin>>& skins
    ) -> erhe::graphics::Buffer_range;

private:
    erhe::graphics::Device& m_graphics_device;
    Joint_interface&        m_joint_interface;
};

} // namespace erhe::scene_renderer
