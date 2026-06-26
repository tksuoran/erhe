#include "erhe_scene_renderer/content_wide_line_renderer.hpp"
#include "erhe_scene_renderer/content_wide_line_interface.hpp"
#include "erhe_scene_renderer/content_wide_line_view_writer.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"

#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/compute_pipeline_state.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_graphics/scoped_debug_group.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace erhe::scene_renderer {

namespace {

// Compute-shader backend: pre-transforms edge endpoints into a triangle
// SSBO via a compute dispatch in compute(), then draws those triangles
// in render() through a graphics shader that reads the SSBO at
// gl_VertexID (single-view) or gl_VertexID + gl_ViewIndex *
// stride_per_view (multiview).
class Content_wide_line_compute_renderer final : public Content_wide_line_renderer
{
public:
    Content_wide_line_compute_renderer(
        erhe::graphics::Device&        graphics_device,
        Content_wide_line_interface&   interface_,
        erhe::graphics::Shader_stages* compute_shader_stages,
        erhe::graphics::Shader_stages* compute_shader_stages_skinned,
        erhe::graphics::Shader_stages* graphics_shader_stages,
        erhe::graphics::Shader_stages* multiview_graphics_shader_stages
    );

    [[nodiscard]] auto uses_compute() const -> bool override { return true; }
    void compute(erhe::graphics::Compute_command_encoder& command_encoder) override;
    void render (
        erhe::graphics::Render_command_encoder& render_encoder,
        erhe::graphics::Base_render_pipeline&   pipeline_state,
        erhe::graphics::Color_blend_state*      color_blend_state,
        uint32_t                                group,
        bool                                    multiview
    ) override;

protected:
    void add_primitive(
        Mesh_memory&                        mesh_memory,
        const erhe::primitive::Buffer_mesh& buffer_mesh,
        const glm::mat4&                    world_from_node,
        const glm::vec4&                    color,
        float                               line_width,
        uint32_t                            group,
        bool                                mesh_is_skinned,
        uint32_t                            base_joint_index,
        uint32_t                            id_base
    ) override;

    void release_backend_state() override;

private:
    class Dispatch_entry
    {
    public:
        // Source GPU buffer holding the edge-line vertex pairs for this
        // mesh. With lazy edge-line pool growth, different meshes may
        // live in different GPU buffers.
        erhe::graphics::Buffer* edge_buffer            {nullptr};
        std::size_t edge_buffer_byte_offset{0};
        std::size_t edge_count             {0};
        glm::mat4   world_from_node        {1.0f};
        glm::vec4   color                  {1.0f};
        float       line_width             {1.0f};
        uint32_t    group                  {0};
        // Per-primitive face-id base for the id mode (added to each half-quad's
        // facet index before encoding). Zero for the normal color path.
        uint32_t    id_base                {0};

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
        // size = view_count * padded_edge_count * 12 * stride. The
        // multiview vertex shader indexes within it as
        //     base_offset + (gl_VertexID + gl_ViewIndex * stride_per_view)
        // and stride_per_view (= padded_edge_count * 12 vertices) is
        // mirrored into the view UBO. view_buffer_range is held across
        // the compute encoder + the render encoder so the multiview
        // vertex shader can read stride_per_view at draw time; release
        // happens in release_backend_state() after the GPU is done with
        // the draw.
        std::size_t                       padded_edge_count    {0};
        erhe::graphics::Ring_buffer_range triangle_buffer_range{};
        erhe::graphics::Ring_buffer_range view_buffer_range    {};
        bool                              dispatched{false};
    };

    erhe::graphics::Shader_stages* m_compute_shader_stages           {nullptr};
    erhe::graphics::Shader_stages* m_compute_shader_stages_skinned   {nullptr};
    erhe::graphics::Shader_stages* m_graphics_shader_stages          {nullptr};
    erhe::graphics::Shader_stages* m_multiview_graphics_shader_stages{nullptr};

    std::optional<erhe::graphics::Compute_pipeline> m_compute_pipeline;
    std::optional<erhe::graphics::Compute_pipeline> m_compute_pipeline_skinned;

    // Empty Vertex_input_state used by the graphics pipeline. The
    // vertex shader reads from the triangle SSBO via gl_VertexID so no
    // actual attribute bindings are needed, but OpenGL core profile
    // requires a non-default VAO to be bound for glDrawArrays to draw.
    // Passing nullptr to Render_pipeline_data::vertex_input would bind
    // VAO 0 and the draw would silently fail.
    erhe::graphics::Vertex_input_state m_empty_vertex_input;

    erhe::graphics::Ring_buffer_client m_triangle_vertex_buffer_client;

    std::vector<Dispatch_entry>        m_dispatches;
};

Content_wide_line_compute_renderer::Content_wide_line_compute_renderer(
    erhe::graphics::Device&        graphics_device,
    Content_wide_line_interface&   interface_,
    erhe::graphics::Shader_stages* compute_shader_stages,
    erhe::graphics::Shader_stages* compute_shader_stages_skinned,
    erhe::graphics::Shader_stages* graphics_shader_stages,
    erhe::graphics::Shader_stages* multiview_graphics_shader_stages
)
    : Content_wide_line_renderer        {graphics_device, interface_}
    , m_compute_shader_stages           {compute_shader_stages}
    , m_compute_shader_stages_skinned   {compute_shader_stages_skinned}
    , m_graphics_shader_stages          {graphics_shader_stages}
    , m_multiview_graphics_shader_stages{multiview_graphics_shader_stages}
    , m_empty_vertex_input              {graphics_device, erhe::graphics::Vertex_input_state_data{}}
    , m_triangle_vertex_buffer_client{
        graphics_device,
        erhe::graphics::Buffer_target::storage,
        "Content_wide_line_compute_renderer::m_triangle_vertex_buffer",
        interface_.triangle_vertex_buffer_block->get_binding_point()
    }
{
    ERHE_VERIFY(m_compute_shader_stages  != nullptr);
    ERHE_VERIFY(m_graphics_shader_stages != nullptr);
    ERHE_VERIFY(m_compute_shader_stages ->is_valid());
    ERHE_VERIFY(m_graphics_shader_stages->is_valid());

    m_compute_pipeline.emplace(
        m_graphics_device,
        erhe::graphics::Compute_pipeline_data{
            .name              = "compute_before_content_line",
            .shader_stages     = m_compute_shader_stages,
            .bind_group_layout = m_interface.bind_group_layout.get()
        }
    );

    if (
        (m_compute_shader_stages_skinned != nullptr) && m_compute_shader_stages_skinned->is_valid() &&
        (m_interface.skinned_bind_group_layout != nullptr)
    ) {
        m_compute_pipeline_skinned.emplace(
            m_graphics_device,
            erhe::graphics::Compute_pipeline_data{
                .name              = "compute_before_content_line_skinned",
                .shader_stages     = m_compute_shader_stages_skinned,
                .bind_group_layout = m_interface.skinned_bind_group_layout.get()
            }
        );
    }
}

void Content_wide_line_compute_renderer::add_primitive(
    Mesh_memory&                        mesh_memory,
    const erhe::primitive::Buffer_mesh& buffer_mesh,
    const glm::mat4&                    world_from_node,
    const glm::vec4&                    color,
    const float                         line_width,
    const uint32_t                      group,
    const bool                          mesh_is_skinned,
    const uint32_t                      base_joint_index,
    const uint32_t                      id_base
)
{
    const erhe::primitive::Buffer_range& edge_range = buffer_mesh.edge_line_vertex_buffer_range;
    if (edge_range.count == 0) {
        return;
    }

    erhe::graphics::Buffer* edge_buffer = mesh_memory.get_vertex_buffer(edge_range);
    if (edge_buffer == nullptr) {
        return;
    }

    const bool skinned_path_available = m_compute_pipeline_skinned.has_value();
    const bool use_skinned             = skinned_path_available && mesh_is_skinned;

    const std::size_t       edge_count               = edge_range.count / 2;
    erhe::graphics::Buffer* joint_buffer             = nullptr;
    std::size_t             joint_buffer_byte_offset = 0;
    std::size_t             joint_buffer_byte_size   = 0;
    const erhe::primitive::Buffer_range& joint_range = buffer_mesh.edge_line_joint_buffer_range;
    if (use_skinned && (joint_range.count > 0)) {
        joint_buffer = mesh_memory.get_vertex_buffer(joint_range);
        if (joint_buffer != nullptr) {
            joint_buffer_byte_offset = joint_range.byte_offset;
            joint_buffer_byte_size   = joint_range.count * joint_range.element_size;
        }
    }
    const bool dispatch_is_skinned = (joint_buffer != nullptr);

    m_dispatches.push_back(Dispatch_entry{
        .edge_buffer              = edge_buffer,
        .edge_buffer_byte_offset  = edge_range.byte_offset,
        .edge_count               = edge_count,
        .world_from_node          = world_from_node,
        .color                    = color,
        .line_width               = line_width,
        .group                    = group,
        .id_base                  = id_base,
        .skinned                  = dispatch_is_skinned,
        .joint_buffer             = joint_buffer,
        .joint_buffer_byte_offset = joint_buffer_byte_offset,
        .joint_buffer_byte_size   = joint_buffer_byte_size,
        .base_joint_index         = dispatch_is_skinned ? base_joint_index : 0u
    });
}

void Content_wide_line_compute_renderer::compute(erhe::graphics::Compute_command_encoder& command_encoder)
{
    if (m_dispatches.empty()) {
        return;
    }

    erhe::graphics::Scoped_debug_group debug_scope{
        command_encoder.get_command_buffer(),
        "Content_wide_line_compute_renderer::compute"
    };

    ERHE_VERIFY(m_compute_pipeline.has_value());

    erhe::graphics::Ring_buffer_client* joint_buffer_client = get_joint_buffer_client();
    erhe::graphics::Ring_buffer_range&  joint_buffer_range  = get_joint_buffer_range();
    const bool have_joint_data =
        (joint_buffer_client != nullptr) &&
        m_compute_pipeline_skinned.has_value();

    command_encoder.set_bind_group_layout(m_interface.bind_group_layout.get());
    command_encoder.set_compute_pipeline(m_compute_pipeline.value());

    const Dispatch_per_frame_params& frame_params = get_frame_params();

    const std::size_t triangle_vertex_stride = m_interface.triangle_vertex_format.streams.front().stride;
    const std::size_t view_size              = m_interface.view_block.get_size_bytes();

    // Track the previously-bound edge SSBO so consecutive dispatches that
    // share the same source don't re-issue the bind.
    erhe::graphics::Buffer* prev_edge_buffer        = nullptr;
    std::size_t             prev_edge_buffer_offset = 0;
    std::size_t             prev_edge_buffer_size   = 0;

    bool current_skinned = false;

    for (Dispatch_entry& dispatch : m_dispatches) {
        // Demote skinned dispatch to non-skinned when the caller did not
        // provide joint data; edge lines render in bind pose, matching
        // legacy behaviour.
        const bool dispatch_skinned = dispatch.skinned && have_joint_data;

        // Switch pipeline + bind group layout when skinned-ness flips
        // across consecutive dispatches. Vulkan requires the new
        // descriptor set layout to be applied before any further bindings;
        // OpenGL accepts the redundant set without cost. Rebind the joint
        // UBO/SSBO each time we enter the skinned layout because Vulkan
        // invalidates all descriptors when the pipeline layout changes.
        if (dispatch_skinned != current_skinned) {
            if (dispatch_skinned) {
                command_encoder.set_bind_group_layout(m_interface.skinned_bind_group_layout.get());
                command_encoder.set_compute_pipeline(m_compute_pipeline_skinned.value());
                joint_buffer_client->bind(command_encoder, joint_buffer_range);
            } else {
                command_encoder.set_bind_group_layout(m_interface.bind_group_layout.get());
                command_encoder.set_compute_pipeline(m_compute_pipeline.value());
            }
            current_skinned = dispatch_skinned;
            // Layout change invalidates previously-bound edge SSBOs.
            prev_edge_buffer        = nullptr;
            prev_edge_buffer_offset = 0;
            prev_edge_buffer_size   = 0;
        }

        // padded_edge_count = workgroup_count * 32 (= ceil(edge_count/32) * 32).
        // stride_per_view (in vertices) = padded_edge_count * 12 (each edge
        // expands to a 4-triangle tent = 12 vertices).
        const uint32_t    workgroup_count   = static_cast<uint32_t>((dispatch.edge_count + 31) / 32);
        const std::size_t padded_edge_count = static_cast<std::size_t>(workgroup_count) * 32;
        const uint32_t    stride_per_view_v = static_cast<uint32_t>(padded_edge_count * 12);

        erhe::graphics::Ring_buffer_range view_buffer_range = m_view_buffer.acquire(
            erhe::graphics::Ring_buffer_usage::CPU_write,
            view_size
        );
        write_view_block(
            view_buffer_range.get_span(),
            m_interface.offsets,
            frame_params,
            dispatch.world_from_node,
            dispatch.color,
            dispatch.line_width,
            static_cast<uint32_t>(dispatch.edge_count),
            stride_per_view_v,
            dispatch.base_joint_index,
            dispatch.id_base
        );
        view_buffer_range.bytes_written(view_size);
        view_buffer_range.close();
        m_view_buffer.bind(command_encoder, view_buffer_range);
        // Hold the view UBO range so render() (potentially in a later
        // encoder) can re-bind it without re-uploading. Release moves to
        // release_backend_state() / end_frame().
        dispatch.view_buffer_range = std::move(view_buffer_range);

        // Each dispatch carries its own source buffer pointer so meshes
        // whose edge data landed in different lazy-grown pool blocks each
        // get the right SSBO bound. Skip the rebind if the previous
        // dispatch already bound the same (buffer, offset, size) tuple.
        const std::size_t edge_buffer_size = dispatch.edge_count * 2 * sizeof(float) * 8;
        if (
            (dispatch.edge_buffer             != prev_edge_buffer       ) ||
            (dispatch.edge_buffer_byte_offset != prev_edge_buffer_offset) ||
            (edge_buffer_size                 != prev_edge_buffer_size  )
        ) {
            command_encoder.set_buffer(
                erhe::graphics::Buffer_target::storage,
                dispatch.edge_buffer,
                dispatch.edge_buffer_byte_offset,
                edge_buffer_size,
                m_interface.edge_line_vertex_buffer_block->get_binding_point()
            );
            prev_edge_buffer        = dispatch.edge_buffer;
            prev_edge_buffer_offset = dispatch.edge_buffer_byte_offset;
            prev_edge_buffer_size   = edge_buffer_size;
        }

        if (dispatch_skinned) {
            // Joint side-buffer for this mesh's per-endpoint indices +
            // weights. Bound per-dispatch (no de-dup) because there is no
            // expectation that consecutive skinned meshes share a pool block.
            command_encoder.set_buffer(
                erhe::graphics::Buffer_target::storage,
                dispatch.joint_buffer,
                dispatch.joint_buffer_byte_offset,
                dispatch.joint_buffer_byte_size,
                m_interface.edge_line_joint_vertex_buffer_block->get_binding_point()
            );
        }

        // Triangle SSBO holds view_count_runtime contiguous slabs, each
        // sized stride_per_view * vertex_stride. The multiview compute
        // shader writes one slab per loop iteration; the multiview vertex
        // shader indexes within the buffer by gl_VertexID + gl_ViewIndex
        // * stride_per_view.
        const std::size_t per_view_triangle_byte_count = static_cast<std::size_t>(stride_per_view_v) * triangle_vertex_stride;
        const std::size_t triangle_byte_count          = frame_params.view_count_runtime * per_view_triangle_byte_count;
        // See note in joint_buffer.cpp.
        const std::size_t triangle_acquire_byte_count  = std::max(
            triangle_byte_count,
            m_interface.triangle_vertex_buffer_block->get_size_bytes()
        );
        dispatch.padded_edge_count     = padded_edge_count;
        dispatch.triangle_buffer_range = m_triangle_vertex_buffer_client.acquire(
            erhe::graphics::Ring_buffer_usage::GPU_access,
            triangle_acquire_byte_count
        );
        dispatch.triangle_buffer_range.bytes_gpu_used(triangle_byte_count);
        dispatch.triangle_buffer_range.close();
        m_triangle_vertex_buffer_client.bind(command_encoder, dispatch.triangle_buffer_range);

        command_encoder.dispatch_compute(workgroup_count, 1, 1);

        dispatch.dispatched = true;
        // Note: dispatch.view_buffer_range is intentionally held until
        // release_backend_state() so the render encoder can re-bind it
        // (the multiview vertex shader reads stride_per_view from the
        // same UBO).
    }

    // Note: the compute->vertex-attribute memory barrier that pairs with
    // these dispatches is emitted by the caller AFTER the compute encoder
    // scope ends. memory_barrier() inside compute() would be illegal on
    // Metal (cb cannot be split while a compute encoder is open) and
    // unnecessary on Vulkan (the call must happen between encoders, not
    // within one).
}

void Content_wide_line_compute_renderer::render(
    erhe::graphics::Render_command_encoder& render_encoder,
    erhe::graphics::Base_render_pipeline&   pipeline_state,
    erhe::graphics::Color_blend_state*      color_blend_state,
    const uint32_t                          group,
    const bool                              multiview
)
{
    if (m_dispatches.empty()) {
        return;
    }

    // Note: the compute->vertex-attribute memory barrier is emitted by
    // compute() after the last dispatch, before any render pass begins.
    // Emitting it here would be inside the active render pass and
    // therefore invalid on Vulkan.

    erhe::graphics::Scoped_debug_group debug_scope{
        render_encoder.get_command_buffer(),
        "Content_wide_line_compute_renderer::render"
    };

    // Both paths read pre-transformed triangle vertices from the
    // triangle SSBO and the per-eye viewport from the view UBO; the
    // multiview build of the vertex shader picks the per-eye slab via
    // gl_ViewIndex * stride_per_view, the single-view build uses
    // c_view_index = 0. Two compiled shader variants exist only because
    // the multiview shader needs ERHE_MULTIVIEW + the multiview render
    // pass's viewMask on its pipeline; otherwise their bindings are
    // identical and the same bind group layout serves both.
    //
    // Pipeline state is built ad-hoc and bound via
    // set_render_pipeline_state(); the encoder's internal pipeline cache
    // handles VkPipeline reuse. This mirrors the multiview override
    // pattern in debug_renderer_bucket.cpp / forward_renderer.cpp and
    // avoids the need for the caller's Base_render_pipeline to know about
    // the renderer's shader stages or bind group layout.
    erhe::graphics::Shader_stages* shader_stages = multiview
        ? m_multiview_graphics_shader_stages
        : m_graphics_shader_stages;
    if ((shader_stages == nullptr) || !shader_stages->is_valid()) {
        return;
    }

    // Culling is disabled for the wide-line draw (both the tent and simple-quad
    // paths). The compute shader's tent path emits two screen-space half-quads per
    // edge -- one per adjacent face, each on its own face plane -- whose projected
    // winding encodes the placement side, not the face's 3D facing, so hardware
    // back-face culling cannot select front vs back (at a silhouette both half-quads
    // land on the same side with the same winding). The front face is instead selected
    // in the compute shader by an explicit per-face world-space facing test, which
    // collapses the back face's half-quad (see compute_before_content_line.comp).
    // Other rasterization settings (depth clamp, polygon mode) are kept from the caller.
    erhe::graphics::Rasterization_state rasterization = pipeline_state.data.rasterization;
    rasterization.face_cull_enable = false;

    erhe::graphics::Render_pipeline_state temp_state{
        erhe::graphics::Render_pipeline_data{
            .debug_label          = pipeline_state.data.debug_label,
            .shader_stages        = shader_stages,
            // Empty (no attributes) but non-null: OpenGL core profile
            // requires a non-default VAO to be bound for the draw to
            // actually fire. The triangles are read from the SSBO via
            // gl_VertexID, not through the input assembler.
            .vertex_input         = &m_empty_vertex_input,
            // The compute shader expands lines into triangles before the
            // draw; the caller's pipeline_state was built with line
            // topology for the edge-line pass, so override it here.
            .input_assembly       = erhe::graphics::Input_assembly_state::triangle,
            .multisample          = pipeline_state.data.multisample,
            .viewport_depth_range = pipeline_state.data.viewport_depth_range,
            .rasterization        = rasterization,
            .depth_stencil        = pipeline_state.data.depth_stencil,
            .color_blend          = (color_blend_state != nullptr)
                ? *color_blend_state
                : erhe::graphics::Color_blend_state::color_blend_disabled
        }
    };

    render_encoder.set_bind_group_layout(m_interface.graphics_bind_group_layout.get());
    render_encoder.set_render_pipeline_state(temp_state);

    for (const Dispatch_entry& dispatch : m_dispatches) {
        if (!dispatch.dispatched || (dispatch.group != group)) {
            continue;
        }

        // Triangle SSBO at binding 1 (declared read-only in the vertex
        // shader) + view UBO at binding 3. The vertex shader fetches
        // pre-transformed vertices from the SSBO; the fragment shader
        // reads cameras[c_view_index].viewport.xy from the view UBO to
        // convert gl_FragCoord into viewport-relative pixel coords.
        m_triangle_vertex_buffer_client.bind(render_encoder, dispatch.triangle_buffer_range);
        m_view_buffer.bind(render_encoder, dispatch.view_buffer_range);

        render_encoder.draw_primitives(
            erhe::graphics::Primitive_type::triangle,
            0,
            static_cast<uint32_t>(12 * dispatch.edge_count)
        );
    }
}

void Content_wide_line_compute_renderer::release_backend_state()
{
    for (Dispatch_entry& dispatch : m_dispatches) {
        if (dispatch.dispatched) {
            dispatch.triangle_buffer_range.release();
            dispatch.view_buffer_range.release();
        }
    }
    m_dispatches.clear();
}

} // anonymous namespace

auto make_content_wide_line_compute_renderer(
    erhe::graphics::Device&        graphics_device,
    Content_wide_line_interface&   interface_,
    erhe::graphics::Shader_stages* compute_shader_stages,
    erhe::graphics::Shader_stages* compute_shader_stages_skinned,
    erhe::graphics::Shader_stages* graphics_shader_stages,
    erhe::graphics::Shader_stages* multiview_graphics_shader_stages
) -> std::unique_ptr<Content_wide_line_renderer>
{
    if ((compute_shader_stages  == nullptr) || !compute_shader_stages ->is_valid()) {
        return nullptr;
    }
    if ((graphics_shader_stages == nullptr) || !graphics_shader_stages->is_valid()) {
        return nullptr;
    }
    return std::make_unique<Content_wide_line_compute_renderer>(
        graphics_device,
        interface_,
        compute_shader_stages,
        compute_shader_stages_skinned,
        graphics_shader_stages,
        multiview_graphics_shader_stages
    );
}

} // namespace erhe::scene_renderer
