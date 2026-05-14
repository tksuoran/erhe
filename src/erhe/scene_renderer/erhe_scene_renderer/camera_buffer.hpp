#pragma once

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_math/viewport.hpp"

#include <span>

namespace erhe::graphics {
    class Device;
}
namespace erhe::scene {
    class Node;
    class Projection;
}

namespace erhe::scene_renderer {

class Camera_struct
{
public:
    std::size_t world_from_node;      // mat4
    std::size_t world_from_clip;      // mat4
    std::size_t clip_from_world;      // mat4
    std::size_t viewport;             // vec4
    std::size_t fov;                  // vec4
    std::size_t clip_depth_direction; // float 1.0 = forward depth, -1.0 = reverse depth
    std::size_t view_depth_near;      // float
    std::size_t view_depth_far;       // float
    std::size_t exposure;             // float
    std::size_t grid_size;            // vec4
    std::size_t grid_line_width;      // vec4
    std::size_t frame_number;         // uvec2
    std::size_t padding;              // uvec2
};

class Camera_interface
{
public:
    // max_camera_count: per-frame cap on the number of distinct cameras
    //   recorded into the ring buffer (size of the underlying allocation).
    // max_view_count:   size N of the cameras[N] array inside the UBO
    //   block. 1 = single-view (default). 2 = stereo / OpenXR multiview.
    //   The shader reads camera.cameras[c_view_index]; c_view_index is 0
    //   for non-multiview shaders and gl_ViewIndex (from
    //   GL_EXT_multiview / SPV_KHR_multiview) for multiview ones.
    Camera_interface(erhe::graphics::Device& graphics_device, int max_camera_count, int max_view_count);

    erhe::graphics::Shader_resource camera_block;
    erhe::graphics::Shader_resource camera_struct;
    Camera_struct                   offsets{};
    std::size_t                     max_camera_count;
    std::size_t                     max_view_count;
};

class Camera_view_input
{
public:
    const erhe::scene::Projection* projection {nullptr};
    const erhe::scene::Node*       node       {nullptr};
    erhe::math::Viewport           viewport   {};
};

class Camera_buffer : public erhe::graphics::Ring_buffer_client
{
public:
    Camera_buffer(erhe::graphics::Device& graphics_device, Camera_interface& camera_interface);

    auto update(
        const erhe::scene::Projection&            camera_projection,
        const erhe::scene::Node&                  camera_node,
        erhe::math::Viewport                      viewport,
        float                                     exposure,
        glm::vec4                                 grid_size,
        glm::vec4                                 grid_line_width,
        uint64_t                                  frame_number,
        bool                                      reverse_depth,
        erhe::math::Depth_range                   depth_range,
        const erhe::math::Coordinate_conventions& conventions = erhe::math::Coordinate_conventions{}
    ) -> erhe::graphics::Ring_buffer_range;

    // Multi-view variant: writes one Camera_struct entry per view into
    // the cameras[] array. The number of views must equal
    // Camera_interface::max_view_count (the array size declared in the
    // UBO block); shaders read camera.cameras[u_view_index]. Used for
    // single-pass stereo / OpenXR multiview rendering.
    auto update_views(
        std::span<const Camera_view_input>        views,
        float                                     exposure,
        glm::vec4                                 grid_size,
        glm::vec4                                 grid_line_width,
        uint64_t                                  frame_number,
        bool                                      reverse_depth,
        erhe::math::Depth_range                   depth_range,
        const erhe::math::Coordinate_conventions& conventions = erhe::math::Coordinate_conventions{}
    ) -> erhe::graphics::Ring_buffer_range;

private:
    Camera_interface& m_camera_interface;
};

} // namespace erhe::scene_renderer
