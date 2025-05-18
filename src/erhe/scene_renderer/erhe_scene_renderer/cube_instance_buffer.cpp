// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_scene_renderer/cube_instance_buffer.hpp"
#include "erhe_gl/wrapper_functions.hpp"

namespace erhe::scene_renderer {

Cube_interface::Cube_interface(erhe::graphics::Instance& graphics_instance)
    : cube_instance_block  {graphics_instance, "instance", 5, erhe::graphics::Shader_resource::Type::shader_storage_block}
    , cube_instance_struct {graphics_instance, "Instance"}
    , cube_instance_offsets{
        .packed_position = cube_instance_struct.add_uint("packed_position")->offset_in_parent(),
    }

    , cube_control_block  {graphics_instance, "cube_control", 6, erhe::graphics::Shader_resource::Type::shader_storage_block}
    , cube_control_struct {graphics_instance, "Cube_control"}
    , cube_control_offsets{
        .cube_size   = cube_control_struct.add_vec4("cube_size"  )->offset_in_parent(),
        .color_bias  = cube_control_struct.add_vec4("color_bias" )->offset_in_parent(),
        .color_scale = cube_control_struct.add_vec4("color_scale")->offset_in_parent(),
        .color_start = cube_control_struct.add_vec4("color_start")->offset_in_parent(),
        .color_end   = cube_control_struct.add_vec4("color_end"  )->offset_in_parent(),
    }
{
    cube_instance_block.add_struct("instances", &cube_instance_struct, erhe::graphics::Shader_resource::unsized_array);
    cube_instance_block.set_readonly(true);

    cube_control_block.add_struct("cube_control", &cube_control_struct, erhe::graphics::Shader_resource::unsized_array);
    cube_control_block.set_readonly(true);
}

Cube_instance_buffer::Cube_instance_buffer(
    erhe::graphics::Instance&    graphics_instance,
    Cube_interface&              cube_interface,
    const std::vector<uint32_t>& cubes
)
    : m_cube_interface{cube_interface}
    , m_buffer{
        graphics_instance,
        erhe::graphics::Buffer_create_info{
            .target              = gl::Buffer_target::shader_storage_buffer,
            .capacity_byte_count = cube_interface.cube_instance_struct.size_bytes() * cubes.size(),
            .storage_mask        = gl::Buffer_storage_mask::map_write_bit,
            .access_mask         = gl::Map_buffer_access_mask::map_write_bit,
            .debug_label         = "Cube_instance_buffer"
        }
    }
    , m_cube_count{cubes.size()}
{
    std::span<uint32_t> gpu_data = m_buffer.map_elements<uint32_t>(
        0,
        cubes.size(),
        gl::Map_buffer_access_mask::map_write_bit
    );
    std::copy(cubes.begin(), cubes.end(), gpu_data.begin());
    m_buffer.unmap();
}

auto Cube_instance_buffer::bind() -> std::size_t
{
    gl::bind_buffer_base(
        m_buffer.target(),
        static_cast<GLuint>(m_cube_interface.cube_instance_block.binding_point()),
        static_cast<GLuint>(m_buffer.gl_name())
    );
    return m_cube_count;
}

Cube_control_buffer::Cube_control_buffer(erhe::graphics::Instance& graphics_instance, Cube_interface& cube_interface)
    : GPU_ring_buffer{
        graphics_instance,
        erhe::renderer::GPU_ring_buffer_create_info{
            .target        = gl::Buffer_target::shader_storage_buffer,
            .binding_point = cube_interface.cube_control_block.binding_point(),
            .size          = cube_interface.cube_control_struct.size_bytes() * 10,
            .debug_label   = "cube_control"
        }
    }
    , m_cube_interface{cube_interface}
{
}

auto Cube_control_buffer::update(
    const glm::vec4& cube_size,
    const glm::vec4& color_bias,
    const glm::vec4& color_scale,
    const glm::vec4& color_start,
    const glm::vec4& color_end
) -> erhe::renderer::Buffer_range
{
    const std::size_t primitive_count = 1;
    const auto        entry_size      = m_cube_interface.cube_control_struct.size_bytes();
    const auto&       offsets         = m_cube_interface.cube_control_offsets;
    const std::size_t max_byte_count  = primitive_count * entry_size;
    erhe::renderer::Buffer_range buffer_range = open(erhe::renderer::Ring_buffer_usage::CPU_write, max_byte_count);
    std::span<std::byte>         gpu_data     = buffer_range.get_span();
    std::size_t                  write_offset = 0;
    using erhe::graphics::as_span;
    using erhe::graphics::write;
    write(gpu_data, write_offset + offsets.cube_size,   as_span(cube_size));
    write(gpu_data, write_offset + offsets.color_bias,  as_span(color_bias));
    write(gpu_data, write_offset + offsets.color_scale, as_span(color_scale));
    write(gpu_data, write_offset + offsets.color_start, as_span(color_start));
    write(gpu_data, write_offset + offsets.color_end,   as_span(color_end));
    write_offset += entry_size;
    buffer_range.close(write_offset);
    return buffer_range;
}

} // namespace erhe::scene_renderer
