#include "erhe_scene_renderer/content_wide_line_renderer.hpp"
#include "erhe_scene_renderer/content_wide_line_interface.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"

#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_graphics/scoped_debug_group.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/span.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/projection.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/gtc/type_ptr.hpp>

namespace erhe::scene_renderer {

namespace {

class Per_view_camera
{
public:
    glm::mat4 clip_from_world;
    glm::vec4 viewport;
    glm::vec4 fov;
    glm::vec4 view_position_in_world;
};

[[nodiscard]] auto build_per_view_camera(
    const erhe::scene::Projection&            projection,
    const erhe::scene::Node&                  node,
    const erhe::math::Viewport&               viewport,
    const bool                                reverse_depth,
    const erhe::math::Depth_range             depth_range,
    const erhe::math::Coordinate_conventions& conventions
) -> Per_view_camera
{
    const erhe::scene::Transform clip_from_camera = projection.clip_from_node_transform(viewport, reverse_depth, depth_range, conventions);
    const glm::mat4              clip_from_world  = clip_from_camera.get_matrix() * node.node_from_world();
    const glm::mat4              world_from_node  = node.world_from_node();
    const glm::vec4              view_position_in_world{world_from_node[3]};
    const erhe::scene::Projection::Fov_sides fov  = projection.get_fov_sides(viewport);
    return Per_view_camera{
        .clip_from_world        = clip_from_world,
        .viewport               = glm::vec4{
            static_cast<float>(viewport.x),
            static_cast<float>(viewport.y),
            static_cast<float>(viewport.width),
            static_cast<float>(viewport.height)
        },
        .fov                    = glm::vec4{fov.left, fov.right, fov.up, fov.down},
        .view_position_in_world = view_position_in_world
    };
}

class Dispatch_per_frame_params
{
public:
    std::span<const Per_view_camera> per_view_cameras;
    uint32_t                         view_count_runtime;
    float                            vp_y_sign;
    float                            clip_depth_direction;
};

void write_view_block(
    std::span<std::byte>                  view_data,
    const Content_wide_line_view_offsets& offsets,
    const Dispatch_per_frame_params&      frame_params,
    const glm::mat4&                      world_from_node,
    const glm::vec4&                      color,
    float                                 line_width,
    uint32_t                              edge_count,
    uint32_t                              stride_per_view,
    uint32_t                              base_joint_index
)
{
    using erhe::graphics::as_span;
    using erhe::graphics::write;

    for (std::size_t v = 0; v < frame_params.per_view_cameras.size(); ++v) {
        const std::size_t base = v * offsets.camera_stride;
        write(view_data, base + offsets.clip_from_world,        as_span(frame_params.per_view_cameras[v].clip_from_world       ));
        write(view_data, base + offsets.viewport,               as_span(frame_params.per_view_cameras[v].viewport              ));
        write(view_data, base + offsets.fov,                    as_span(frame_params.per_view_cameras[v].fov                   ));
        write(view_data, base + offsets.view_position_in_world, as_span(frame_params.per_view_cameras[v].view_position_in_world));
    }

    const glm::vec4 line_color_with_width{color.x, color.y, color.z, line_width};
    const float     padding_zero        {0.0f};

    write(view_data, offsets.world_from_node,      as_span(world_from_node                ));
    write(view_data, offsets.line_color,           as_span(line_color_with_width          ));
    write(view_data, offsets.edge_count,           as_span(edge_count                     ));
    write(view_data, offsets.view_count,           as_span(frame_params.view_count_runtime));
    write(view_data, offsets.stride_per_view,      as_span(stride_per_view                ));
    write(view_data, offsets.vp_y_sign,            as_span(frame_params.vp_y_sign         ));
    write(view_data, offsets.clip_depth_direction, as_span(frame_params.clip_depth_direction));
    write(view_data, offsets.base_joint_index,     as_span(base_joint_index               ));
    write(view_data, offsets.padding0,             as_span(padding_zero                   ));
}

} // anonymous namespace

Content_wide_line_renderer::Content_wide_line_renderer(
    erhe::graphics::Device&        graphics_device,
    Content_wide_line_interface&   interface_,
    erhe::graphics::Shader_stages* compute_shader_stages,
    erhe::graphics::Shader_stages* compute_shader_stages_skinned,
    erhe::graphics::Shader_stages* graphics_shader_stages,
    erhe::graphics::Shader_stages* multiview_graphics_shader_stages
)
    : m_graphics_device                 {graphics_device}
    , m_interface                       {interface_}
    , m_compute_shader_stages           {compute_shader_stages}
    , m_compute_shader_stages_skinned   {compute_shader_stages_skinned}
    , m_graphics_shader_stages          {graphics_shader_stages}
    , m_multiview_graphics_shader_stages{multiview_graphics_shader_stages}
    , m_empty_vertex_input              {graphics_device, erhe::graphics::Vertex_input_state_data{}}
    , m_view_buffer{
        graphics_device,
        erhe::graphics::Buffer_target::uniform,
        "Content_wide_line_renderer::m_view_buffer",
        interface_.view_block.get_binding_point()
    }
    , m_triangle_vertex_buffer_client{
        graphics_device,
        erhe::graphics::Buffer_target::storage,
        "Content_wide_line_renderer::m_triangle_vertex_buffer",
        interface_.triangle_vertex_buffer_block.get_binding_point()
    }
{
    ERHE_VERIFY(m_compute_shader_stages != nullptr);
    ERHE_VERIFY(m_graphics_shader_stages != nullptr);
    ERHE_VERIFY(m_compute_shader_stages->is_valid());
    ERHE_VERIFY(m_graphics_shader_stages->is_valid());

    m_compute_pipeline.emplace(
        m_graphics_device,
        erhe::graphics::Compute_pipeline_data{
            .name              = "compute_before_content_line",
            .shader_stages     = m_compute_shader_stages,
            .bind_group_layout = &m_interface.bind_group_layout
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

Content_wide_line_renderer::~Content_wide_line_renderer() noexcept = default;

auto Content_wide_line_renderer::get_interface             () -> Content_wide_line_interface&         { return m_interface; }
auto Content_wide_line_renderer::get_graphics_shader_stages() -> erhe::graphics::Shader_stages*       { return m_graphics_shader_stages; }

void Content_wide_line_renderer::begin_frame()
{
    m_dispatches.clear();
}

void Content_wide_line_renderer::add_mesh(
    Mesh_memory&             mesh_memory,
    const erhe::scene::Mesh& mesh,
    const glm::vec4&         color,
    const float              line_width,
    const uint32_t           group
)
{
    const erhe::scene::Node* node = mesh.get_node();
    if (node == nullptr) {
        return;
    }

    const glm::mat4                           world_from_node        = node->world_from_node();
    const bool                                skinned_path_available = m_compute_pipeline_skinned.has_value();
    const std::shared_ptr<erhe::scene::Skin>& skin                   = mesh.skin;
    const bool                                use_skinned            = skinned_path_available && (skin != nullptr);
    const uint32_t                            base_joint_index       = use_skinned ? skin->skin_data.joint_buffer_index : 0u;

    for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh.get_primitives()) {
        if (!mesh_primitive.primitive) {
            continue;
        }
        const erhe::primitive::Buffer_mesh* buffer_mesh = mesh_primitive.primitive->get_renderable_mesh();
        if (buffer_mesh == nullptr) {
            continue;
        }

        const erhe::primitive::Buffer_range& edge_range = buffer_mesh->edge_line_vertex_buffer_range;
        if (edge_range.count == 0) {
            continue;
        }

        erhe::graphics::Buffer* edge_buffer = mesh_memory.get_vertex_buffer(edge_range);
        if (edge_buffer == nullptr) {
            continue;
        }

        const std::size_t       edge_count               = edge_range.count / 2;
        erhe::graphics::Buffer* joint_buffer             = nullptr;
        std::size_t             joint_buffer_byte_offset = 0;
        std::size_t             joint_buffer_byte_size   = 0;
        const erhe::primitive::Buffer_range& joint_range = buffer_mesh->edge_line_joint_buffer_range;
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
            .skinned                  = dispatch_is_skinned,
            .joint_buffer             = joint_buffer,
            .joint_buffer_byte_offset = joint_buffer_byte_offset,
            .joint_buffer_byte_size   = joint_buffer_byte_size,
            .base_joint_index         = dispatch_is_skinned ? base_joint_index : 0u
        });
    }
}

void Content_wide_line_renderer::compute(
    erhe::graphics::Compute_command_encoder&  command_encoder,
    std::span<const Camera_view_input>        views,
    erhe::graphics::Ring_buffer_client*       joint_buffer_client,
    erhe::graphics::Ring_buffer_range*        joint_buffer_range,
    const bool                                reverse_depth,
    const erhe::math::Depth_range             depth_range,
    const erhe::math::Coordinate_conventions& conventions
)
{
    if (m_dispatches.empty()) {
        return;
    }

    ERHE_VERIFY(!views.empty());
    ERHE_VERIFY(views.size() <= static_cast<std::size_t>(m_interface.view_count));

    erhe::graphics::Scoped_debug_group debug_scope{
        command_encoder.get_command_buffer(),
        "Content_wide_line_renderer::compute"
    };

    ERHE_VERIFY(m_compute_pipeline.has_value());

    const bool have_joint_data =
        (joint_buffer_client != nullptr) &&
        (joint_buffer_range  != nullptr) &&
        m_compute_pipeline_skinned.has_value();

    command_encoder.set_bind_group_layout(&m_interface.bind_group_layout);
    command_encoder.set_compute_pipeline(m_compute_pipeline.value());

    // Pre-compute per-view camera data once for the whole dispatch batch
    // (does not depend on the per-mesh primitive transform). Pad the
    // cameras[] tail with copies of the first entry when views is shorter
    // than the shader's compile-time view_count so the compute shader's
    // view loop never reads uninitialised UBO entries.
    std::vector<Per_view_camera> per_view_cameras;
    per_view_cameras.reserve(static_cast<std::size_t>(m_interface.view_count));
    for (const Camera_view_input& view : views) {
        ERHE_VERIFY(view.projection != nullptr);
        ERHE_VERIFY(view.node != nullptr);
        per_view_cameras.push_back(build_per_view_camera(
            *view.projection, *view.node, view.viewport, reverse_depth, depth_range, conventions
        ));
    }
    while (per_view_cameras.size() < static_cast<std::size_t>(m_interface.view_count)) {
        per_view_cameras.push_back(per_view_cameras.front());
    }
    const std::size_t view_count_runtime = views.size();

    const bool  top_left  = (m_graphics_device.get_info().coordinate_conventions.framebuffer_origin == erhe::math::Framebuffer_origin::top_left);
    const float vp_y_sign = top_left ? -1.0f : 1.0f;

    const Dispatch_per_frame_params frame_params{
        .per_view_cameras     = per_view_cameras,
        .view_count_runtime   = static_cast<uint32_t>(view_count_runtime),
        .vp_y_sign            = vp_y_sign,
        .clip_depth_direction = reverse_depth ? -1.0f : 1.0f,
    };

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
                joint_buffer_client->bind(command_encoder, *joint_buffer_range);
            } else {
                command_encoder.set_bind_group_layout(&m_interface.bind_group_layout);
                command_encoder.set_compute_pipeline(m_compute_pipeline.value());
            }
            current_skinned = dispatch_skinned;
            // Layout change invalidates previously-bound edge SSBOs.
            prev_edge_buffer        = nullptr;
            prev_edge_buffer_offset = 0;
            prev_edge_buffer_size   = 0;
        }

        // padded_edge_count = workgroup_count * 32 (= ceil(edge_count/32) * 32).
        // stride_per_view (in vertices) = padded_edge_count * 6.
        const uint32_t    workgroup_count   = static_cast<uint32_t>((dispatch.edge_count + 31) / 32);
        const std::size_t padded_edge_count = static_cast<std::size_t>(workgroup_count) * 32;
        const uint32_t    stride_per_view_v = static_cast<uint32_t>(padded_edge_count * 6);

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
            dispatch.base_joint_index
        );
        view_buffer_range.bytes_written(view_size);
        view_buffer_range.close();
        m_view_buffer.bind(command_encoder, view_buffer_range);
        // Hold the view UBO range so render() (potentially in a later
        // encoder) can re-bind it without re-uploading. Release moves to
        // end_frame().
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
                m_interface.edge_line_vertex_buffer_block.get_binding_point()
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
                m_interface.edge_line_joint_vertex_buffer_block.get_binding_point()
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
            m_interface.triangle_vertex_buffer_block.get_size_bytes()
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
        // end_frame() so the render encoder can re-bind it (the multiview
        // vertex shader reads stride_per_view from the same UBO).
    }

    // Note: the compute->vertex-attribute memory barrier that pairs with
    // these dispatches is emitted by the caller AFTER the compute encoder
    // scope ends. memory_barrier() inside compute() would be illegal on
    // Metal (cb cannot be split while a compute encoder is open) and
    // unnecessary on Vulkan (the call must happen between encoders, not
    // within one).
}

void Content_wide_line_renderer::render(
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
    // Content_wide_line_renderer::compute() after the last dispatch,
    // before any render pass begins. Emitting it here would be inside the
    // active render pass and therefore invalid on Vulkan.

    erhe::graphics::Scoped_debug_group debug_scope{
        render_encoder.get_command_buffer(),
        "Content_wide_line_renderer::render"
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

    erhe::graphics::Render_pipeline_state temp_state{
        erhe::graphics::Render_pipeline_data{
            .debug_label          = pipeline_state.data.debug_label,
            .shader_stages        = shader_stages,
            // Empty (no attributes) but non-null: OpenGL core profile
            // requires a non-default VAO to be bound for the draw to
            // actually fire. The triangles are read from the SSBO via
            // gl_VertexID, not through the input assembler.
            .vertex_input         = &m_empty_vertex_input,
            .input_assembly       = pipeline_state.data.input_assembly,
            .multisample          = pipeline_state.data.multisample,
            .viewport_depth_range = pipeline_state.data.viewport_depth_range,
            .rasterization        = pipeline_state.data.rasterization,
            .depth_stencil        = pipeline_state.data.depth_stencil,
            .color_blend          = (color_blend_state != nullptr)
                ? *color_blend_state
                : erhe::graphics::Color_blend_state::color_blend_disabled
        }
    };

    render_encoder.set_bind_group_layout(&m_interface.graphics_bind_group_layout);
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
            static_cast<uint32_t>(6 * dispatch.edge_count)
        );
    }
}

void Content_wide_line_renderer::end_frame()
{
    for (Dispatch_entry& dispatch : m_dispatches) {
        if (dispatch.dispatched) {
            dispatch.triangle_buffer_range.release();
            dispatch.view_buffer_range.release();
        }
    }
    m_dispatches.clear();

    // No cross-frame memory_barrier is emitted here. Across device frames
    // the triangle vertex buffer is accessed through a Ring_buffer_client
    // that hands out disjoint byte ranges per dispatch: frame N writes
    // range R_N, frame N-1 reads range R_(N-1), R_N != R_(N-1). Vulkan
    // tracks hazards per byte range, so even if the GPU overlaps frame
    // N-1's draws with frame N's compute across submit boundaries there
    // is no WAR on the same bytes. Range reuse is gated by the ring
    // buffer's per-frame bookkeeping, which is informed by the frame
    // fence (so a range is only returned to the free pool after its
    // frame is known complete on the GPU).
}

} // namespace erhe::scene_renderer
