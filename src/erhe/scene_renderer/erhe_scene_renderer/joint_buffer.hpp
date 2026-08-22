#pragma once

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/shader_resource.hpp"

#include <glm/glm.hpp>

namespace erhe::scene { class Node; class Skin; }

namespace erhe::scene_renderer {

class Joint_struct
{
public:
    std::size_t world_from_bind;    // mat4 16 * 4 bytes
    std::size_t normal_transform;   // mat4 16 * 4 bytes
    // Per-joint debug flags. .x != 0 marks this joint slot as the weight
    // display's active joint (Shader_debug::joint_weight_ramp target); .yzw
    // are reserved. Per-SLOT rather than a single global index because one
    // joint Node is commonly a joint of several skins (every skin of a
    // multi-mesh character rig shares the rig's nodes), and each skin gets
    // its own range of slots in this buffer. A uvec4 rather than a bare uint
    // so the struct size stays a multiple of 16 - update() strides the
    // buffer by get_size_bytes(), which must match the std140 array stride.
    std::size_t debug_flags;        // uvec4
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
    Joint_interface(erhe::graphics::Device& graphics_device, int max_joint_count);

    erhe::graphics::Shader_resource joint_block;
    erhe::graphics::Shader_resource joint_struct;
    Joint_block                     offsets;
    std::size_t                     max_joint_count{1000};
};

class Joint_buffer : public erhe::graphics::Ring_buffer_client
{
public:
    Joint_buffer(erhe::graphics::Device& graphics_device, Joint_interface& joint_interface);

    // debug_target_joint: the weight display's active joint, or nullptr.
    // Every slot whose joint Node is this node gets debug_flags.x = 1, in
    // every skin that uses it.
    auto update(
        const glm::uvec4&                                          debug_joint_indices,
        const std::span<glm::vec4>&                                debug_joint_colors,
        const std::span<const std::shared_ptr<erhe::scene::Skin>>& skins,
        const erhe::scene::Node*                                   debug_target_joint = nullptr
    ) -> erhe::graphics::Ring_buffer_range;

private:
    erhe::graphics::Device& m_graphics_device;
    Joint_interface&        m_joint_interface;
};

} // namespace erhe::scene_renderer
