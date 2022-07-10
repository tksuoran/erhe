// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "renderers/camera_buffer.hpp"
#include "renderers/program_interface.hpp"
#include "log.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/scene/projection.hpp"
#include "erhe/scene/transform.hpp"
#include "erhe/scene/viewport.hpp"
#include "erhe/toolkit/profile.hpp"


namespace editor
{


Camera_interface::Camera_interface(std::size_t max_camera_count)
    : camera_block {
        "camera",
        4,
        erhe::graphics::Shader_resource::Type::uniform_block
    }
    , camera_struct{"Camera"}
    , offsets{
        .world_from_node      = camera_struct.add_mat4 ("world_from_node"     )->offset_in_parent(),
        .world_from_clip      = camera_struct.add_mat4 ("world_from_clip"     )->offset_in_parent(),
        .clip_from_world      = camera_struct.add_mat4 ("clip_from_world"     )->offset_in_parent(),
        .viewport             = camera_struct.add_vec4 ("viewport"            )->offset_in_parent(),
        .fov                  = camera_struct.add_vec4 ("fov"                 )->offset_in_parent(),
        .clip_depth_direction = camera_struct.add_float("clip_depth_direction")->offset_in_parent(),
        .view_depth_near      = camera_struct.add_float("view_depth_near"     )->offset_in_parent(),
        .view_depth_far       = camera_struct.add_float("view_depth_far"      )->offset_in_parent(),
        .exposure             = camera_struct.add_float("exposure"            )->offset_in_parent()
    }
    , max_camera_count{max_camera_count}
{
    camera_block.add_struct("cameras", &camera_struct, max_camera_count);
}

Camera_buffer::Camera_buffer(const Camera_interface& camera_interface)
    : Multi_buffer      {"camera"}
    , m_camera_interface{camera_interface}
{
    Multi_buffer::allocate(
        gl::Buffer_target::uniform_buffer,
        m_camera_interface.camera_block.binding_point(),
        m_camera_interface.camera_block.size_bytes(),
        m_name
    );
}

auto Camera_buffer::update(
    const erhe::scene::Projection& camera_projection,
    const erhe::scene::Node&       camera_node,
    erhe::scene::Viewport          viewport,
    float                          exposure
) -> erhe::application::Buffer_range
{
    ERHE_PROFILE_FUNCTION

    SPDLOG_LOGGER_TRACE(
        log_render,
        "write_offset = {}",
        m_writer.write_offset
    );

    auto&           buffer           = current_buffer();
    const auto      entry_size       = m_camera_interface.camera_struct.size_bytes();
    const auto&     offsets          = m_camera_interface.offsets;
    const auto      clip_from_camera = camera_projection.clip_from_node_transform(viewport);
    const auto      gpu_data         = buffer.map();
    const glm::mat4 world_from_node  = camera_node.world_from_node();
    const glm::mat4 world_from_clip  = world_from_node * clip_from_camera.inverse_matrix();
    const glm::mat4 clip_from_world  = clip_from_camera.matrix() * camera_node.node_from_world();

    if ((m_writer.write_offset + entry_size) > buffer.capacity_byte_count())
    {
        log_render->critical("camera buffer capacity {} exceeded", buffer.capacity_byte_count());
        ERHE_FATAL("camera buffer capacity exceeded");
    }

    m_writer.begin(buffer.target());
    const float viewport_floats[4] {
        static_cast<float>(viewport.x),
        static_cast<float>(viewport.y),
        static_cast<float>(viewport.width),
        static_cast<float>(viewport.height)
    };
    const auto  fov_sides = camera_projection.get_fov_sides(viewport);
    const float fov_floats[4] {
        fov_sides.left,
        fov_sides.right,
        fov_sides.up,
        fov_sides.down
    };
    const float clip_depth_direction = viewport.reverse_depth ? -1.0f : 1.0f;
    const float view_depth_near      = camera_projection.z_near;
    const float view_depth_far       = camera_projection.z_far;
    using erhe::graphics::as_span;
    using erhe::graphics::write;
    write(gpu_data, m_writer.write_offset + offsets.world_from_node,      as_span(world_from_node     ));
    write(gpu_data, m_writer.write_offset + offsets.world_from_clip,      as_span(world_from_clip     ));
    write(gpu_data, m_writer.write_offset + offsets.clip_from_world,      as_span(clip_from_world     ));
    write(gpu_data, m_writer.write_offset + offsets.viewport,             as_span(viewport_floats     ));
    write(gpu_data, m_writer.write_offset + offsets.fov,                  as_span(fov_floats          ));
    write(gpu_data, m_writer.write_offset + offsets.clip_depth_direction, as_span(clip_depth_direction));
    write(gpu_data, m_writer.write_offset + offsets.view_depth_near,      as_span(view_depth_near     ));
    write(gpu_data, m_writer.write_offset + offsets.view_depth_far,       as_span(view_depth_far      ));
    write(gpu_data, m_writer.write_offset + offsets.exposure,             as_span(exposure            ));
    m_writer.write_offset += entry_size;
    ERHE_VERIFY(m_writer.write_offset <= buffer.capacity_byte_count());
    m_writer.end();

    return m_writer.range;
}


}