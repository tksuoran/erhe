#pragma once

#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/compute_pipeline_state.hpp"
#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace erhe::graphics {
    class Buffer;
    class Compute_command_encoder;
    class Device;
    class Base_render_pipeline;
    class Render_command_encoder;
    class Render_pass;
    class Shader_stages;
}
namespace erhe::primitive {
    class Buffer_mesh;
}
namespace erhe::scene {
    class Camera;
    class Mesh;
    class Node;
}

namespace erhe::scene_renderer {

class Mesh_memory;

// Dispatches one compute per mesh primitive, expanding edge line
// vertex pairs to triangle quads for wide line rendering.
// Used on Metal (and other backends without geometry shaders).
//
// The app provides compute_shader_stages and graphics_shader_stages.
// The compute shader must match the interface defined by this class
// (edge_line_vertex_buffer SSBO, triangle_vertex_buffer SSBO, view UBO).
class Content_wide_line_renderer
{
public:
    // view_count: size N of the cameras[N] array inside the view
    //   UBO block (and the per-view stride of the triangle SSBO).
    //   1 = single-view (default). >= 2 = stereo / OpenXR multiview.
    //   Mirrors Camera_interface::view_count; pass through from the
    //   editor's Program_interface_config.
    Content_wide_line_renderer(
        erhe::graphics::Device&        graphics_device,
        erhe::graphics::Shader_stages* compute_shader_stages,
        erhe::graphics::Shader_stages* graphics_shader_stages,
        int                            view_count = 1
    );
    ~Content_wide_line_renderer() noexcept;

    [[nodiscard]] auto is_enabled() const -> bool;

    // Set shader stages after construction (for two-phase init).
    // multiview_graphics_shader_stages is the multiview-compiled
    // sibling of graphics_shader_stages: it reads the triangle SSBO
    // (instead of vertex attributes) at gl_VertexID + gl_ViewIndex *
    // stride_per_view so a single draw inside a multiview render pass
    // produces correct stereo output. Pass nullptr for single-view-only
    // builds; the multiview render path will then fall back to the
    // single-view shader and only the first view will look correct.
    void set_shader_stages(
        erhe::graphics::Shader_stages* compute_shader_stages,
        erhe::graphics::Shader_stages* graphics_shader_stages,
        erhe::graphics::Shader_stages* multiview_graphics_shader_stages = nullptr
    );

    // Shader resource accessors - app needs these to build shader create_info
    [[nodiscard]] auto get_edge_line_vertex_struct      () const -> erhe::graphics::Shader_resource*;
    [[nodiscard]] auto get_edge_line_vertex_buffer_block() const -> erhe::graphics::Shader_resource*;
    [[nodiscard]] auto get_triangle_vertex_struct       () const -> erhe::graphics::Shader_resource*;
    [[nodiscard]] auto get_triangle_vertex_buffer_block () const -> erhe::graphics::Shader_resource*;
    // Same binding as get_triangle_vertex_buffer_block() but declared
    // with readonly qualifier instead of writeonly. The compute shader
    // uses the writeonly block (cannot be read); the multiview vertex
    // shader uses this read-only block to fetch the per-view triangle
    // it must emit. Different declarations of the same descriptor set
    // binding are legal in Vulkan.
    [[nodiscard]] auto get_triangle_vertex_buffer_read_block() const -> erhe::graphics::Shader_resource*;
    [[nodiscard]] auto get_view_camera_struct           () const -> erhe::graphics::Shader_resource*;
    [[nodiscard]] auto get_view_block                   () const -> erhe::graphics::Shader_resource*;
    [[nodiscard]] auto get_bind_group_layout            () const -> erhe::graphics::Bind_group_layout*;
    // Multiview graphics pipeline layout: triangle SSBO (binding 1,
    // read by vertex stage), view UBO (binding 3, read by vertex
    // stage for stride_per_view), camera UBO (binding 4, read by
    // fragment stage for camera.cameras[c_view_index].viewport.xy).
    // Distinct from the compute layout because the compute side also
    // needs the line-vertex SSBO at binding 0 and writes the triangle
    // SSBO; this graphics-side layout omits the line input and binds
    // the triangle buffer read-only.
    [[nodiscard]] auto get_multiview_graphics_bind_group_layout() const -> erhe::graphics::Bind_group_layout*;
    [[nodiscard]] auto get_fragment_outputs             () -> erhe::graphics::Fragment_outputs&;
    [[nodiscard]] auto get_triangle_vertex_format       () -> erhe::dataformat::Vertex_format&;
    [[nodiscard]] auto get_vertex_input                 () -> erhe::graphics::Vertex_input_state*;
    [[nodiscard]] auto get_graphics_shader_stages       () -> erhe::graphics::Shader_stages*;

    // Per-frame lifecycle
    void begin_frame();

    // Add edge lines from a mesh for compute expansion.
    // group identifies which render pass this mesh belongs to.
    // mesh_memory is used to resolve the edge-line buffer range's
    // (pool_id, buffer_id) into the underlying graphics buffer.
    void add_mesh(
        Mesh_memory&             mesh_memory,
        const erhe::scene::Mesh& mesh,
        const glm::vec4&         color,
        float                    line_width,
        uint32_t                 group = 0
    );

    // Dispatch compute shader to expand lines to triangles (call before
    // render pass). Single-view path: pass camera + viewport, leave
    // multiview_views empty; the compute writes one set of triangles
    // per dispatch. Multiview path: pass a non-empty span of per-view
    // inputs (size must equal view_count from construction) and
    // leave camera nullptr; the compute writes view_count sets of
    // triangles per dispatch, laid out [view][line][vertex] in the
    // triangle SSBO. The viewport argument is shared by all views (the
    // OpenXR multiview path uses the same per-eye recommended extent).
    void compute(
        erhe::graphics::Compute_command_encoder&  command_encoder,
        const erhe::math::Viewport&               viewport,
        const erhe::scene::Camera*                camera,
        std::span<const Camera_view_input>        multiview_views,
        bool                                      reverse_depth,
        erhe::math::Depth_range                   depth_range,
        const erhe::math::Coordinate_conventions& conventions = erhe::math::Coordinate_conventions{}
    );

    // Single-view convenience overload (kept for non-XR callers).
    void compute(
        erhe::graphics::Compute_command_encoder&  command_encoder,
        const erhe::math::Viewport&               viewport,
        const erhe::scene::Camera&                camera,
        bool                                      reverse_depth,
        erhe::math::Depth_range                   depth_range,
        const erhe::math::Coordinate_conventions& conventions = erhe::math::Coordinate_conventions{}
    );

    // Render the expanded triangles for a specific group (call inside
    // render pass). multiview = true binds the multiview-compiled
    // graphics shader (which reads the triangle SSBO via
    // gl_ViewIndex-strided indexing instead of vertex attributes) and
    // issues one draw per dispatch entry; the surrounding multiview
    // render pass distributes the draw across both layers. multiview =
    // false uses the existing vertex-attribute path.
    void render(
        erhe::graphics::Render_command_encoder& render_encoder,
        erhe::graphics::Base_render_pipeline&   pipeline_state,
        const erhe::graphics::Render_pass&      render_pass,
        uint32_t                                group = 0,
        bool                                    multiview = false
    );

    void end_frame();

private:
    struct Dispatch_entry
    {
        // Source GPU buffer holding the edge-line vertex pairs for this
        // mesh. With lazy edge-line pool growth, different meshes may live
        // in different GPU buffers, so each dispatch records its own
        // source buffer pointer rather than reading from a single shared
        // one held by the renderer.
        erhe::graphics::Buffer* edge_buffer            {nullptr};
        std::size_t edge_buffer_byte_offset{0};
        std::size_t edge_count             {0};
        glm::mat4   world_from_node        {1.0f};
        glm::vec4   color                  {1.0f};
        float       line_width             {1.0f};
        uint32_t    group                  {0};

        // Filled by compute(). triangle_buffer_range covers all views:
        // size = view_count * padded_edge_count * 6 * stride. The
        // multiview vertex shader indexes within it as
        //     base_offset + (gl_VertexID + gl_ViewIndex * stride_per_view)
        // and stride_per_view (= padded_edge_count * 6 vertices) is
        // mirrored into the view UBO. view_buffer_range is held across
        // the compute encoder + the render encoder so the multiview
        // vertex shader can read stride_per_view at draw time without
        // a separate push constant; release happens in end_frame()
        // after the GPU is done with the draw.
        std::size_t                       padded_edge_count    {0};
        erhe::graphics::Ring_buffer_range triangle_buffer_range{};
        erhe::graphics::Ring_buffer_range view_buffer_range    {};
        bool                              dispatched{false};
    };

    erhe::graphics::Device&                                m_graphics_device;
    int                                                    m_view_count{1};

    // Shader resources (define the interface contract for shaders)
    erhe::graphics::Fragment_outputs                       m_fragment_outputs;
    std::unique_ptr<erhe::graphics::Shader_resource>       m_edge_line_vertex_struct;
    std::unique_ptr<erhe::graphics::Shader_resource>       m_edge_line_vertex_buffer_block;
    std::unique_ptr<erhe::graphics::Shader_resource>       m_triangle_vertex_struct;
    std::unique_ptr<erhe::graphics::Shader_resource>       m_triangle_vertex_buffer_block;
    std::unique_ptr<erhe::graphics::Shader_resource>       m_triangle_vertex_buffer_read_block;
    std::unique_ptr<erhe::graphics::Shader_resource>       m_view_camera_struct;
    std::unique_ptr<erhe::graphics::Shader_resource>       m_view_block;
    std::unique_ptr<erhe::graphics::Bind_group_layout>     m_bind_group_layout;
    std::unique_ptr<erhe::graphics::Bind_group_layout>     m_multiview_graphics_bind_group_layout;
    erhe::dataformat::Vertex_format                        m_triangle_vertex_format;

    // Offsets inside the per-view ViewCamera struct (same struct
    // repeated view_count times at the start of the view block).
    std::size_t                                            m_view_camera_clip_from_world_offset       {0};
    std::size_t                                            m_view_camera_viewport_offset              {0};
    std::size_t                                            m_view_camera_fov_offset                   {0};
    std::size_t                                            m_view_camera_view_position_in_world_offset{0};
    std::size_t                                            m_view_camera_stride                       {0};

    // Offsets in the outer view block, after the cameras[] array.
    std::size_t                                            m_world_from_node_offset      {0};
    std::size_t                                            m_line_color_offset           {0};
    std::size_t                                            m_edge_count_offset           {0};
    std::size_t                                            m_view_count_offset           {0};
    std::size_t                                            m_stride_per_view_offset      {0};
    std::size_t                                            m_vp_y_sign_offset            {0};
    std::size_t                                            m_clip_depth_direction_offset {0};
    std::size_t                                            m_padding0_offset             {0};
    std::size_t                                            m_padding1_offset             {0};

    // Shader stages provided by app
    erhe::graphics::Shader_stages*                         m_compute_shader_stages          {nullptr};
    erhe::graphics::Shader_stages*                         m_graphics_shader_stages         {nullptr};
    erhe::graphics::Shader_stages*                         m_multiview_graphics_shader_stages{nullptr};
    std::optional<erhe::graphics::Compute_pipeline>         m_compute_pipeline;
    erhe::graphics::Vertex_input_state                     m_vertex_input;

    // Ring buffer clients
    erhe::graphics::Ring_buffer_client                     m_view_buffer;
    erhe::graphics::Ring_buffer_client                     m_triangle_vertex_buffer_client;

    // Per-frame state
    std::vector<Dispatch_entry>                            m_dispatches;
    bool                                                   m_enabled{false};
};

} // namespace erhe::scene_renderer
