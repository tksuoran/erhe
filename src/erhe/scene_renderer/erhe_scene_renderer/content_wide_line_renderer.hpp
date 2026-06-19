#pragma once

#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_scene_renderer/content_wide_line_view_writer.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace erhe::graphics {
    class Base_render_pipeline;
    class Color_blend_state;
    class Compute_command_encoder;
    class Device;
    class Render_command_encoder;
    class Shader_stages;
}
namespace erhe::primitive {
    class Buffer_mesh;
}
namespace erhe::scene {
    class Mesh;
}

namespace erhe::scene_renderer {

class Content_wide_line_interface;
class Mesh_memory;

// Abstract owner of the content wide-line rendering path. The editor
// constructs exactly one concrete subclass per device via a factory
// (see make_content_wide_line_compute_renderer /
// make_content_wide_line_geometry_renderer below). Callers see only
// this base class -- the choice of backend is encoded in which factory
// was called.
//
// Frame protocol (both backends):
//   renderer.begin_frame();
//   renderer.set_view_params(views, reverse_depth, depth_range, conventions);
//   renderer.set_joint_buffer(client, std::move(range));    // optional
//   for each mesh: renderer.add_mesh(mesh_memory, mesh, color, line_width, group);
//   renderer.compute(compute_encoder);                       // geom backend = no-op
//   renderer.render (render_encoder, pipeline_state, blend, group, multiview);
//   renderer.end_frame();
//
// The view UBO contents and the cached joint buffer are owned by the
// base; both backends share one set of view-block writes and one
// joint-range lifetime. end_frame() releases the cached joint range
// and any per-dispatch ranges stashed by the subclass.
class Content_wide_line_renderer
{
public:
    virtual ~Content_wide_line_renderer() noexcept;

    // Kept callable so existing call sites `if (renderer &&
    // renderer->is_enabled())` stay valid. Both factories return
    // nullptr on construction failure, so a live pointer means enabled.
    [[nodiscard]] auto is_enabled() const -> bool { return true; }

    // Tent wide-line method controls (compute backend only). use_tent selects
    // between the simple single-plane quad (false) and the two-face surface
    // tent (true) at runtime; both are kept because each wins in different
    // cases. line_bias_margin is the tent's surface-line depth-bias headroom in
    // depth-buffer resolvable units (ULPs).
    void               set_use_tent        (bool value)   { m_use_tent = value; }
    [[nodiscard]] auto get_use_tent        () const -> bool  { return m_use_tent; }
    void               set_line_bias_margin(float margin)  { m_line_bias_margin = margin; }
    [[nodiscard]] auto get_line_bias_margin() const -> float { return m_line_bias_margin; }
    // Max toward-camera face-plane extrapolation per tent corner (ULPs); bounds
    // show-through where thin geometry overlaps in screen space.
    void               set_line_bias_clamp (float ulps)   { m_line_bias_clamp = ulps; }
    [[nodiscard]] auto get_line_bias_clamp () const -> float { return m_line_bias_clamp; }

    // True for the compute backend (compute() runs a real dispatch and
    // callers must emit a compute->vertex memory barrier afterwards),
    // false for the geometry-shader backend (compute() is a no-op, there
    // is no compute->vertex hazard, and the device may not even expose
    // glMemoryBarrier -- e.g. OpenGL 4.1 on macOS).
    [[nodiscard]] virtual auto uses_compute() const -> bool = 0;

    void begin_frame();

    // Cache the per-frame view + depth conventions. Eagerly builds the
    // Per_view_camera array consumed by both backends' write_view_block
    // calls. Call once per frame between begin_frame() and the first
    // add_mesh() / compute() / render().
    void set_view_params(
        std::span<const Camera_view_input>        views,
        bool                                      reverse_depth,
        erhe::math::Depth_range                   depth_range,
        const erhe::math::Coordinate_conventions& conventions = erhe::math::Coordinate_conventions{}
    );

    // Take ownership of the per-frame joint UBO range. Pass an empty
    // (default-constructed) range and a null client when no skinned
    // meshes will be queued. The range is released in end_frame().
    void set_joint_buffer(
        erhe::graphics::Ring_buffer_client* joint_buffer_client,
        erhe::graphics::Ring_buffer_range&& joint_buffer_range
    );

    // Queue this mesh's edge-line primitives for rendering. group
    // partitions dispatches across composition passes (selection
    // outline, selected, not_selected, ...). Subclass picks which
    // primitives carry usable edge data for its backend.
    void add_mesh(
        Mesh_memory&             mesh_memory,
        const erhe::scene::Mesh& mesh,
        const glm::vec4&         color,
        float                    line_width,
        uint32_t                 group = 0
    );

    // Run the compute pre-pass that pre-transforms edge endpoints into
    // a triangle SSBO. No-op on the geometry-shader backend (the
    // geometry shader expands lines inside the render encoder
    // instead). Call AFTER add_mesh() / set_joint_buffer() and BEFORE
    // any render pass that calls render().
    virtual void compute(erhe::graphics::Compute_command_encoder& command_encoder) = 0;

    // Issue draw calls for all queued dispatches matching group.
    // pipeline_state supplies caller-controlled state (debug_label,
    // input_assembly, multisample, viewport_depth_range, rasterization,
    // depth_stencil); the renderer overrides shader stages, vertex
    // input, and bind group layout per backend. color_blend_state
    // overrides pipeline_state's blend. The caller's
    // pipeline_state.data.shader_stages, vertex_input, and
    // bind_group_layout are ignored.
    virtual void render(
        erhe::graphics::Render_command_encoder& render_encoder,
        erhe::graphics::Base_render_pipeline&   pipeline_state,
        erhe::graphics::Color_blend_state*      color_blend_state,
        uint32_t                                group     = 0,
        bool                                    multiview = false
    ) = 0;

    void end_frame();

    [[nodiscard]] auto get_interface() -> Content_wide_line_interface&;

protected:
    Content_wide_line_renderer(
        erhe::graphics::Device&      graphics_device,
        Content_wide_line_interface& interface_
    );

    // Subclass entry point for per-mesh-primitive dispatch construction.
    // The base's add_mesh() has already verified the mesh has a node,
    // computed world_from_node, and resolved the skin's base_joint_index.
    // The subclass inspects buffer_mesh for the backend-specific
    // edge-data ranges and either pushes a backend dispatch or skips.
    virtual void add_primitive(
        Mesh_memory&                        mesh_memory,
        const erhe::primitive::Buffer_mesh& buffer_mesh,
        const glm::mat4&                    world_from_node,
        const glm::vec4&                    color,
        float                               line_width,
        uint32_t                            group,
        bool                                mesh_is_skinned,
        uint32_t                            base_joint_index
    ) = 0;

    // Subclass hook for end_frame() -- release any per-dispatch ring
    // buffer ranges and clear backend dispatch queues. Called after the
    // base releases its own joint range.
    virtual void release_backend_state() = 0;

    // Shared per-frame state accessible to subclasses.
    [[nodiscard]] auto get_frame_params       () const -> const Dispatch_per_frame_params&;
    [[nodiscard]] auto get_joint_buffer_client()       -> erhe::graphics::Ring_buffer_client*;
    [[nodiscard]] auto get_joint_buffer_range ()       -> erhe::graphics::Ring_buffer_range&;

    erhe::graphics::Device&            m_graphics_device;
    Content_wide_line_interface&       m_interface;

    // Shared view UBO ring buffer client. Each backend acquires per-
    // dispatch view-block ranges from this client; the subclass holds
    // the ranges alive until end_frame().
    erhe::graphics::Ring_buffer_client m_view_buffer;

private:
    std::vector<Per_view_camera>       m_per_view_cameras;
    Dispatch_per_frame_params          m_frame_params{};
    bool                               m_view_params_set{false};

    // Tent wide-line method state (compute backend). Defaults: tent off (the
    // simple single-plane quad is the no-regression default; the tent is opt-in
    // via the Visual Style toggle), a 1024-ULP bias headroom matching
    // Debug_renderer's default, and a 2048-ULP toward-camera extrapolation clamp
    // (anti show-through on overlapping thin geometry) used when the tent is on.
    bool                               m_use_tent{false};
    float                              m_line_bias_margin{1024.0f};
    float                              m_line_bias_clamp{2048.0f};

    erhe::graphics::Ring_buffer_client* m_joint_buffer_client{nullptr};
    erhe::graphics::Ring_buffer_range   m_joint_buffer_range;
    bool                                m_joint_buffer_set{false};
};

// Factory for the compute backend (SSBO expansion path used when the
// device supports compute shaders). Returns nullptr if any of the
// supplied shader stages is null or not valid.
//
// compute_shader_stages_skinned may be null when the interface was
// built without joint_block (skinned path disabled); non-skinned
// dispatches still work in that case.
//
// multiview_graphics_shader_stages may be null for single-view-only
// builds; the multiview render path will then no-op.
auto make_content_wide_line_compute_renderer(
    erhe::graphics::Device&        graphics_device,
    Content_wide_line_interface&   interface_,
    erhe::graphics::Shader_stages* compute_shader_stages,
    erhe::graphics::Shader_stages* compute_shader_stages_skinned,
    erhe::graphics::Shader_stages* graphics_shader_stages,
    erhe::graphics::Shader_stages* multiview_graphics_shader_stages
) -> std::unique_ptr<Content_wide_line_renderer>;

// Factory for the geometry-shader backend (used when the device does
// not support compute shaders). Returns nullptr if either of the
// supplied shader stages is null or not valid.
//
// Both shader stage sets render edge lines via the same geometry
// shader; they differ only in vertex-format defines: the not-skinned
// variant is compiled against vertex_format_not_skinned, the skinned
// variant against vertex_format_skinned (so the skinning branch in the
// vertex shader is enabled). The renderer picks per dispatch based on
// whether the queued mesh has a skin.
auto make_content_wide_line_geometry_renderer(
    erhe::graphics::Device&        graphics_device,
    Content_wide_line_interface&   interface_,
    erhe::graphics::Shader_stages* geometry_shader_stages_not_skinned,
    erhe::graphics::Shader_stages* geometry_shader_stages_skinned
) -> std::unique_ptr<Content_wide_line_renderer>;

} // namespace erhe::scene_renderer
