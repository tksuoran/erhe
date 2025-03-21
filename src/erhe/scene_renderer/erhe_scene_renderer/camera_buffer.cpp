// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_renderer/renderer_config.hpp"

#include "erhe_configuration/configuration.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/projection.hpp"
#include "erhe_scene/transform.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::scene_renderer {

Camera_interface::Camera_interface(erhe::graphics::Instance& graphics_instance)
    : camera_block{graphics_instance, "camera", 4, erhe::graphics::Shader_resource::Type::uniform_block}
    , camera_struct{graphics_instance, "Camera"}
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
{
    const auto& ini = erhe::configuration::get_ini_file_section("erhe.ini", "renderer");
    ini.get("max_camera_count", max_camera_count);

    camera_block.add_struct("cameras", &camera_struct, 1);
}

Camera_buffer::Camera_buffer(erhe::graphics::Instance& graphics_instance, Camera_interface& camera_interface)
    : GPU_ring_buffer{
        graphics_instance,
        erhe::renderer::GPU_ring_buffer_create_info{
            .target        = gl::Buffer_target::uniform_buffer,
            .binding_point = camera_interface.camera_block.binding_point(),
            .size          = 64 * camera_interface.max_camera_count * camera_interface.camera_struct.size_bytes(),
            .debug_label   = "Camera_buffer"
        }
    }
    , m_camera_interface{camera_interface}
{
}

auto Camera_buffer::update(
    const erhe::scene::Projection& camera_projection,
    const erhe::scene::Node&       camera_node,
    erhe::math::Viewport           viewport,
    float                          exposure
) -> erhe::renderer::Buffer_range
{
    ERHE_PROFILE_FUNCTION();

    SPDLOG_LOGGER_TRACE(log_render, "write_offset = {}", m_writer.write_offset);

    const auto  entry_size       = m_camera_interface.camera_struct.size_bytes();
    const auto& offsets          = m_camera_interface.offsets;
    const auto  clip_from_camera = camera_projection.clip_from_node_transform(viewport);

    erhe::renderer::Buffer_range buffer_range = open(erhe::renderer::Ring_buffer_usage::CPU_write, entry_size);
    std::span<std::byte>         gpu_data     = buffer_range.get_span();
    size_t                       write_offset = 0;

    const glm::mat4 world_from_node = camera_node.world_from_node();
    const glm::mat4 world_from_clip = world_from_node * clip_from_camera.get_inverse_matrix();
    const glm::mat4 clip_from_world = clip_from_camera.get_matrix() * camera_node.node_from_world();

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
    write(gpu_data, write_offset + offsets.world_from_node,      as_span(world_from_node     ));
    write(gpu_data, write_offset + offsets.world_from_clip,      as_span(world_from_clip     ));
    write(gpu_data, write_offset + offsets.clip_from_world,      as_span(clip_from_world     ));
    write(gpu_data, write_offset + offsets.viewport,             as_span(viewport_floats     ));
    write(gpu_data, write_offset + offsets.fov,                  as_span(fov_floats          ));
    write(gpu_data, write_offset + offsets.clip_depth_direction, as_span(clip_depth_direction));
    write(gpu_data, write_offset + offsets.view_depth_near,      as_span(view_depth_near     ));
    write(gpu_data, write_offset + offsets.view_depth_far,       as_span(view_depth_far      ));
    write(gpu_data, write_offset + offsets.exposure,             as_span(exposure            ));
    write_offset += entry_size;
    buffer_range.close(write_offset);

    return buffer_range;
}

} // namespace erhe::scene_renderer
