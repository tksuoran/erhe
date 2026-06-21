#pragma once

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_math/viewport.hpp"

#include <array>
#include <span>

namespace erhe::graphics { class Device; }
namespace erhe::scene {
    class Node;
    class Projection;
    class Trs_transform;
}

namespace erhe::scene_renderer {

class Camera_struct
{
public:
    std::size_t world_from_node;          // mat4
    std::size_t world_from_clip;          // mat4
    std::size_t clip_from_world;          // mat4
    std::size_t world_from_node_for_grid; // mat4
    std::size_t world_from_clip_for_grid; // mat4
    std::size_t clip_from_world_for_grid; // mat4
    std::size_t world_from_grid;          // mat4
    std::size_t viewport;                 // vec4
    std::size_t fov;                      // vec4
    std::size_t clip_depth_direction;     // float 1.0 = forward depth, -1.0 = reverse depth
    std::size_t view_depth_near;          // float
    std::size_t view_depth_far;           // float
    std::size_t exposure;                 // float
    std::size_t grid_size;                // vec4
    std::size_t grid_line_width;          // vec4
    std::size_t grid_label;               // vec4 x = enable, y = text height fraction, z = label spacing, w = fade threshold (pixels per em)
    std::size_t grid_color;               // vec4[4] per-LOD-level line color (rgb, a = opacity)
    std::size_t grid_label_color;         // vec4 axis label color (rgb, a = opacity)
    // World-space translation snapped to the level-0 grid (xyz, y = 0,
    // w unused). The grid pass runs all line math in wrapped world space
    // (world - grid_offset) so coordinates stay small near the camera
    // (fp32 jitter fix far from origin): grid.vert adds the offset to
    // its wrapped-space plane geometry to get world positions for
    // gl_Position, grid.frag adds it back when computing axis label
    // world coordinates. A multiple of the level-0 cell size keeps
    // every LOD level's line pattern invariant under the shift
    // (level k cell size = level0 / div^k).
    std::size_t grid_offset;          // vec4
    // Camera position in wrapped world space (xyz = camera position
    // minus grid_offset, w unused), computed in double on the CPU so
    // the grid shader gets a small, exact ray origin without doing the
    // large-magnitude subtraction itself.
    std::size_t grid_view_position;   // vec4
    std::size_t sky_checker;          // vec4 x, y = checker frequency, z = intensity a, w = intensity b
    std::size_t sky_horizon_color;    // vec4 rgb, w = horizon-to-zenith falloff power
    std::size_t sky_zenith_color;     // vec4 rgb, w unused
    std::size_t ground_horizon_color; // vec4 rgb, w = horizon-to-nadir falloff power
    std::size_t ground_nadir_color;   // vec4 rgb, w unused
    // Atmosphere (Hillaire) sky mode. Read by sky_atmosphere.frag; ignored by
    // the gradient sky and other passes.
    std::size_t sun_direction;        // vec4 xyz = world dir toward sun, w = sun illuminance
    std::size_t atmosphere;           // vec4 x = march steps, y = observer altitude (km), z = cos(sun angular radius), w = sun disc brightness
    std::size_t frame_number;         // uvec2
    std::size_t padding;              // uvec2
};

// Grid rendering parameters written to the camera UBO; read by the
// editor's grid composition pass fragment shader (grid.frag). Other
// passes ignore these fields, so renderers that do not draw the grid
// can pass a default-constructed instance.
class Grid_parameters
{
public:
    // Cell size for each of the 4 grid LOD levels, in world units.
    glm::vec4                grid_size      {10.0f,   1.0f,  0.1f,  0.01f};
    // Line width for each of the 4 grid LOD levels.
    glm::vec4                grid_line_width{ 0.006f, 0.02f, 0.02f, 0.02f};
    // Axis coordinate labels: x = enable, y = text height as a fraction
    // of label spacing, z = label spacing in world units, w = fade
    // threshold: glyph size in pixels per em at which labels are fully
    // visible (fade starts at half of it; smaller = visible further).
    glm::vec4                grid_label     { 1.0f,   0.15f, 1.0f,  4.0f};
    // Per-LOD-level line colors (rgb, a = line opacity). Defaults match
    // the historical hardcoded grid shader tints.
    std::array<glm::vec4, 4> grid_color{
        glm::vec4{0.0f,  0.0f,  0.01f, 1.0f},
        glm::vec4{0.0f,  0.0f,  0.0f,  1.0f},
        glm::vec4{0.01f, 0.0f,  0.0f,  1.0f},
        glm::vec4{0.0f,  0.01f, 0.0f,  1.0f}
    };
    // Axis label color (rgb, a = opacity).
    glm::vec4                grid_label_color{0.0f, 0.0f, 0.0f, 1.0f};
};

// Sky rendering parameters written to the camera UBO; read by the
// editor's sky composition pass fragment shader (sky.frag). Other
// passes ignore these fields, so renderers that do not draw the sky
// can pass a default-constructed instance. Defaults match the
// historical hardcoded sky shader values.
class Sky_parameters
{
public:
    // x, y = checkerboard frequency in heading and elevation,
    // z = checker intensity a, w = checker intensity b.
    glm::vec4 sky_checker         {18.0f, 18.0f, 0.92f, 1.0f};
    // rgb = color at the horizon, w = horizon-to-zenith falloff power.
    glm::vec4 sky_horizon_color   { 0.3f,  0.3f, 0.33f, 10.0f};
    glm::vec4 sky_zenith_color    { 0.2f,  0.2f, 0.22f,  0.0f};
    // rgb = color at the horizon, w = horizon-to-nadir falloff power.
    glm::vec4 ground_horizon_color{ 0.2f,  0.2f, 0.2f,   8.0f};
    glm::vec4 ground_nadir_color  { 0.1f,  0.1f, 0.1f,   0.0f};
    // Atmosphere (Hillaire) sky mode parameters. xyz = world-space direction
    // toward the sun, w = sun illuminance. Read by sky_atmosphere.frag only;
    // the gradient sky ignores these. Defaults give a mid-morning sun.
    glm::vec4 sun_direction       { 0.0f,  0.7071f, 0.7071f, 20.0f};
    // x = ray-march step count, y = observer altitude above sea level (km),
    // z = cos(sun angular radius), w = sun disc brightness multiplier.
    glm::vec4 atmosphere          {32.0f,  0.5f,    0.99996f, 30.0f};
};

class Camera_interface
{
public:
    // max_camera_count: per-frame cap on the number of distinct cameras
    //   recorded into the ring buffer (size of the underlying allocation).
    // view_count:   size N of the cameras[N] array inside the UBO
    //   block. 1 = single-view (default). 2 = stereo / OpenXR multiview.
    //   The shader reads camera.cameras[c_view_index]; c_view_index is 0
    //   for non-multiview shaders and gl_ViewIndex (from
    //   GL_EXT_multiview / SPV_KHR_multiview) for multiview ones.
    Camera_interface(erhe::graphics::Device& graphics_device, int max_camera_count, int view_count);

    erhe::graphics::Shader_resource camera_block;
    erhe::graphics::Shader_resource camera_struct;
    Camera_struct                   offsets{};
    std::size_t                     max_camera_count;
    std::size_t                     view_count;
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
        const Grid_parameters&                    grid_parameters,
        const Sky_parameters&                     sky_parameters,
        uint64_t                                  frame_number,
        bool                                      reverse_depth,
        erhe::math::Depth_range                   depth_range,
        const erhe::math::Coordinate_conventions& conventions = erhe::math::Coordinate_conventions{}
    ) -> erhe::graphics::Ring_buffer_range;

    // Overload taking a precomputed world<->camera Transform instead of
    // a Node. Used by the shadow pass, where the directional-light
    // "camera" is the snapped pose from Light_projection_transforms
    // (see Light::stable_directional_light_projection_transforms): the
    // shadow map is rasterized using that snapped pose, and the shading
    // pass samples it via the matching snapped texture_from_world in
    // light_buffer.cpp -- both sides must derive clip_from_world from
    // the SAME snapped transform, not from the raw light node.
    auto update(
        const erhe::scene::Projection&            camera_projection,
        const erhe::scene::Trs_transform&         world_from_camera,
        erhe::math::Viewport                      viewport,
        float                                     exposure,
        const Grid_parameters&                    grid_parameters,
        const Sky_parameters&                     sky_parameters,
        uint64_t                                  frame_number,
        bool                                      reverse_depth,
        erhe::math::Depth_range                   depth_range,
        const erhe::math::Coordinate_conventions& conventions = erhe::math::Coordinate_conventions{}
    ) -> erhe::graphics::Ring_buffer_range;

    // Multi-view variant: writes one Camera_struct entry per view into
    // the cameras[] array. The number of views must equal
    // Camera_interface::view_count (the array size declared in the
    // UBO block); shaders read camera.cameras[u_view_index]. Used for
    // single-pass stereo / OpenXR multiview rendering.
    auto update_views(
        std::span<const Camera_view_input>        views,
        float                                     exposure,
        const Grid_parameters&                    grid_parameters,
        const Sky_parameters&                     sky_parameters,
        uint64_t                                  frame_number,
        bool                                      reverse_depth,
        erhe::math::Depth_range                   depth_range,
        const erhe::math::Coordinate_conventions& conventions = erhe::math::Coordinate_conventions{}
    ) -> erhe::graphics::Ring_buffer_range;

private:
    Camera_interface& m_camera_interface;
};

} // namespace erhe::scene_renderer
