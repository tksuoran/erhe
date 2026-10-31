#pragma once

#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_scene_renderer/content_wide_line_feed.hpp"
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
    class Sampler;
    class Shader_stages;
    class Texture;
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

// Tent wide-line method controls, passed per frame to
// Content_wide_line_renderer::set_view_params(). use_tent selects between the
// simple single-plane quad (false) and the two-face surface tent (true); both
// are kept because each wins in different cases. line_bias_margin is the
// tent's surface-line depth-bias headroom in depth-buffer resolvable units
// (ULPs). line_bias_clamp is the max toward-camera face-plane extrapolation
// per tent corner (ULPs); it bounds show-through where thin geometry overlaps
// in screen space. Defaults: tent off (the simple quad is the no-regression
// default), a 1024-ULP bias headroom matching Debug_renderer's default, and a
// 2048-ULP clamp.
class Content_wide_line_tent_settings
{
public:
    bool  use_tent        {false};
    float line_bias_margin{1024.0f};
    float line_bias_clamp {2048.0f};
};

// Abstract owner of the content wide-line rendering path. The editor
// constructs the compute implementation via
// make_content_wide_line_compute_renderer below; callers see only this
// base class.
//
// Frame protocol:
//   renderer.begin_frame();
//   renderer.set_view_params(views, tent_settings, reverse_depth, depth_range, conventions);
//   renderer.set_joint_buffer(client, std::move(range));    // optional
//   renderer.add_feed(feed);   // prepared by prepare_mesh(), or add_mesh() per mesh
//   renderer.compute(compute_encoder);
//   renderer.render (render_encoder, pipeline_state, blend, group, multiview);
//   renderer.end_frame();
//
// The view UBO contents and the cached joint buffer are owned by the
// base: one set of view-block writes and one joint-range lifetime.
// end_frame() releases the cached joint range and any per-dispatch
// ranges stashed by the subclass.
class Content_wide_line_renderer
{
public:
    virtual ~Content_wide_line_renderer() noexcept;

    // Kept callable so existing call sites `if (renderer &&
    // renderer->is_enabled())` stay valid. The factory returns
    // nullptr on construction failure, so a live pointer means enabled.
    [[nodiscard]] auto is_enabled() const -> bool { return true; }

    void begin_frame();

    // Cache the per-frame view + depth conventions and the tent settings of
    // the frame (the caller passes the render phase's copy of its settings).
    // Eagerly builds the Per_view_camera array consumed by the subclass's
    // write_view_block calls. Call once per frame between begin_frame() and
    // the first add_mesh() / compute() / render().
    void set_view_params(
        std::span<const Camera_view_input>        views,
        const Content_wide_line_tent_settings&    tent_settings,
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

    // Prepare phase (main thread, or wherever the mesh is owned): appends
    // the records of this mesh's edge-line primitives to out - the edge-line
    // ranges of the level the mesh's fill draws, world_from_node, the skin's
    // joint slot - and counts the mesh. lod_level is the level the caller's
    // fill draws the mesh with (doc/plans/selection_outline_lod.md): for a
    // level above 0, a primitive whose drawn variant (the one the draw lists
    // resolve, Primitive::get_resolved_renderable_mesh() for the optimized
    // variant) has that LOD level, clamped to its chain like the draw list
    // entries, with edge lines, feeds that level's edge lines; otherwise the
    // original variant's. group partitions dispatches across composition
    // passes (selection outline, selected, not_selected, ...).
    static void prepare_mesh(
        Mesh_memory&             mesh_memory,
        const erhe::scene::Mesh& mesh,
        std::size_t              lod_level,
        const glm::vec4&         color,
        float                    line_width,
        uint32_t                 group,
        Content_wide_line_feed&  out
    );

    // Render phase: queue the dispatches of a prepared feed. Reads no Mesh.
    void add_feed(const Content_wide_line_feed& feed);

    // prepare_mesh() and add_feed() of one mesh on the calling thread: the
    // headset, which feeds where it renders.
    void add_mesh(
        Mesh_memory&             mesh_memory,
        const erhe::scene::Mesh& mesh,
        std::size_t              lod_level,
        const glm::vec4&         color,
        float                    line_width,
        uint32_t                 group
    );

    // Run the compute pre-pass that pre-transforms edge endpoints into
    // a triangle SSBO. Call AFTER add_mesh() / set_joint_buffer() and
    // BEFORE any render pass that calls render().
    virtual void compute(erhe::graphics::Compute_command_encoder& command_encoder) = 0;

    // Issue draw calls for all queued dispatches matching group.
    // pipeline_state supplies caller-controlled state (debug_label,
    // input_assembly, multisample, viewport_depth_range, rasterization,
    // depth_stencil); the renderer overrides shader stages, vertex
    // input, and bind group layout itself. color_blend_state
    // overrides pipeline_state's blend. The caller's
    // pipeline_state.data.shader_stages, vertex_input, and
    // bind_group_layout are ignored.
    virtual void render(
        erhe::graphics::Render_command_encoder& render_encoder,
        erhe::graphics::Base_render_pipeline&   pipeline_state,
        erhe::graphics::Color_blend_state*      color_blend_state,
        uint32_t                                group        = 0,
        bool                                    multiview    = false
    ) = 0;

    void end_frame();

    [[nodiscard]] auto get_interface() -> Content_wide_line_interface&;

protected:
    Content_wide_line_renderer(
        erhe::graphics::Device&      graphics_device,
        Content_wide_line_interface& interface_
    );

    // Subclass entry point for per-primitive dispatch construction from a
    // prepared record; the subclass decides whether the dispatch takes the
    // skinned path.
    virtual void add_record(const Content_wide_line_record& record) = 0;

    // Subclass hook for end_frame() -- release any per-dispatch ring
    // buffer ranges and clear dispatch queues. Called after the base
    // releases its own joint range.
    virtual void release_backend_state() = 0;

    // Shared per-frame state accessible to subclasses.
    [[nodiscard]] auto get_frame_params       () const -> const Dispatch_per_frame_params&;
    [[nodiscard]] auto get_joint_buffer_client()       -> erhe::graphics::Ring_buffer_client*;
    [[nodiscard]] auto get_joint_buffer_range ()       -> erhe::graphics::Ring_buffer_range&;

    erhe::graphics::Device&            m_graphics_device;
    Content_wide_line_interface&       m_interface;

    // Shared view UBO ring buffer client. The subclass acquires per-
    // dispatch view-block ranges from this client and holds the ranges
    // alive until end_frame().
    erhe::graphics::Ring_buffer_client m_view_buffer;

private:
    std::vector<Per_view_camera>       m_per_view_cameras;
    Dispatch_per_frame_params          m_frame_params{};
    bool                               m_view_params_set{false};

    erhe::graphics::Ring_buffer_client* m_joint_buffer_client{nullptr};
    erhe::graphics::Ring_buffer_range   m_joint_buffer_range;
    bool                                m_joint_buffer_set{false};
};

// Factory for the compute implementation (the only one): a compute
// pre-pass expands edge lines into an SSBO of triangles that the
// graphics stages then draw. Returns nullptr if any of the supplied
// shader stages is null or not valid.
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

} // namespace erhe::scene_renderer
