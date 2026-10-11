// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_scene_renderer/joint_buffer.hpp"
#include "erhe_scene_renderer/buffer_binding_points.hpp"

#include "erhe_graphics/span.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::scene_renderer {

Joint_interface::Joint_interface(erhe::graphics::Device& graphics_device, const int max_joint_count)
    : joint_block{erhe::graphics::Shader_resource{
        graphics_device,
        {
            .name          = "joint",
            .binding_point = joint_buffer_binding_point,
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    }}
    , joint_struct{erhe::graphics::Shader_resource{graphics_device, "Joint"}}
    , max_joint_count{static_cast<std::size_t>(max_joint_count)}
{

    offsets.debug_joint_indices     = joint_block.add_uvec4("debug_joint_indices"    )->get_offset_in_parent();
    offsets.debug_joint_color_count = joint_block.add_uint ("debug_joint_color_count")->get_offset_in_parent(),
    offsets.extra1                  = joint_block.add_uint ("extra1"                 )->get_offset_in_parent(),
    offsets.extra2                  = joint_block.add_uint ("extra2"                 )->get_offset_in_parent(),
    offsets.extra3                  = joint_block.add_uint ("extra3"                 )->get_offset_in_parent(),
    offsets.debug_joint_colors      = joint_block.add_vec4 ("debug_joint_colors",  32)->get_offset_in_parent();
    offsets.joint = {
        .world_from_bind  = joint_struct.add_mat4 ("world_from_bind"       )->get_offset_in_parent(),
        .normal_transform = joint_struct.add_mat4 ("world_from_bind_normal")->get_offset_in_parent(),
        .debug_flags      = joint_struct.add_uvec4("debug_flags"           )->get_offset_in_parent()
    };

    offsets.joint_struct = joint_block.add_struct("joints", &joint_struct, erhe::graphics::Shader_resource::unsized_array)->get_offset_in_parent();
}

Joint_buffer::Joint_buffer(erhe::graphics::Device& graphics_device, Joint_interface& joint_interface)
    : Ring_buffer_client{
        graphics_device,
        joint_interface.joint_block.get_binding_target(),
        "Joint_buffer",
        joint_interface.joint_block.get_binding_point()
    }
    , m_graphics_device{graphics_device}
    , m_joint_interface{joint_interface}
{
}

auto Joint_buffer::update(
    const glm::uvec4&                 debug_joint_indices,
    const std::span<const glm::vec4>& debug_joint_colors,
    const std::span<const Joint_pose> joint_poses,
    const erhe::scene::Node*          debug_target_joint
) -> erhe::graphics::Ring_buffer_range
{
    ERHE_PROFILE_FUNCTION();

    SPDLOG_LOGGER_TRACE(log_render, "joint_poses.size() = {}, m_writer.write_offset = {}", joint_poses.size(), m_writer.write_offset);

    const std::size_t joint_count = joint_poses.size();

    const auto        entry_size       = m_joint_interface.joint_struct.get_size_bytes();
    const auto&       offsets          = m_joint_interface.offsets;
    const std::size_t exact_byte_count = offsets.joint_struct + joint_count * entry_size;

    // Clamp acquire to the block's reported size so MoltenVK's Metal argument
    // validation (offset + MSL_struct_size <= buffer_size) always holds, even
    // when joint_count == 0 and the ring's tail fragment is smaller than
    // prefix + one joint.
    const std::size_t acquire_byte_count = std::max(exact_byte_count, m_joint_interface.joint_block.get_size_bytes());

    erhe::graphics::Ring_buffer_range buffer_range       = acquire(erhe::graphics::Ring_buffer_usage::CPU_write, acquire_byte_count);
    std::span<std::byte>              primitive_gpu_data = buffer_range.get_span();
    std::size_t                       write_offset       = 0;

    using erhe::graphics::as_span;
    using erhe::graphics::write;

    const uint32_t debug_joint_color_count = static_cast<uint32_t>(debug_joint_colors.size());
    const uint32_t extra1 = 0;
    const uint32_t extra2 = 0;
    const uint32_t extra3 = 0;

    write(primitive_gpu_data, write_offset + offsets.debug_joint_indices,     as_span(debug_joint_indices    ));
    write(primitive_gpu_data, write_offset + offsets.debug_joint_color_count, as_span(debug_joint_color_count));
    write(primitive_gpu_data, write_offset + offsets.extra1,                  as_span(extra1));
    write(primitive_gpu_data, write_offset + offsets.extra2,                  as_span(extra2));
    write(primitive_gpu_data, write_offset + offsets.extra3,                  as_span(extra3));

    if (!debug_joint_colors.empty()) {
        uint32_t color_index = 0;
        for (
            std::size_t i = 0, end = std::min(debug_joint_colors.size(), std::size_t{32});
            i < end;
            ++i
        ) {
            const glm::vec4& color = debug_joint_colors[i];
            write(
                primitive_gpu_data,
                write_offset + offsets.debug_joint_colors + (color_index * 4 * sizeof(float)),
                as_span(color)
            );
            ++color_index;
        }
    }

    write_offset += offsets.joint_struct;

    uint32_t joint_index = 0;
    for (const Joint_pose& pose : joint_poses) {
        const glm::uvec4 debug_flags{
            ((debug_target_joint != nullptr) && (pose.joint == debug_target_joint)) ? 1u : 0u,
            0u,
            0u,
            0u
        };

        write(primitive_gpu_data, write_offset + offsets.joint.world_from_bind,  as_span(pose.world_from_bind ));
        write(primitive_gpu_data, write_offset + offsets.joint.normal_transform, as_span(pose.normal_transform));
        write(primitive_gpu_data, write_offset + offsets.joint.debug_flags,      as_span(debug_flags          ));
        write_offset += entry_size;
        ++joint_index;
    }

    buffer_range.bytes_written(write_offset);
    buffer_range.close();

    SPDLOG_LOGGER_TRACE(log_draw, "wrote {} entries to joint buffer", joint_index);

    return buffer_range;
}

} //namespace erhe::scene_renderer
