#pragma once

#include "erhe_graphics/compute_pipeline_state.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace erhe::graphics {
    class Buffer;
    class Color_blend_state;
    class Compute_command_encoder;
    class Device;
    class Base_render_pipeline;
    class Render_command_encoder;
    class Shader_stages;
}
namespace erhe::scene {
    class Camera;
    class Mesh;
}

namespace erhe::scene_renderer {

class Content_wide_line_interface;
class Mesh_memory;

// Dispatches one compute per mesh primitive, expanding edge line vertex
// pairs to triangle quads for wide line rendering. Used on Metal (and
// other backends without geometry shaders).
//
// All shader-interface description (Shader_resources, bind group layouts,
// view UBO offsets, vertex format, fragment outputs) lives on the
// companion Content_wide_line_interface, mirroring the
// Camera_interface+Camera_buffer / Joint_interface+Joint_buffer pattern in
// this same library.
//
// Lifetime:
//  1. App constructs Content_wide_line_interface.
//  2. App compiles compute + graphics shader stages against the interface's
//     public fields.
//  3. App constructs Content_wide_line_renderer with the interface and the
//     compiled shader stages. If shader compilation fails, the runtime
//     renderer is simply not constructed -- there is no internal disabled
//     state to query.
class Content_wide_line_renderer
{
public:
    // multiview_graphics_shader_stages: multiview-compiled sibling of
    //   graphics_shader_stages. It reads the triangle SSBO (instead of
    //   vertex attributes) at gl_VertexID + gl_ViewIndex * stride_per_view
    //   so a single draw inside a multiview render pass produces correct
    //   stereo output. Pass nullptr for single-view-only builds; the
    //   multiview render path will then no-op.
    // compute_shader_stages_skinned: skinned compute variant. Pass nullptr
    //   when the interface was built without a joint_block (skinned path
    //   disabled). When non-null, skinned dispatches transform edges via
    //   the joint matrices read from the global `joint` block.
    Content_wide_line_renderer(
        erhe::graphics::Device&        graphics_device,
        Content_wide_line_interface&   interface_,
        erhe::graphics::Shader_stages* compute_shader_stages,
        erhe::graphics::Shader_stages* compute_shader_stages_skinned,
        erhe::graphics::Shader_stages* graphics_shader_stages,
        erhe::graphics::Shader_stages* multiview_graphics_shader_stages
    );
    ~Content_wide_line_renderer() noexcept;

    // Trivially true: the renderer is only constructed when compute is
    // supported and the required shader stages compiled successfully, so
    // its mere existence implies it is enabled. Kept as a callable so
    // caller sites can stay on `if (renderer && renderer->is_enabled())`.
    [[nodiscard]] auto is_enabled() const -> bool { return true; }

    // Per-frame lifecycle
    void begin_frame();

    // Add edge lines from a mesh for compute expansion. group identifies
    // which render pass this mesh belongs to. mesh_memory resolves the
    // edge-line buffer range to the underlying graphics buffer.
    void add_mesh(
        Mesh_memory&             mesh_memory,
        const erhe::scene::Mesh& mesh,
        const glm::vec4&         color,
        float                    line_width,
        uint32_t                 group = 0
    );

    // Dispatch compute shader to expand lines to triangles (call before
    // render pass). views: one Camera_view_input per eye. Single-view
    // callers pass a 1-element span; multiview callers pass interface.view_count
    // entries. Each entry carries its own viewport; the per-view camera
    // data the compute shader reads is derived from view.projection,
    // view.node and view.viewport.
    //
    // joint_buffer_client / joint_buffer_range: optional pair describing
    //   the per-frame joint UBO/SSBO contents (typically produced by
    //   Joint_buffer::update()). Required when any skinned dispatch is
    //   queued; pass nullptr/nullptr for scenes with no skinned meshes.
    void compute(
        erhe::graphics::Compute_command_encoder&  command_encoder,
        std::span<const Camera_view_input>        views,
        erhe::graphics::Ring_buffer_client*       joint_buffer_client,
        erhe::graphics::Ring_buffer_range*        joint_buffer_range,
        bool                                      reverse_depth,
        erhe::math::Depth_range                   depth_range,
        const erhe::math::Coordinate_conventions& conventions = erhe::math::Coordinate_conventions{}
    );

    // Render the expanded triangles for a specific group (call inside
    // render pass). multiview = true binds the multiview-compiled graphics
    // shader (which reads the triangle SSBO via gl_ViewIndex-strided
    // indexing instead of vertex attributes) and issues one draw per
    // dispatch entry; the surrounding multiview render pass distributes
    // the draw across both layers. multiview = false uses the
    // vertex-attribute path.
    //
    // pipeline_state supplies caller-controlled state (debug_label,
    // input_assembly, multisample, viewport_depth_range, rasterization,
    // depth_stencil); the renderer overrides shader stages, vertex input,
    // and bind group layout per path. color_blend_state overrides
    // pipeline_state's blend. The caller's pipeline_state.data.shader_stages
    // / vertex_input / bind_group_layout are ignored.
    void render(
        erhe::graphics::Render_command_encoder& render_encoder,
        erhe::graphics::Base_render_pipeline&   pipeline_state,
        erhe::graphics::Color_blend_state*      color_blend_state,
        uint32_t                                group = 0,
        bool                                    multiview = false
    );

    void end_frame();

    // Accessors for callers that still need to reference the shader
    // interface or graphics shader stages (e.g. building Base_render_pipeline).
    [[nodiscard]] auto get_interface             () -> Content_wide_line_interface&;
    [[nodiscard]] auto get_graphics_shader_stages() -> erhe::graphics::Shader_stages*;

private:
    class Dispatch_entry
    {
    public:
        // Source GPU buffer holding the edge-line vertex pairs for this
        // mesh. With lazy edge-line pool growth, different meshes may live
        // in different GPU buffers.
        erhe::graphics::Buffer* edge_buffer            {nullptr};
        std::size_t edge_buffer_byte_offset{0};
        std::size_t edge_count             {0};
        glm::mat4   world_from_node        {1.0f};
        glm::vec4   color                  {1.0f};
        float       line_width             {1.0f};
        uint32_t    group                  {0};

        // Skinned dispatch state (skinned == false leaves the joint
        // fields unused). joint_buffer/_byte_offset/_byte_size identify
        // the GPU joint side buffer holding per-endpoint indices +
        // weights for this mesh; base_joint_index is the offset into the
        // global joint UBO/SSBO that maps the mesh's local joint indices
        // to global joint matrices.
        bool                    skinned                 {false};
        erhe::graphics::Buffer* joint_buffer            {nullptr};
        std::size_t             joint_buffer_byte_offset{0};
        std::size_t             joint_buffer_byte_size  {0};
        uint32_t                base_joint_index        {0};

        // Filled by compute(). triangle_buffer_range covers all views:
        // size = view_count * padded_edge_count * 6 * stride. The
        // multiview vertex shader indexes within it as
        //     base_offset + (gl_VertexID + gl_ViewIndex * stride_per_view)
        // and stride_per_view (= padded_edge_count * 6 vertices) is
        // mirrored into the view UBO. view_buffer_range is held across
        // the compute encoder + the render encoder so the multiview
        // vertex shader can read stride_per_view at draw time; release
        // happens in end_frame() after the GPU is done with the draw.
        std::size_t                       padded_edge_count    {0};
        erhe::graphics::Ring_buffer_range triangle_buffer_range{};
        erhe::graphics::Ring_buffer_range view_buffer_range    {};
        bool                              dispatched{false};
    };

    erhe::graphics::Device&            m_graphics_device;
    Content_wide_line_interface&       m_interface;

    erhe::graphics::Shader_stages*     m_compute_shader_stages           {nullptr};
    erhe::graphics::Shader_stages*     m_compute_shader_stages_skinned   {nullptr};
    erhe::graphics::Shader_stages*     m_graphics_shader_stages          {nullptr};
    erhe::graphics::Shader_stages*     m_multiview_graphics_shader_stages{nullptr};

    std::optional<erhe::graphics::Compute_pipeline> m_compute_pipeline;
    std::optional<erhe::graphics::Compute_pipeline> m_compute_pipeline_skinned;

    // Empty Vertex_input_state used by the graphics pipeline. The vertex
    // shader reads from the triangle SSBO via gl_VertexID so no actual
    // attribute bindings are needed, but OpenGL core profile requires a
    // non-default VAO to be bound for glDrawArrays to draw. Passing
    // nullptr to Render_pipeline_data::vertex_input would bind VAO 0
    // and the draw would silently fail.
    erhe::graphics::Vertex_input_state m_empty_vertex_input;

    erhe::graphics::Ring_buffer_client m_view_buffer;
    erhe::graphics::Ring_buffer_client m_triangle_vertex_buffer_client;

    std::vector<Dispatch_entry>        m_dispatches;
};

} // namespace erhe::scene_renderer
