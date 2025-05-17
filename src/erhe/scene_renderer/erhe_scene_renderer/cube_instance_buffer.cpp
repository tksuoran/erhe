// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_scene_renderer/cube_instance_buffer.hpp"
#include "erhe_gl/wrapper_functions.hpp"

namespace erhe::scene_renderer {

Cube_interface::Cube_interface(erhe::graphics::Instance& graphics_instance)
    : cube_instance_block {graphics_instance, "instance", 5, erhe::graphics::Shader_resource::Type::shader_storage_block}
    , cube_instance_struct{graphics_instance, "Instance"}
    , offsets{
        .position = cube_instance_struct.add_uint("packed_position")->offset_in_parent(),
    }
{
    cube_instance_block.add_struct("instances", &cube_instance_struct, erhe::graphics::Shader_resource::unsized_array);
    cube_instance_block.set_readonly(true);
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

} // namespace erhe::scene_renderer
