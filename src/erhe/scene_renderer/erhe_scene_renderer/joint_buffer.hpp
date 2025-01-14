#pragma once

#include "erhe_graphics/shader_resource.hpp"
#include "erhe_renderer/multi_buffer.hpp"

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
    explicit Joint_interface(erhe::graphics::Instance& graphics_instance);

    erhe::graphics::Shader_resource joint_block;
    erhe::graphics::Shader_resource joint_struct;
    Joint_block                     offsets;
    std::size_t                     max_joint_count{1000};
};

class Joint_buffer : public erhe::renderer::Multi_buffer
{
public:
    Joint_buffer(erhe::graphics::Instance& graphics_instance, Joint_interface& joint_interface);

    auto update(
        const glm::uvec4&                                          debug_joint_indices,
        const std::span<glm::vec4>&                                debug_joint_colors,
        const std::span<const std::shared_ptr<erhe::scene::Skin>>& skins
    ) -> erhe::renderer::Buffer_range;

private:
    erhe::graphics::Instance& m_graphics_instance;
    Joint_interface&          m_joint_interface;
};

} // namespace erhe::scene_renderer
