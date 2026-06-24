// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_scene_renderer/buffer_binding_points.hpp"

#include "erhe_graphics/span.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/projection.hpp"
#include "erhe_scene/transform.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"
#include "erhe_verify/verify.hpp"

#include <cmath>
#include <cstring>

namespace erhe::scene_renderer {

Camera_interface::Camera_interface(erhe::graphics::Device& graphics_device, const int max_camera_count, const int view_count)
    : camera_block{graphics_device, "camera", camera_buffer_binding_point, erhe::graphics::Shader_resource::Type::uniform_block}
    , camera_struct{graphics_device, "Camera"}
    , offsets{
        .world_from_node          = camera_struct.add_mat4 ("world_from_node"         )->get_offset_in_parent(),
        .world_from_clip          = camera_struct.add_mat4 ("world_from_clip"         )->get_offset_in_parent(),
        .clip_from_world          = camera_struct.add_mat4 ("clip_from_world"         )->get_offset_in_parent(),
        .world_from_node_for_grid = camera_struct.add_mat4 ("world_from_node_for_grid")->get_offset_in_parent(),
        .world_from_clip_for_grid = camera_struct.add_mat4 ("world_from_clip_for_grid")->get_offset_in_parent(),
        .clip_from_world_for_grid = camera_struct.add_mat4 ("clip_from_world_for_grid")->get_offset_in_parent(),
        .world_from_grid          = camera_struct.add_mat4 ("world_from_grid"         )->get_offset_in_parent(),
        .viewport                 = camera_struct.add_vec4 ("viewport"                )->get_offset_in_parent(),
        .fov                      = camera_struct.add_vec4 ("fov"                     )->get_offset_in_parent(),
        .clip_depth_direction     = camera_struct.add_float("clip_depth_direction"    )->get_offset_in_parent(),
        .view_depth_near          = camera_struct.add_float("view_depth_near"         )->get_offset_in_parent(),
        .view_depth_far           = camera_struct.add_float("view_depth_far"          )->get_offset_in_parent(),
        .exposure                 = camera_struct.add_float("exposure"                )->get_offset_in_parent(),
        .grid_size                = camera_struct.add_vec4 ("grid_size"               )->get_offset_in_parent(),
        .grid_line_width          = camera_struct.add_vec4 ("grid_line_width"         )->get_offset_in_parent(),
        .grid_label               = camera_struct.add_vec4 ("grid_label"              )->get_offset_in_parent(),
        .grid_color               = camera_struct.add_vec4 ("grid_color", 4           )->get_offset_in_parent(),
        .grid_label_color         = camera_struct.add_vec4 ("grid_label_color"        )->get_offset_in_parent(),
        .grid_offset              = camera_struct.add_vec4 ("grid_offset"             )->get_offset_in_parent(),
        .grid_view_position       = camera_struct.add_vec4 ("grid_view_position"      )->get_offset_in_parent(),
        .sky_checker              = camera_struct.add_vec4 ("sky_checker"             )->get_offset_in_parent(),
        .sky_horizon_color        = camera_struct.add_vec4 ("sky_horizon_color"       )->get_offset_in_parent(),
        .sky_zenith_color         = camera_struct.add_vec4 ("sky_zenith_color"        )->get_offset_in_parent(),
        .ground_horizon_color     = camera_struct.add_vec4 ("ground_horizon_color"    )->get_offset_in_parent(),
        .ground_nadir_color       = camera_struct.add_vec4 ("ground_nadir_color"      )->get_offset_in_parent(),
        .sun_direction            = camera_struct.add_vec4 ("sun_direction"           )->get_offset_in_parent(),
        .atmosphere               = camera_struct.add_vec4 ("atmosphere"              )->get_offset_in_parent(),
        .frame_number             = camera_struct.add_uvec2("frame_number"            )->get_offset_in_parent(),
        .edge_id_texture          = camera_struct.add_uvec2("edge_id_texture"         )->get_offset_in_parent(),
        .edge_line_color          = camera_struct.add_vec4 ("edge_line_color"         )->get_offset_in_parent(),
    }
    , max_camera_count{static_cast<std::size_t>(max_camera_count)}
    , view_count{static_cast<std::size_t>(view_count)}
{
    ERHE_VERIFY(view_count >= 1);
    camera_block.add_struct("cameras", &camera_struct, static_cast<std::size_t>(view_count));
}

Camera_buffer::Camera_buffer(erhe::graphics::Device& graphics_device, Camera_interface& camera_interface)
    : Ring_buffer_client{
        graphics_device,
        erhe::graphics::Buffer_target::uniform,
        "Camera_buffer",
        camera_interface.camera_block.get_binding_point()
    }
    , m_camera_interface{camera_interface}
{
}

namespace {

void write_camera_entry(
    std::span<std::byte>                      gpu_data,
    std::size_t                               write_offset,
    const Camera_struct&                      offsets,
    const erhe::scene::Projection&            camera_projection,
    const erhe::scene::Trs_transform&         world_from_camera,
    erhe::math::Viewport                      viewport,
    float                                     exposure,
    const Grid_parameters&                    grid_parameters,
    const Sky_parameters&                     sky_parameters,
    uint64_t                                  frame_number,
    const bool                                reverse_depth,
    const erhe::math::Depth_range             depth_range,
    const erhe::math::Coordinate_conventions& conventions,
    const Edge_lines_parameters&              edge_lines_parameters
)
{
    const glm::mat4&              world_from_camera_node     = world_from_camera.get_matrix();
    const glm::mat4&              camera_node_from_world     = world_from_camera.get_inverse_matrix();
    const erhe::scene::Transform& clip_from_camera_transform = camera_projection.clip_from_node_transform(viewport, reverse_depth, depth_range, conventions);
    const glm::mat4               world_from_clip            = world_from_camera_node * clip_from_camera_transform.get_inverse_matrix();
    const glm::mat4               clip_from_world            = clip_from_camera_transform.get_matrix() * camera_node_from_world;
    const float     viewport_floats[4] {
        static_cast<float>(viewport.x),
        static_cast<float>(viewport.y),
        static_cast<float>(viewport.width),
        static_cast<float>(viewport.height)
    };
    const auto      fov_sides            = camera_projection.get_fov_sides(viewport);
    const float     fov_floats[4]        { fov_sides.left, fov_sides.right, fov_sides.up, fov_sides.down };
    const float     clip_depth_direction = reverse_depth ? -1.0f : 1.0f;
    const float     view_depth_near      = camera_projection.z_near;
    const float     view_depth_far       = camera_projection.z_far;

    const glm::mat4 grid_from_world       = glm::mat4{1.0f}; // TODO
    const glm::mat4 world_from_grid       = glm::mat4{1.0f}; // TODO
    const glm::vec4 camera_in_world       = glm::vec4{world_from_camera.get_translation(), 1.0f};
    const glm::vec4 camera_in_grid        = grid_from_world * camera_in_world;
    const double    level0_cell_size      = static_cast<double>(grid_parameters.grid_size.x);
    const double    grid_offset_x_in_grid = level0_cell_size * std::round(static_cast<double>(camera_in_grid.x) / level0_cell_size);
    //const double    grid_offset_y_in_grid = level0_cell_size * std::floor(static_cast<double>(camera_in_grid.y) / level0_cell_size);
    const double    grid_offset_z_in_grid = level0_cell_size * std::round(static_cast<double>(camera_in_grid.z) / level0_cell_size);
    const glm::vec3 grid_offset_in_world  = world_from_grid * glm::vec4{static_cast<float>(grid_offset_x_in_grid), 0.0f, static_cast<float>(grid_offset_z_in_grid), 0.0f};
    const erhe::scene::Trs_transform world_from_camera_node_transform_for_grid{
        world_from_camera.get_translation() - grid_offset_in_world,
        world_from_camera.get_rotation()
    };
    const glm::vec3 grid_view_position              = world_from_camera_node_transform_for_grid.get_translation();
    const glm::mat4 world_from_node_matrix_for_grid = world_from_camera_node_transform_for_grid.get_matrix();
    const glm::mat4 world_from_clip_matrix_for_grid = world_from_node_matrix_for_grid * clip_from_camera_transform.get_inverse_matrix();
    const glm::mat4 clip_from_world_for_grid        = glm::inverse(world_from_clip_matrix_for_grid);

    using erhe::graphics::as_span;
    using erhe::graphics::write;
    write(gpu_data, write_offset + offsets.world_from_node,          as_span(world_from_camera_node         ));
    write(gpu_data, write_offset + offsets.world_from_clip,          as_span(world_from_clip                ));
    write(gpu_data, write_offset + offsets.clip_from_world,          as_span(clip_from_world                ));
    write(gpu_data, write_offset + offsets.world_from_node_for_grid, as_span(world_from_node_matrix_for_grid));
    write(gpu_data, write_offset + offsets.world_from_clip_for_grid, as_span(world_from_clip_matrix_for_grid));
    write(gpu_data, write_offset + offsets.clip_from_world_for_grid, as_span(clip_from_world_for_grid       ));
    write(gpu_data, write_offset + offsets.world_from_grid,          as_span(world_from_grid                ));
    write(gpu_data, write_offset + offsets.viewport,                 as_span(viewport_floats                ));
    write(gpu_data, write_offset + offsets.fov,                      as_span(fov_floats                     ));
    write(gpu_data, write_offset + offsets.clip_depth_direction,     as_span(clip_depth_direction           ));
    write(gpu_data, write_offset + offsets.view_depth_near,          as_span(view_depth_near                ));
    write(gpu_data, write_offset + offsets.view_depth_far,           as_span(view_depth_far                 ));
    write(gpu_data, write_offset + offsets.exposure,                 as_span(exposure                       ));
    write(gpu_data, write_offset + offsets.grid_size,                as_span(grid_parameters.grid_size      ));
    write(gpu_data, write_offset + offsets.grid_line_width,          as_span(grid_parameters.grid_line_width));
    write(gpu_data, write_offset + offsets.grid_label,               as_span(grid_parameters.grid_label     ));
    for (std::size_t i = 0; i < grid_parameters.grid_color.size(); ++i) {
        // std140 vec4 array: 16 byte element stride
        write(gpu_data, write_offset + offsets.grid_color + (i * sizeof(glm::vec4)), as_span(grid_parameters.grid_color[i]));
    }
    write(gpu_data, write_offset + offsets.grid_label_color,     as_span(grid_parameters.grid_label_color    ));
    write(gpu_data, write_offset + offsets.grid_offset,          as_span(grid_offset_in_world                ));
    write(gpu_data, write_offset + offsets.grid_view_position,   as_span(grid_view_position                  ));
    write(gpu_data, write_offset + offsets.sky_checker,          as_span(sky_parameters.sky_checker          ));
    write(gpu_data, write_offset + offsets.sky_horizon_color,    as_span(sky_parameters.sky_horizon_color    ));
    write(gpu_data, write_offset + offsets.sky_zenith_color,     as_span(sky_parameters.sky_zenith_color     ));
    write(gpu_data, write_offset + offsets.ground_horizon_color, as_span(sky_parameters.ground_horizon_color ));
    write(gpu_data, write_offset + offsets.ground_nadir_color,   as_span(sky_parameters.ground_nadir_color   ));
    write(gpu_data, write_offset + offsets.sun_direction,        as_span(sky_parameters.sun_direction        ));
    write(gpu_data, write_offset + offsets.atmosphere,           as_span(sky_parameters.atmosphere           ));
    write(gpu_data, write_offset + offsets.frame_number,         as_span(frame_number                        ));
    write(gpu_data, write_offset + offsets.edge_id_texture,      as_span(edge_lines_parameters.edge_id_texture_handle));
    write(gpu_data, write_offset + offsets.edge_line_color,      as_span(edge_lines_parameters.edge_line_color));
}

} // anonymous namespace

auto Camera_buffer::update(
    const erhe::scene::Projection&            camera_projection,
    const erhe::scene::Node&                  camera_node,
    erhe::math::Viewport                      viewport,
    float                                     exposure,
    const Grid_parameters&                    grid_parameters,
    const Sky_parameters&                     sky_parameters,
    uint64_t                                  frame_number,
    const bool                                reverse_depth,
    const erhe::math::Depth_range             depth_range,
    const erhe::math::Coordinate_conventions& conventions,
    const Edge_lines_parameters&              edge_lines_parameters
) -> erhe::graphics::Ring_buffer_range
{
    ERHE_PROFILE_FUNCTION();

    SPDLOG_LOGGER_TRACE(log_render, "write_offset = {}", m_writer.write_offset);

    const std::size_t entry_size  = m_camera_interface.camera_struct.get_size_bytes();
    // Bind the full block (cameras[view_count]) so the shader can read
    // any cameras[c_view_index] within range. Ring-buffer storage is
    // recycled, so trailing entries past index 0 must be explicitly zeroed.
    const std::size_t block_size  = entry_size * m_camera_interface.view_count;
    const auto&       offsets     = m_camera_interface.offsets;

    erhe::graphics::Ring_buffer_range buffer_range = acquire(erhe::graphics::Ring_buffer_usage::CPU_write, block_size);
    std::span<std::byte>              gpu_data     = buffer_range.get_span();

    write_camera_entry(
        gpu_data, 0, offsets,
        camera_projection, camera_node.world_from_node_transform(),
        viewport,
        exposure, grid_parameters, sky_parameters, frame_number,
        reverse_depth, depth_range, conventions, edge_lines_parameters
    );
    if (block_size > entry_size) {
        std::memset(gpu_data.data() + entry_size, 0, block_size - entry_size);
    }

    buffer_range.bytes_written(block_size);
    buffer_range.close();

    return buffer_range;
}

auto Camera_buffer::update(
    const erhe::scene::Projection&            camera_projection,
    const erhe::scene::Trs_transform&         world_from_camera,
    erhe::math::Viewport                      viewport,
    float                                     exposure,
    const Grid_parameters&                    grid_parameters,
    const Sky_parameters&                     sky_parameters,
    uint64_t                                  frame_number,
    const bool                                reverse_depth,
    const erhe::math::Depth_range             depth_range,
    const erhe::math::Coordinate_conventions& conventions,
    const Edge_lines_parameters&              edge_lines_parameters
) -> erhe::graphics::Ring_buffer_range
{
    ERHE_PROFILE_FUNCTION();

    const std::size_t entry_size = m_camera_interface.camera_struct.get_size_bytes();
    const std::size_t block_size = entry_size * m_camera_interface.view_count;
    const auto&       offsets    = m_camera_interface.offsets;

    erhe::graphics::Ring_buffer_range buffer_range = acquire(erhe::graphics::Ring_buffer_usage::CPU_write, block_size);
    std::span<std::byte>              gpu_data     = buffer_range.get_span();

    write_camera_entry(
        gpu_data, 0, offsets,
        camera_projection,
        world_from_camera,
        viewport,
        exposure, grid_parameters, sky_parameters, frame_number,
        reverse_depth, depth_range, conventions, edge_lines_parameters
    );
    if (block_size > entry_size) {
        std::memset(gpu_data.data() + entry_size, 0, block_size - entry_size);
    }

    buffer_range.bytes_written(block_size);
    buffer_range.close();

    return buffer_range;
}

auto Camera_buffer::update_views(
    std::span<const Camera_view_input>        views,
    float                                     exposure,
    const Grid_parameters&                    grid_parameters,
    const Sky_parameters&                     sky_parameters,
    uint64_t                                  frame_number,
    const bool                                reverse_depth,
    const erhe::math::Depth_range             depth_range,
    const erhe::math::Coordinate_conventions& conventions,
    const Edge_lines_parameters&              edge_lines_parameters
) -> erhe::graphics::Ring_buffer_range
{
    ERHE_PROFILE_FUNCTION();

    // The number of input views must match the cameras[N] array size
    // declared in the UBO block; otherwise shader reads of
    // cameras[c_view_index] would alias entries we never wrote.
    ERHE_VERIFY(views.size() == m_camera_interface.view_count);

    const std::size_t entry_size = m_camera_interface.camera_struct.get_size_bytes();
    const std::size_t block_size = entry_size * m_camera_interface.view_count;
    const auto&       offsets    = m_camera_interface.offsets;

    erhe::graphics::Ring_buffer_range buffer_range = acquire(erhe::graphics::Ring_buffer_usage::CPU_write, block_size);
    std::span<std::byte>              gpu_data     = buffer_range.get_span();

    std::size_t write_offset = 0;
    for (const Camera_view_input& view : views) {
        ERHE_VERIFY(view.projection != nullptr);
        ERHE_VERIFY(view.node != nullptr);
        write_camera_entry(
            gpu_data, write_offset, offsets,
            *view.projection,
            view.node->world_from_node_transform(),
            view.viewport,
            exposure, grid_parameters, sky_parameters, frame_number,
            reverse_depth, depth_range, conventions, edge_lines_parameters
        );
        write_offset += entry_size;
    }

    buffer_range.bytes_written(block_size);
    buffer_range.close();

    return buffer_range;
}

} // namespace erhe::scene_renderer
