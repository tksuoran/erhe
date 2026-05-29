#pragma once

// Internal helper shared by Content_wide_line_compute_renderer and
// Content_wide_line_geometry_renderer. The data laid out by
// write_view_block matches the Content_wide_line_view_offsets that
// Content_wide_line_interface publishes; both backends consume the
// same UBO contents (the compute backend reads cameras[] in compute
// before SSBO write, the geometry backend reads cameras[] and the
// per-mesh fields in the vertex stage).

#include "erhe_graphics/span.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/projection.hpp"
#include "erhe_scene_renderer/content_wide_line_interface.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace erhe::scene_renderer {

class Per_view_camera
{
public:
    glm::mat4 clip_from_world;
    glm::vec4 viewport;
    glm::vec4 fov;
    glm::vec4 view_position_in_world;
};

[[nodiscard]] inline auto build_per_view_camera(
    const erhe::scene::Projection&            projection,
    const erhe::scene::Node&                  node,
    const erhe::math::Viewport&               viewport,
    const bool                                reverse_depth,
    const erhe::math::Depth_range             depth_range,
    const erhe::math::Coordinate_conventions& conventions
) -> Per_view_camera
{
    const erhe::scene::Transform clip_from_camera = projection.clip_from_node_transform(viewport, reverse_depth, depth_range, conventions);
    const glm::mat4              clip_from_world  = clip_from_camera.get_matrix() * node.node_from_world();
    const glm::mat4              world_from_node  = node.world_from_node();
    const glm::vec4              view_position_in_world{world_from_node[3]};
    const erhe::scene::Projection::Fov_sides fov  = projection.get_fov_sides(viewport);
    return Per_view_camera{
        .clip_from_world        = clip_from_world,
        .viewport               = glm::vec4{
            static_cast<float>(viewport.x),
            static_cast<float>(viewport.y),
            static_cast<float>(viewport.width),
            static_cast<float>(viewport.height)
        },
        .fov                    = glm::vec4{fov.left, fov.right, fov.up, fov.down},
        .view_position_in_world = view_position_in_world
    };
}

class Dispatch_per_frame_params
{
public:
    std::span<const Per_view_camera> per_view_cameras;
    uint32_t                         view_count_runtime;
    float                            vp_y_sign;
    float                            clip_depth_direction;
};

inline void write_view_block(
    std::span<std::byte>                  view_data,
    const Content_wide_line_view_offsets& offsets,
    const Dispatch_per_frame_params&      frame_params,
    const glm::mat4&                      world_from_node,
    const glm::vec4&                      color,
    float                                 line_width,
    uint32_t                              edge_count,
    uint32_t                              stride_per_view,
    uint32_t                              base_joint_index
)
{
    using erhe::graphics::as_span;
    using erhe::graphics::write;

    for (std::size_t v = 0; v < frame_params.per_view_cameras.size(); ++v) {
        const std::size_t base = v * offsets.camera_stride;
        write(view_data, base + offsets.clip_from_world,        as_span(frame_params.per_view_cameras[v].clip_from_world       ));
        write(view_data, base + offsets.viewport,               as_span(frame_params.per_view_cameras[v].viewport              ));
        write(view_data, base + offsets.fov,                    as_span(frame_params.per_view_cameras[v].fov                   ));
        write(view_data, base + offsets.view_position_in_world, as_span(frame_params.per_view_cameras[v].view_position_in_world));
    }

    const glm::vec4 line_color_with_width{color.x, color.y, color.z, line_width};
    const float     padding_zero        {0.0f};

    write(view_data, offsets.world_from_node,      as_span(world_from_node                ));
    write(view_data, offsets.line_color,           as_span(line_color_with_width          ));
    write(view_data, offsets.edge_count,           as_span(edge_count                     ));
    write(view_data, offsets.view_count,           as_span(frame_params.view_count_runtime));
    write(view_data, offsets.stride_per_view,      as_span(stride_per_view                ));
    write(view_data, offsets.vp_y_sign,            as_span(frame_params.vp_y_sign         ));
    write(view_data, offsets.clip_depth_direction, as_span(frame_params.clip_depth_direction));
    write(view_data, offsets.base_joint_index,     as_span(base_joint_index               ));
    write(view_data, offsets.padding0,             as_span(padding_zero                   ));
}

} // namespace erhe::scene_renderer
