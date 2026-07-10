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
#include <span>
#include <stack>
#include <vector>

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
    // view_count: size N of the cameras[N] array inside the view UBO.
    // 1 = single-view (default). >= 2 = stereo / OpenXR multiview.
    // Single-view callers populate cameras[0] only and set view_count
    // = 1 at runtime; multiview callers populate all N entries.
    // Mirrors Content_wide_line_renderer; see
    // doc/debug_renderer_multiview.md for the full layout.
    explicit Debug_renderer_program_interface(
        erhe::graphics::Device& graphics_device,
        int                     view_count = 1
    );

    bool                                             use_compute{false};
    bool                                             use_geometry_shader{false};
    int                                              view_count{1};

    erhe::graphics::Fragment_outputs                 fragment_outputs;

    // Compute path (wide lines): SSBO line vertices → compute shader → triangle vertices
    erhe::dataformat::Vertex_format                  triangle_vertex_format;
    std::unique_ptr<erhe::graphics::Shader_resource> line_vertex_struct;
    std::unique_ptr<erhe::graphics::Shader_resource> line_vertex_buffer_block;
    std::unique_ptr<erhe::graphics::Shader_resource> triangle_vertex_struct;
    std::unique_ptr<erhe::graphics::Shader_resource> triangle_vertex_buffer_block;

    // Read-only declaration of the same triangle SSBO binding for the
    // multiview vertex shader (which fetches its triangle data from
    // here instead of via the input assembler). Different .name to
    // avoid duplicate-symbol errors but mapped to the same descriptor
    // set binding so a single buffer bind serves both the writeonly
    // compute output declaration and this readonly input declaration.
    std::unique_ptr<erhe::graphics::Shader_resource> triangle_vertex_buffer_read_block;

    std::unique_ptr<erhe::graphics::Shader_stages>   compute_shader_stages;
    std::unique_ptr<erhe::graphics::Shader_stages>   graphics_shader_stages;

    // Multiview graphics stage: line_after_compute.{vert,frag}
    // recompiled with ERHE_MULTIVIEW so c_view_index resolves to
    // gl_ViewIndex (graphics_shader_stages uses c_view_index = 0).
    // Built only when view_count >= 2; both variants read pre-transformed
    // triangles from the triangle SSBO + per-eye viewport from the view
    // UBO -- they differ only in the ERHE_MULTIVIEW define and the
    // multiview render pass's viewMask.
    //
    // No multiview compute stage: compute_before_line.comp is already
    // view-count agnostic (loops over view.view_count, indexes
    // view.cameras[v]) so a single compiled compute program serves
    // both paths; the C++ side just writes view_count = 1 vs N.
    std::unique_ptr<erhe::graphics::Shader_stages>   multiview_graphics_shader_stages;

    // Geometry shader path (wide lines without compute): GL_LINES -> geometry shader -> triangle strip
    std::unique_ptr<erhe::graphics::Shader_stages>   geometry_shader_stages;

    // Simple line path (no compute, no geometry shader): vertex buffer → GL_LINES
    erhe::dataformat::Vertex_format                  line_vertex_format;
    std::unique_ptr<erhe::graphics::Shader_stages>   line_shader_stages;

    erhe::graphics::Color_blend_state                color_blend_visible;
    // Dim constant-factor blend for the hidden (occluded) pass; buckets with
    // Debug_renderer_config::xray use color_blend_visible for that pass instead.
    erhe::graphics::Color_blend_state                color_blend_hidden;

    // View UBO. Layout:
    //   ViewCamera cameras[view_count];   // per-eye camera
    //   uint   view_count;                    // 1 single-view, N multiview
    //   uint   stride_per_view;               // triangle vertices per eye
    //   float  vp_y_sign;
    //   float  _padding0;
    std::unique_ptr<erhe::graphics::Shader_resource>   view_camera_struct;
    std::unique_ptr<erhe::graphics::Shader_resource>   view_block;
    std::unique_ptr<erhe::graphics::Bind_group_layout> bind_group_layout;

    // Graphics-pipeline layout used by both single-view and multiview
    // compute-path render. Triangle SSBO bound here read-only (vertex
    // stage indexes it via gl_VertexID + c_view_index * stride_per_view).
    // View UBO bound at binding 3 for both vertex (stride_per_view) and
    // fragment (per-eye viewport.xy) reads. This layout omits the
    // line-input SSBO at binding 0 because the vertex shader reads
    // pre-transformed triangles directly from binding 1, never touching
    // the original line vertices.
    std::unique_ptr<erhe::graphics::Bind_group_layout> graphics_bind_group_layout;

    // Offsets inside one ViewCamera entry (repeated view_count times
    // at the start of view_block).
    std::size_t                                      view_camera_clip_from_world_offset       {0};
    std::size_t                                      view_camera_viewport_offset              {0};
    std::size_t                                      view_camera_fov_offset                   {0};
    std::size_t                                      view_camera_view_position_in_world_offset{0};
    std::size_t                                      view_camera_stride                       {0};

    // Offsets in view_block, after the cameras[] array.
    std::size_t                                      view_count_offset          {0};
    std::size_t                                      stride_per_view_offset     {0};
    std::size_t                                      vp_y_sign_offset           {0};
    // +1.0 forward depth, -1.0 reverse depth; used by the line shaders to push
    // a surface-aligned line toward the viewer (normal-derived bias).
    std::size_t                                      clip_depth_direction_offset{0};
    // Surface-line bias headroom in depth resolvable units (ULPs).
    std::size_t                                      line_bias_margin_offset    {0};
    // d(window)/d(ndc) factor for the depth-range convention: 1.0 for
    // zero_to_one, 2.0 for minus_one_to_one. Lets the line shaders map an
    // NDC-space bias to/from the window-depth space the ULP is measured in.
    std::size_t                                      window_to_ndc_scale_offset {0};
};

class Primitive_renderer;
class Debug_renderer_bucket;
class Debug_renderer_config;

class Debug_renderer
{
public:
    // view_count threads through to Debug_renderer_program_interface.
    // Default 1 keeps single-view callers unchanged; pass >= 2 for
    // stereo / OpenXR multiview. See doc/debug_renderer_multiview.md.
    explicit Debug_renderer(
        erhe::graphics::Device& graphics_device,
        int                     view_count = 1
    );
    ~Debug_renderer() noexcept;

    // Surface-line depth bias headroom in depth-buffer resolvable units
    // (ULPs). The line shaders push a surface-aligned line toward the viewer
    // by margin * ulp(depth) * tilt, where ulp(depth) is the float depth
    // buffer's resolvable step at the fragment (so the bias is derived from
    // the depth precision and reverse-Z distribution, not a magic length) and
    // tilt is a clamped surface-slope factor. This margin is the only knob,
    // unitless; written to the view UBO each frame and tunable live.
    void               set_line_bias_margin(float margin) { m_line_bias_margin = margin; }
    [[nodiscard]] auto get_line_bias_margin() const -> float { return m_line_bias_margin; }

    // Public API
    auto get        (const Debug_renderer_config& config) -> Primitive_renderer;
    // Begin a debug-renderer frame. `views` size must equal view_count
    // from construction; single-view callers pass a 1-element span built
    // via view_from_camera(). The shared `viewport` matches the
    // (multiview) swapchain extent (same (w, h) for every eye);
    // per-eye projection differences are captured in each View's
    // clip_from_world.
    void begin_frame(
        erhe::math::Viewport  viewport,
        std::span<const View> views
    );

    // Helper: derive a single View from a Camera, viewport, and depth
    // conventions. Callers that drive Debug_renderer with a Camera (no
    // explicit per-eye View setup) use this to build the 1-element span
    // for begin_frame().
    [[nodiscard]] static auto view_from_camera(
        const erhe::scene::Camera&                camera,
        erhe::math::Viewport                      viewport,
        const erhe::math::Coordinate_conventions& conventions = erhe::math::Coordinate_conventions{}
    ) -> View;
    void compute    (erhe::graphics::Compute_command_encoder& command_encoder);
    void render     (
        erhe::graphics::Render_command_encoder& encoder,
        const erhe::graphics::Render_pass&      render_pass,
        erhe::math::Viewport                    camera_viewport,
        bool                                    multiview = false
    );
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
    auto get_line_vertex_input() -> erhe::graphics::Vertex_input_state* { return &m_line_vertex_input; }
    // Empty (no attributes) vertex input state for SSBO-read triangle
    // draws. OpenGL core profile requires a non-default VAO bound for
    // the draw to fire, even when the vertex shader reads its data via
    // gl_VertexID instead of input attributes.
    auto get_empty_vertex_input() -> erhe::graphics::Vertex_input_state* { return &m_empty_vertex_input; }
    auto use_compute          () const -> bool { return m_program_interface.use_compute; }
    auto use_geometry_shader  () const -> bool { return m_program_interface.use_geometry_shader; }
    // True only when there are at least two views; matches the
    // >= 2 gate that Debug_renderer_bucket::dispatch_compute uses
    // to decide between single-view and multiview pipelines. A
    // single-view caller (`begin_frame` overload taking one View)
    // should appear as is_multiview_active() == false.
    auto is_multiview_active  () const -> bool { return m_multiview_views.size() >= 2; }
    auto get_multiview_views  () const -> std::span<const View> { return m_multiview_views; }

private:
    erhe::graphics::Device&                         m_graphics_device;
    Debug_renderer_program_interface                m_program_interface;
    erhe::graphics::Vertex_input_state              m_line_vertex_input;  // simple line path
    erhe::graphics::Vertex_input_state              m_empty_vertex_input; // SSBO-read triangle path
    std::optional<erhe::graphics::Compute_pipeline> m_lines_to_triangles_compute_pipeline;
    std::stack<View>                                m_view_stack{};
    View                                            m_view      {};
    float                                           m_line_bias_margin{1024.0f};

    // Multiview state, parallel to m_view_stack / m_view. Non-empty
    // when a multiview begin_frame() supplied a per-eye View span;
    // emptied at end_frame(). Buckets check is_multiview_active() to
    // decide which UBO layout to write per draw.
    std::vector<View>                               m_multiview_views{};

    // NOTE: Elements in m_buckets must be stable, etl::vector<> works, std::vector<> does not work.
    etl::vector<Debug_renderer_bucket, 32>          m_buckets;
};

} // namespace erhe::renderer
