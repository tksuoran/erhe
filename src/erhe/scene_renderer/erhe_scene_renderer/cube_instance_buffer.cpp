// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_scene_renderer/cube_instance_buffer.hpp"
#include "erhe_scene_renderer/buffer_binding_points.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_gl/wrapper_functions.hpp"

namespace erhe::scene_renderer {

Cube_interface::Cube_interface(erhe::graphics::Device& graphics_device)
    : cube_instance_block  {graphics_device, "instance", cube_instance_buffer_binding_point, erhe::graphics::Shader_resource::Type::shader_storage_block}
    , cube_instance_struct {graphics_device, "Instance"}
    , cube_instance_offsets{
        .packed_position = cube_instance_struct.add_uint("packed_position")->get_offset_in_parent(),
    }

    , cube_control_block  {graphics_device, "cube_control", cube_control_buffer_binding_point, erhe::graphics::Shader_resource::Type::shader_storage_block}
    , cube_control_struct {graphics_device, "Cube_control"}
    , cube_control_offsets{
        .cube_size   = cube_control_struct.add_vec4("cube_size"  )->get_offset_in_parent(),
        .color_bias  = cube_control_struct.add_vec4("color_bias" )->get_offset_in_parent(),
        .color_scale = cube_control_struct.add_vec4("color_scale")->get_offset_in_parent(),
        .color_start = cube_control_struct.add_vec4("color_start")->get_offset_in_parent(),
        .color_end   = cube_control_struct.add_vec4("color_end"  )->get_offset_in_parent(),
    }
{
    cube_instance_block.add_struct("instances", &cube_instance_struct, erhe::graphics::Shader_resource::unsized_array);
    cube_instance_block.set_readonly(true);

    cube_control_block.add_struct("cube_control", &cube_control_struct, erhe::graphics::Shader_resource::unsized_array);
    cube_control_block.set_readonly(true);
}

Cube_instance_buffer::Cube_instance_buffer(
    erhe::graphics::Device&      graphics_device,
    Cube_interface&              cube_interface,
    const std::vector<uint32_t>& cubes
)
    : m_cube_interface{cube_interface}
    , m_buffer{
        graphics_device,
        erhe::graphics::Buffer_create_info{
            .capacity_byte_count = cube_interface.cube_instance_struct.get_size_bytes() * cubes.size(),
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

auto Cube_instance_buffer::bind(erhe::graphics::Render_command_encoder& encoder) -> std::size_t
{
    encoder.set_buffer(
        m_cube_interface.cube_instance_block.get_binding_target(),
        &m_buffer,
        0,
        m_buffer.capacity_byte_count(),
        m_cube_interface.cube_instance_block.get_binding_point()
    );
    return m_cube_count;
}

Cube_control_buffer::Cube_control_buffer(erhe::graphics::Device& graphics_device, Cube_interface& cube_interface)
    : GPU_ring_buffer_client{
        graphics_device,
        erhe::graphics::Buffer_target::storage,
        "cube_control",
        cube_interface.cube_control_block.get_binding_point()
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
) -> erhe::graphics::Buffer_range
{
    const std::size_t primitive_count = 1;
    const auto        entry_size      = m_cube_interface.cube_control_struct.get_size_bytes();
    const auto&       offsets         = m_cube_interface.cube_control_offsets;
    const std::size_t max_byte_count  = primitive_count * entry_size;
    erhe::graphics::Buffer_range buffer_range = acquire(erhe::graphics::Ring_buffer_usage::CPU_write, max_byte_count);
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
    buffer_range.bytes_written(write_offset);
    buffer_range.close();
    return buffer_range;
}

} // namespace erhe::scene_renderer
