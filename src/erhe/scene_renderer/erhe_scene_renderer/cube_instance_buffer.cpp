// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_scene_renderer/cube_instance_buffer.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::scene_renderer {

Cube_interface::Cube_interface(erhe::graphics::Instance& graphics_instance)
    : cube_instance_block {graphics_instance, "instance", 5, erhe::graphics::Shader_resource::Type::shader_storage_block}
    , cube_instance_struct{graphics_instance, "Instance"}
    , offsets{
        .position = cube_instance_struct.add_vec4("position")->offset_in_parent(),
        .color    = cube_instance_struct.add_vec4("color")->offset_in_parent()
    }
{
    cube_instance_block.add_struct("instances", &cube_instance_struct, erhe::graphics::Shader_resource::unsized_array);
    cube_instance_block.set_readonly(true);
}

Cube_instance_buffer::Cube_instance_buffer(erhe::graphics::Instance& graphics_instance, Cube_interface& cube_interface)
    : m_cube_interface{cube_interface}
    , m_buffer{
        graphics_instance,
        erhe::graphics::Buffer_create_info{
            .target              = gl::Buffer_target::shader_storage_buffer,
            .capacity_byte_count = cube_interface.cube_instance_struct.size_bytes() * cube_interface.max_cube_instance_count,
            .storage_mask        = gl::Buffer_storage_mask::map_write_bit,
            .access_mask         = gl::Map_buffer_access_mask::map_write_bit,
            .debug_label         = "Cube_instance_buffer"
        }
    }
{
}

void Cube_instance_buffer::clear()
{
    m_frames.clear();
    m_buffer.clear();
}

auto Cube_instance_buffer::append_frame(const std::vector<Cube_instance>& cubes) -> std::size_t
{
    const std::size_t frame_index = m_frames.size();

    const std::size_t                byte_count  = cubes.size() * m_cube_interface.cube_instance_struct.size_bytes();
    const std::optional<std::size_t> byte_offset = m_buffer.allocate_bytes(byte_count);
    ERHE_VERIFY(byte_offset.has_value());

    m_frames.emplace_back(byte_offset.value(), cubes.size());

    std::span<std::byte> gpu_data = m_buffer.begin_write(byte_offset.value(), byte_count);
    std::byte* const     start    = gpu_data.data();
    ERHE_VERIFY(byte_count == gpu_data.size_bytes());

    const std::size_t    float_count = byte_count / sizeof(float);
    const std::span<float> gpu_float_data{ reinterpret_cast<float*>(start), float_count };
    size_t write_offset = 0;
    for (const Cube_instance& cube : cubes) {
        gpu_float_data[write_offset++] = cube.position.x;
        gpu_float_data[write_offset++] = cube.position.y;
        gpu_float_data[write_offset++] = cube.position.z;
        gpu_float_data[write_offset++] = cube.position.w;
        gpu_float_data[write_offset++] = cube.color.r;
        gpu_float_data[write_offset++] = cube.color.g;
        gpu_float_data[write_offset++] = cube.color.b;
        gpu_float_data[write_offset++] = cube.color.a;
    }
    m_buffer.end_write(byte_offset.value(), byte_count);

    return frame_index;
}

auto Cube_instance_buffer::bind(std::size_t frame_index) -> std::size_t
{
    ERHE_VERIFY(frame_index < m_frames.size());
    const Cube_frame_info& frame_info = m_frames[frame_index];
    gl::bind_buffer_range(
        m_buffer.target(),
        static_cast<GLuint>    (m_cube_interface.cube_instance_block.binding_point()),
        static_cast<GLuint>    (m_buffer.gl_name()),
        static_cast<GLintptr>  (frame_info.byte_offset),
        static_cast<GLsizeiptr>(frame_info.cube_count * m_cube_interface.cube_instance_struct.size_bytes())
    );
    return frame_info.cube_count;
}

} // namespace erhe::scene_renderer
