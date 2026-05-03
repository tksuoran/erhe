#pragma once

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_renderer/debug_renderer_bucket.hpp"
#include "erhe_renderer/view.hpp"
#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/compute_pipeline_state.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_math/viewport.hpp"

#include <etl/vector.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <stack>

namespace erhe::graphics {
    class Buffer;
    class Compute_pipeline_state;
    class Render_command_encoder;
    class Render_pass;
    class Shader_stages;
}
namespace erhe::scene {
    class Camera;
    class Transform;
}

namespace erhe::renderer {

class Debug_renderer_program_interface
{
public:
    // max_view_count: size N of the cameras[N] array inside the view UBO.
    // 1 = single-view (default). >= 2 = stereo / OpenXR multiview. The
    // single-view callers always populate cameras[0] only and set
    // view_count = 1 at runtime; multiview callers (added in Phase 2)
    // populate all N entries. Mirrors Content_wide_line_renderer.
    explicit Debug_renderer_program_interface(
        erhe::graphics::Device& graphics_device,
        int                     max_view_count = 1
    );

    bool                                             use_compute{false};
    bool                                             use_geometry_shader{false};
    int                                              max_view_count{1};

    erhe::graphics::Fragment_outputs                 fragment_outputs;

    // Compute path (wide lines): SSBO line vertices → compute shader → triangle vertices
    erhe::dataformat::Vertex_format                  triangle_vertex_format;
    std::unique_ptr<erhe::graphics::Shader_resource> line_vertex_struct;
    std::unique_ptr<erhe::graphics::Shader_resource> line_vertex_buffer_block;
    std::unique_ptr<erhe::graphics::Shader_resource> triangle_vertex_struct;
    std::unique_ptr<erhe::graphics::Shader_resource> triangle_vertex_buffer_block;
    std::unique_ptr<erhe::graphics::Shader_stages>   compute_shader_stages;
    std::unique_ptr<erhe::graphics::Shader_stages>   graphics_shader_stages;

    // Geometry shader path (wide lines without compute): GL_LINES -> geometry shader -> triangle strip
    std::unique_ptr<erhe::graphics::Shader_stages>   geometry_shader_stages;

    // Simple line path (no compute, no geometry shader): vertex buffer → GL_LINES
    erhe::dataformat::Vertex_format                  line_vertex_format;
    std::unique_ptr<erhe::graphics::Shader_stages>   line_shader_stages;

    // View UBO. Layout:
    //   ViewCamera cameras[max_view_count];   // per-eye camera
    //   uint   view_count;                    // 1 single-view, N multiview
    //   uint   stride_per_view;               // triangle vertices per eye
    //   float  vp_y_sign;
    //   float  _padding0;
    std::unique_ptr<erhe::graphics::Shader_resource>   view_camera_struct;
    std::unique_ptr<erhe::graphics::Shader_resource>   view_block;
    std::unique_ptr<erhe::graphics::Bind_group_layout> bind_group_layout;

    // Offsets inside one ViewCamera entry (repeated max_view_count times
    // at the start of view_block).
    std::size_t                                      view_camera_clip_from_world_offset       {0};
    std::size_t                                      view_camera_viewport_offset              {0};
    std::size_t                                      view_camera_fov_offset                   {0};
    std::size_t                                      view_camera_view_position_in_world_offset{0};
    std::size_t                                      view_camera_stride                       {0};

    // Offsets in view_block, after the cameras[] array.
    std::size_t                                      view_count_offset     {0};
    std::size_t                                      stride_per_view_offset{0};
    std::size_t                                      vp_y_sign_offset      {0};
    std::size_t                                      padding0_offset       {0};
};

class Primitive_renderer;
class Debug_renderer_bucket;
class Debug_renderer_config;

class Debug_renderer
{
public:
    // max_view_count threads through to Debug_renderer_program_interface.
    // Default 1 keeps existing single-view callers unchanged; pass >= 2
    // for stereo / OpenXR multiview (added in Phase 2 of the multiview
    // port - see doc/debug_renderer_multiview.md).
    explicit Debug_renderer(
        erhe::graphics::Device& graphics_device,
        int                     max_view_count = 1
    );
    ~Debug_renderer() noexcept;

    // Public API
    auto get        (const Debug_renderer_config& config) -> Primitive_renderer;
    void begin_frame(
        erhe::math::Viewport                      viewport,
        const erhe::scene::Camera&                camera,
        const erhe::math::Coordinate_conventions& conventions = erhe::math::Coordinate_conventions{}
    );
    void compute    (erhe::graphics::Compute_command_encoder& command_encoder);
    void render     (erhe::graphics::Render_command_encoder& encoder, const erhe::graphics::Render_pass& render_pass, erhe::math::Viewport camera_viewport);
    void end_frame  ();

    inline void push_view(const View& view) {
        m_view_stack.push(view);
        m_view = m_view_stack.top();
    }
    inline void pop_view() {
        m_view_stack.pop();
        if (!m_view_stack.empty()){
            m_view = m_view_stack.top();
        } else {
            m_view.clip_from_world = glm::mat4{1.0f};
            m_view.fov_sides       = glm::vec4{-1.0f, 1.0f, 1.0f, -1.0f};
        }
    }
    inline auto get_view() -> const View&
    {
        return m_view;
    }

    // API for Debug_renderer_bucket
    auto get_program_interface() const -> const Debug_renderer_program_interface& { return m_program_interface; }
    auto get_vertex_input     () -> erhe::graphics::Vertex_input_state* { return &m_vertex_input; }
    auto get_line_vertex_input() -> erhe::graphics::Vertex_input_state* { return &m_line_vertex_input; }
    auto use_compute          () const -> bool { return m_program_interface.use_compute; }
    auto use_geometry_shader  () const -> bool { return m_program_interface.use_geometry_shader; }

private:
    erhe::graphics::Device&                               m_graphics_device;
    Debug_renderer_program_interface                      m_program_interface;
    erhe::graphics::Vertex_input_state                    m_vertex_input;      // triangle path
    erhe::graphics::Vertex_input_state                    m_line_vertex_input; // simple line path
    std::optional<erhe::graphics::Compute_pipeline> m_lines_to_triangles_compute_pipeline;
    std::stack<View>                                      m_view_stack{};
    View                                                  m_view      {};

    // NOTE: Elements in m_buckets must be stable, etl::vector<> works, std::vector<> does not work.
    etl::vector<Debug_renderer_bucket, 32> m_buckets;
};

} // namespace erhe::renderer
