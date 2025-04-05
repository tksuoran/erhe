#pragma once

#include "erhe_renderer/gpu_ring_buffer.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_math/viewport.hpp"

namespace erhe::graphics {
    class Instance;
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
};

class Camera_interface
{
public:
    explicit Camera_interface(erhe::graphics::Instance& graphics_instance);

    erhe::graphics::Shader_resource camera_block;
    erhe::graphics::Shader_resource camera_struct;
    Camera_struct                   offsets{};
    std::size_t                     max_camera_count;
};

class Camera_buffer : public erhe::renderer::GPU_ring_buffer
{
public:
    Camera_buffer(erhe::graphics::Instance& graphics_instance, Camera_interface& camera_interface);

    auto update(
        const erhe::scene::Projection& camera_projection,
        const erhe::scene::Node&       camera_node,
        erhe::math::Viewport           viewport,
        float                          exposure,
        glm::vec4                      grid_size,
        glm::vec4                      grid_line_width
    ) -> erhe::renderer::Buffer_range;

private:
    Camera_interface& m_camera_interface;
};

} // namespace erhe::scene_renderer
