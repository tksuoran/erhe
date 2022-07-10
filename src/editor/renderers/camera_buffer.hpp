#pragma once

#include "renderers/multi_buffer.hpp"

#include "erhe/graphics/shader_resource.hpp"

#include <memory>

namespace erhe::scene
{
    class Node;
    class Projection;
    class Viewport;
}

namespace editor
{

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
};

class Camera_interface
{
public:
    explicit Camera_interface(std::size_t max_camera_count);

    erhe::graphics::Shader_resource camera_block;
    erhe::graphics::Shader_resource camera_struct;
    Camera_struct                   offsets{};
    std::size_t                     max_camera_count;
};

class Camera_buffer
    : public Multi_buffer
{
public:
    explicit Camera_buffer(const Camera_interface& camera_interface);

    auto update(
        const erhe::scene::Projection& camera_projection,
        const erhe::scene::Node&       camera_node,
        erhe::scene::Viewport          viewport,
        float                          exposure
    ) -> erhe::application::Buffer_range;

private:
    const Camera_interface& m_camera_interface;
};

} // namespace editor
