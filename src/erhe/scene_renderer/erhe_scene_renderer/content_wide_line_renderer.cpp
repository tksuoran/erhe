#include "erhe_scene_renderer/content_wide_line_renderer.hpp"
#include "erhe_scene_renderer/buffer_binding_points.hpp"
#include "erhe_scene_renderer/scene_renderer_log.hpp"

#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/ring_buffer.hpp"
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
#include "erhe_verify/verify.hpp"

#include <glm/gtc/type_ptr.hpp>

namespace erhe::scene_renderer {

Content_wide_line_renderer::Content_wide_line_renderer(
    erhe::graphics::Device&        graphics_device,
    erhe::graphics::Shader_stages* compute_shader_stages,
    erhe::graphics::Shader_stages* graphics_shader_stages,
    int                            max_view_count
)
    : m_graphics_device        {graphics_device}
    , m_max_view_count         {std::max(1, max_view_count)}
    , m_fragment_outputs{
        erhe::graphics::Fragment_output{
            .name     = "out_color",
            .type     = erhe::graphics::Glsl_type::float_vec4,
            .location = 0
        }
    }
    , m_triangle_vertex_format{
        {
            0,
            {
                {erhe::dataformat::Format::format_32_vec4_float, erhe::dataformat::Vertex_attribute_usage::position},
                {erhe::dataformat::Format::format_32_vec4_float, erhe::dataformat::Vertex_attribute_usage::color},
                {erhe::dataformat::Format::format_32_vec4_float, erhe::dataformat::Vertex_attribute_usage::custom, 0},
            }
        }
    }
    , m_compute_shader_stages {compute_shader_stages}
    , m_graphics_shader_stages{graphics_shader_stages}
    , m_vertex_input{
        graphics_device,
        erhe::graphics::Vertex_input_state_data::make(m_triangle_vertex_format)
    }
    , m_view_buffer{
        graphics_device,
        erhe::graphics::Buffer_target::uniform,
        "Content_wide_line_renderer::m_view_buffer",
        3 // view binding point
    }
    , m_triangle_vertex_buffer_client{
        graphics_device,
        erhe::graphics::Buffer_target::storage,
        "Content_wide_line_renderer::m_triangle_vertex_buffer",
        1 // output triangle SSBO binding point
    }
{
    if (!graphics_device.get_info().use_compute_shader) {
        m_enabled = false;
        return;
    }

    // Edge line vertex struct for the input SSBO
    m_edge_line_vertex_struct = std::make_unique<erhe::graphics::Shader_resource>(graphics_device, "edge_line_vertex");
    m_edge_line_vertex_struct->add_vec4("position");
    m_edge_line_vertex_struct->add_vec4("normal");

    m_edge_line_vertex_buffer_block = std::make_unique<erhe::graphics::Shader_resource>(
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "edge_line_vertex_buffer",
            .binding_point = 0,
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    );
    m_edge_line_vertex_buffer_block->add_struct("vertices", m_edge_line_vertex_struct.get(), erhe::graphics::Shader_resource::unsized_array);

    // Triangle output SSBO
    m_triangle_vertex_struct = std::make_unique<erhe::graphics::Shader_resource>(graphics_device, "triangle_vertex");
    m_triangle_vertex_buffer_block = std::make_unique<erhe::graphics::Shader_resource>(
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "triangle_vertex_buffer",
            .binding_point = 1,
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .writeonly     = true
        }
    );
    erhe::graphics::add_vertex_stream(
        m_triangle_vertex_format.streams.front(),
        *m_triangle_vertex_struct.get(),
        *m_triangle_vertex_buffer_block.get()
    );

    // Read-only declaration of the same triangle SSBO binding for the
    // multiview vertex shader (which fetches its triangle data from
    // here instead of via the input assembler). Distinct GLSL block
    // (different .name to avoid duplicate-symbol errors) but mapped to
    // the same descriptor set binding so a single buffer bind serves
    // both the writeonly compute output declaration and this readonly
    // input declaration.
    m_triangle_vertex_buffer_read_block = std::make_unique<erhe::graphics::Shader_resource>(
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "triangle_vertex_buffer",
            .binding_point = 1,
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    );
    m_triangle_vertex_buffer_read_block->add_struct("vertices", m_triangle_vertex_struct.get(), erhe::graphics::Shader_resource::unsized_array);

    // View UBO. Layout:
    //   ViewCamera cameras[max_view_count];   // per-eye camera
    //   mat4   world_from_node;               // per-dispatch
    //   vec4   line_color;                    // per-dispatch (.w = line_width)
    //   uint   edge_count;                    // per-dispatch
    //   uint   view_count;                    // mirrors max_view_count
    //   uint   stride_per_view;               // padded_edge_count * 6 vertices
    //   float  vp_y_sign;
    //   float  clip_depth_direction;
    //   float  _padding[2];
    //
    // Per-eye data is grouped into cameras[] so the multiview compute
    // shader can write one triangle set per view in a single dispatch
    // (loop over views) and the multiview vertex shader can index its
    // SSBO read by gl_ViewIndex without a separate push constant.
    m_view_camera_struct = std::make_unique<erhe::graphics::Shader_resource>(graphics_device, "ContentLineViewCamera");
    m_view_camera_clip_from_world_offset        = m_view_camera_struct->add_mat4("clip_from_world"       )->get_offset_in_parent();
    m_view_camera_viewport_offset               = m_view_camera_struct->add_vec4("viewport"              )->get_offset_in_parent();
    m_view_camera_fov_offset                    = m_view_camera_struct->add_vec4("fov"                   )->get_offset_in_parent();
    m_view_camera_view_position_in_world_offset = m_view_camera_struct->add_vec4("view_position_in_world")->get_offset_in_parent();
    m_view_camera_stride                        = m_view_camera_struct->get_size_bytes();

    m_view_block = std::make_unique<erhe::graphics::Shader_resource>(
        graphics_device,
        "view",
        3,
        erhe::graphics::Shader_resource::Type::uniform_block
    );
    m_view_block->add_struct("cameras", m_view_camera_struct.get(), static_cast<std::size_t>(m_max_view_count));
    m_world_from_node_offset      = m_view_block->add_mat4 ("world_from_node"     )->get_offset_in_parent();
    m_line_color_offset           = m_view_block->add_vec4 ("line_color"          )->get_offset_in_parent();
    m_edge_count_offset           = m_view_block->add_uint ("edge_count"          )->get_offset_in_parent();
    m_view_count_offset           = m_view_block->add_uint ("view_count"          )->get_offset_in_parent();
    m_stride_per_view_offset      = m_view_block->add_uint ("stride_per_view"     )->get_offset_in_parent();
    m_vp_y_sign_offset            = m_view_block->add_float("vp_y_sign"           )->get_offset_in_parent();
    m_clip_depth_direction_offset = m_view_block->add_float("clip_depth_direction")->get_offset_in_parent();
    m_padding0_offset             = m_view_block->add_float("_padding0"           )->get_offset_in_parent();
    m_padding1_offset             = m_view_block->add_float("_padding1"           )->get_offset_in_parent();

    m_bind_group_layout = std::make_unique<erhe::graphics::Bind_group_layout>(
        graphics_device,
        erhe::graphics::Bind_group_layout_create_info{
            .bindings = {
                {m_edge_line_vertex_buffer_block->get_binding_point(), erhe::graphics::Binding_type::storage_buffer},
                {m_triangle_vertex_buffer_block->get_binding_point(),  erhe::graphics::Binding_type::storage_buffer},
                {m_view_block->get_binding_point(),
                    (m_view_block->get_type() == erhe::graphics::Shader_resource::Type::shader_storage_block)
                        ? erhe::graphics::Binding_type::storage_buffer
                        : erhe::graphics::Binding_type::uniform_buffer},
            },
            .debug_label       = "Content wide line",
            .uses_texture_heap = false
        }
    );

    // Multiview graphics-pipeline layout. Triangle SSBO bound here
    // read-only (vertex stage indexes it via gl_VertexID + gl_ViewIndex
    // * stride_per_view). View UBO bound for stride_per_view AND for
    // the fragment shader's per-eye viewport.xy read - we deliberately
    // do NOT include the program_interface camera UBO at binding 4 in
    // this layout. Switching descriptor-set layouts mid-render-pass
    // (here from forward_renderer's program_interface layout to this
    // wide-line layout) invalidates any previously bound descriptors,
    // and we have no way to re-bind the camera UBO that
    // forward_renderer wrote earlier in the same pass. So the
    // multiview content_line_after_compute fragment reads viewport.xy
    // from view.cameras[c_view_index] instead.
    m_multiview_graphics_bind_group_layout = std::make_unique<erhe::graphics::Bind_group_layout>(
        graphics_device,
        erhe::graphics::Bind_group_layout_create_info{
            .bindings = {
                {m_triangle_vertex_buffer_block->get_binding_point(), erhe::graphics::Binding_type::storage_buffer},
                {m_view_block->get_binding_point(),
                    (m_view_block->get_type() == erhe::graphics::Shader_resource::Type::shader_storage_block)
                        ? erhe::graphics::Binding_type::storage_buffer
                        : erhe::graphics::Binding_type::uniform_buffer},
            },
            .debug_label       = "Content wide line multiview graphics",
            .uses_texture_heap = false
        }
    );

    if ((m_compute_shader_stages != nullptr) && m_compute_shader_stages->is_valid() &&
        (m_graphics_shader_stages != nullptr) && m_graphics_shader_stages->is_valid()) {
        m_compute_pipeline.emplace(
            m_graphics_device,
            erhe::graphics::Compute_pipeline_data{
                .name              = "compute_before_content_line",
                .shader_stages     = m_compute_shader_stages,
                .bind_group_layout = m_bind_group_layout.get()
            }
        );
        m_enabled = true;
    }
}

Content_wide_line_renderer::~Content_wide_line_renderer() noexcept = default;

auto Content_wide_line_renderer::is_enabled() const -> bool
{
    return m_enabled;
}

void Content_wide_line_renderer::set_shader_stages(
    erhe::graphics::Shader_stages* compute_shader_stages,
    erhe::graphics::Shader_stages* graphics_shader_stages,
    erhe::graphics::Shader_stages* multiview_graphics_shader_stages
)
{
    m_compute_shader_stages           = compute_shader_stages;
    m_graphics_shader_stages          = graphics_shader_stages;
    m_multiview_graphics_shader_stages = multiview_graphics_shader_stages;

    if ((m_compute_shader_stages != nullptr) && m_compute_shader_stages->is_valid() &&
        (m_graphics_shader_stages != nullptr) && m_graphics_shader_stages->is_valid()) {
        m_compute_pipeline.emplace(
            m_graphics_device,
            erhe::graphics::Compute_pipeline_data{
                .name              = "compute_before_content_line",
                .shader_stages     = m_compute_shader_stages,
                .bind_group_layout = m_bind_group_layout.get()
            }
        );
        m_enabled = true;
    }
}

auto Content_wide_line_renderer::get_edge_line_vertex_struct      () const -> erhe::graphics::Shader_resource* { return m_edge_line_vertex_struct.get(); }
auto Content_wide_line_renderer::get_edge_line_vertex_buffer_block() const -> erhe::graphics::Shader_resource* { return m_edge_line_vertex_buffer_block.get(); }
auto Content_wide_line_renderer::get_triangle_vertex_struct       () const -> erhe::graphics::Shader_resource* { return m_triangle_vertex_struct.get(); }
auto Content_wide_line_renderer::get_triangle_vertex_buffer_block () const -> erhe::graphics::Shader_resource* { return m_triangle_vertex_buffer_block.get(); }
auto Content_wide_line_renderer::get_triangle_vertex_buffer_read_block() const -> erhe::graphics::Shader_resource* { return m_triangle_vertex_buffer_read_block.get(); }
auto Content_wide_line_renderer::get_view_camera_struct           () const -> erhe::graphics::Shader_resource* { return m_view_camera_struct.get(); }
auto Content_wide_line_renderer::get_view_block                   () const -> erhe::graphics::Shader_resource* { return m_view_block.get(); }
auto Content_wide_line_renderer::get_bind_group_layout            () const -> erhe::graphics::Bind_group_layout* { return m_bind_group_layout.get(); }
auto Content_wide_line_renderer::get_multiview_graphics_bind_group_layout() const -> erhe::graphics::Bind_group_layout* { return m_multiview_graphics_bind_group_layout.get(); }
auto Content_wide_line_renderer::get_fragment_outputs             () -> erhe::graphics::Fragment_outputs&       { return m_fragment_outputs; }
auto Content_wide_line_renderer::get_triangle_vertex_format       () -> erhe::dataformat::Vertex_format&       { return m_triangle_vertex_format; }
auto Content_wide_line_renderer::get_vertex_input                 () -> erhe::graphics::Vertex_input_state*    { return &m_vertex_input; }
auto Content_wide_line_renderer::get_graphics_shader_stages       () -> erhe::graphics::Shader_stages*         { return m_graphics_shader_stages; }

void Content_wide_line_renderer::begin_frame()
{
    if (!m_enabled) {
        return;
    }

    m_dispatches.clear();
}

void Content_wide_line_renderer::add_mesh(
    const erhe::scene::Mesh& mesh,
    const glm::vec4&         color,
    const float              line_width,
    const uint32_t           group
)
{
    if (!m_enabled) {
        return;
    }

    const erhe::scene::Node* node = mesh.get_node();
    if (node == nullptr) {
        return;
    }

    const glm::mat4 world_from_node = node->world_from_node();

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

        const std::size_t edge_count = edge_range.count / 2;
        m_dispatches.push_back(Dispatch_entry{
            .edge_buffer             = edge_range.buffer,
            .edge_buffer_byte_offset = edge_range.byte_offset,
            .edge_count              = edge_count,
            .world_from_node         = world_from_node,
            .color                   = color,
            .line_width              = line_width,
            .group                   = group
        });
    }
}

void Content_wide_line_renderer::compute(
    erhe::graphics::Compute_command_encoder&  command_encoder,
    const erhe::math::Viewport&               viewport,
    const erhe::scene::Camera&                camera,
    const bool                                reverse_depth,
    const erhe::math::Depth_range             depth_range,
    const erhe::math::Coordinate_conventions& conventions
)
{
    compute(command_encoder, viewport, &camera, std::span<const Camera_view_input>{}, reverse_depth, depth_range, conventions);
}

namespace {

struct Per_view_camera
{
    glm::mat4 clip_from_world;
    glm::vec4 viewport;
    glm::vec4 fov;
    glm::vec4 view_position_in_world;
};

auto build_per_view_camera(
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

} // anonymous namespace

void Content_wide_line_renderer::compute(
    erhe::graphics::Compute_command_encoder&  command_encoder,
    const erhe::math::Viewport&               viewport,
    const erhe::scene::Camera*                camera,
    std::span<const Camera_view_input>        multiview_views,
    const bool                                reverse_depth,
    const erhe::math::Depth_range             depth_range,
    const erhe::math::Coordinate_conventions& conventions
)
{
    if (!m_enabled || m_dispatches.empty()) {
        return;
    }

    const bool multiview_path = !multiview_views.empty();
    if (multiview_path) {
        ERHE_VERIFY(static_cast<int>(multiview_views.size()) == m_max_view_count);
    } else {
        ERHE_VERIFY(camera != nullptr);
    }

    erhe::graphics::Scoped_debug_group debug_scope{
        command_encoder.get_command_buffer(),
        "Content_wide_line_renderer::compute"
    };

    ERHE_VERIFY(m_compute_pipeline.has_value());
    command_encoder.set_bind_group_layout(m_bind_group_layout.get());
    command_encoder.set_compute_pipeline(m_compute_pipeline.value());

    // Pre-compute per-view camera data once for the whole dispatch
    // batch (does not depend on the per-mesh primitive transform).
    std::vector<Per_view_camera> per_view_cameras;
    per_view_cameras.reserve(static_cast<std::size_t>(m_max_view_count));
    if (multiview_path) {
        for (const Camera_view_input& view : multiview_views) {
            ERHE_VERIFY(view.projection != nullptr);
            ERHE_VERIFY(view.node != nullptr);
            per_view_cameras.push_back(build_per_view_camera(
                *view.projection, *view.node, viewport, reverse_depth, depth_range, conventions
            ));
        }
    } else {
        const erhe::scene::Node* camera_node = camera->get_node();
        ERHE_VERIFY(camera_node != nullptr);
        per_view_cameras.push_back(build_per_view_camera(
            *camera->projection(), *camera_node, viewport, reverse_depth, depth_range, conventions
        ));
        // Pad the cameras[] tail with copies so the compute shader loop
        // (which always runs view_count iterations from the UBO) does
        // not read uninitialised entries when max_view_count > 1 but the
        // current call is single-view.
        while (per_view_cameras.size() < static_cast<std::size_t>(m_max_view_count)) {
            per_view_cameras.push_back(per_view_cameras.front());
        }
    }

    const float       clip_depth_direction = reverse_depth ? -1.0f : 1.0f;
    const std::size_t triangle_vertex_stride = m_triangle_vertex_format.streams.front().stride;
    const std::size_t view_count_runtime = multiview_path ? multiview_views.size() : std::size_t{1};

    for (Dispatch_entry& dispatch : m_dispatches) {
        const std::size_t view_size = m_view_block->get_size_bytes();
        erhe::graphics::Ring_buffer_range view_buffer_range = m_view_buffer.acquire(
            erhe::graphics::Ring_buffer_usage::CPU_write,
            view_size
        );
        const std::span<std::byte> view_data = view_buffer_range.get_span();

        using erhe::graphics::write;
        using erhe::graphics::as_span;

        // Write per-view ViewCamera entries into cameras[] at the
        // start of the block.
        for (std::size_t v = 0; v < per_view_cameras.size(); ++v) {
            const std::size_t base = v * m_view_camera_stride;
            write(view_data, base + m_view_camera_clip_from_world_offset,        as_span(per_view_cameras[v].clip_from_world));
            write(view_data, base + m_view_camera_viewport_offset,               as_span(per_view_cameras[v].viewport       ));
            write(view_data, base + m_view_camera_fov_offset,                    as_span(per_view_cameras[v].fov            ));
            write(view_data, base + m_view_camera_view_position_in_world_offset, as_span(per_view_cameras[v].view_position_in_world));
        }

        write(view_data, m_world_from_node_offset, as_span(dispatch.world_from_node));
        const glm::vec4 line_color_with_width{dispatch.color.x, dispatch.color.y, dispatch.color.z, dispatch.line_width};
        write(view_data, m_line_color_offset, as_span(line_color_with_width));
        const uint32_t edge_count_uint = static_cast<uint32_t>(dispatch.edge_count);
        write(view_data, m_edge_count_offset, as_span(edge_count_uint));

        // padded_edge_count = workgroup_count * 32 (= ceil(edge_count/32) * 32).
        // stride_per_view (in vertices) = padded_edge_count * 6.
        const uint32_t    workgroup_count   = static_cast<uint32_t>((dispatch.edge_count + 31) / 32);
        const std::size_t padded_edge_count = static_cast<std::size_t>(workgroup_count) * 32;
        const uint32_t    stride_per_view_v = static_cast<uint32_t>(padded_edge_count * 6);
        const uint32_t    view_count_uint   = static_cast<uint32_t>(view_count_runtime);
        write(view_data, m_view_count_offset,      as_span(view_count_uint));
        write(view_data, m_stride_per_view_offset, as_span(stride_per_view_v));

        const bool  top_left  = (m_graphics_device.get_info().coordinate_conventions.framebuffer_origin == erhe::math::Framebuffer_origin::top_left);
        const float vp_y_sign = top_left ? -1.0f : 1.0f;
        const float zero      = 0.0f;
        write(view_data, m_vp_y_sign_offset,            as_span(vp_y_sign));
        write(view_data, m_clip_depth_direction_offset, as_span(clip_depth_direction));
        write(view_data, m_padding0_offset,             as_span(zero));
        write(view_data, m_padding1_offset,             as_span(zero));

        view_buffer_range.bytes_written(view_size);
        view_buffer_range.close();
        m_view_buffer.bind(command_encoder, view_buffer_range);
        // Hold the view UBO range so render() (potentially in a later
        // encoder) can re-bind it without re-uploading. Release moves
        // to end_frame().
        dispatch.view_buffer_range = std::move(view_buffer_range);

        // Each dispatch carries its own source buffer pointer so meshes
        // whose edge data landed in different lazy-grown pool blocks each
        // get the right SSBO bound for their compute dispatch.
        command_encoder.set_buffer(
            erhe::graphics::Buffer_target::storage,
            dispatch.edge_buffer,
            dispatch.edge_buffer_byte_offset,
            dispatch.edge_count * 2 * sizeof(float) * 8,
            m_edge_line_vertex_buffer_block->get_binding_point()
        );

        // Triangle SSBO holds view_count_runtime contiguous slabs, each
        // sized stride_per_view * vertex_stride. The multiview compute
        // shader writes one slab per loop iteration; the multiview
        // vertex shader indexes within the buffer by gl_VertexID +
        // gl_ViewIndex * stride_per_view.
        const std::size_t per_view_triangle_byte_count = static_cast<std::size_t>(stride_per_view_v) * triangle_vertex_stride;
        const std::size_t triangle_byte_count          = view_count_runtime * per_view_triangle_byte_count;
        // See note in joint_buffer.cpp.
        const std::size_t triangle_acquire_byte_count = std::max(
            triangle_byte_count,
            m_triangle_vertex_buffer_block->get_size_bytes()
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
        // end_frame() so the render encoder can re-bind it (the
        // multiview vertex shader reads stride_per_view from the same
        // UBO).
    }

    // Note: the compute->vertex-attribute memory barrier that pairs
    // with these dispatches is emitted by the caller AFTER the compute
    // encoder scope ends. memory_barrier() inside compute() would be
    // illegal on Metal (cb cannot be split while a compute encoder is
    // open) and unnecessary on Vulkan (the call must happen between
    // encoders, not within one).
}

void Content_wide_line_renderer::render(
    erhe::graphics::Render_command_encoder& render_encoder,
    erhe::graphics::Lazy_render_pipeline&   pipeline_state,
    const erhe::graphics::Render_pass&      render_pass,
    const uint32_t                          group,
    const bool                              multiview
)
{
    if (!m_enabled || m_dispatches.empty()) {
        return;
    }

    // Note: the compute->vertex-attribute memory barrier is emitted by
    // Content_wide_line_renderer::compute() after the last dispatch,
    // before any render pass begins. Emitting it here would be inside
    // the active render pass and therefore invalid on Vulkan.

    erhe::graphics::Scoped_debug_group debug_scope{
        render_encoder.get_command_buffer(),
        "Content_wide_line_renderer::render"
    };

    if (multiview) {
        // Multiview render path. The pipeline_state was constructed
        // around the single-view shader and vertex format; bypass it
        // and bind a temp pipeline with the multiview shader stages so
        // the Vulkan render-encoder pipeline cache keys correctly on
        // both the multiview shader modules AND the multiview render
        // pass viewMask. (The Lazy_render_pipeline cache is keyed on
        // single-view shaders and intentionally bypassed here, mirroring
        // the Forward_renderer multiview override path.)
        if ((m_multiview_graphics_shader_stages == nullptr) || !m_multiview_graphics_shader_stages->is_valid()) {
            return;
        }

        erhe::graphics::Render_pipeline_state temp_state{
            erhe::graphics::Render_pipeline_data{
                .debug_label    = pipeline_state.data.debug_label,
                .shader_stages  = m_multiview_graphics_shader_stages,
                .vertex_input   = nullptr, // multiview path reads triangle SSBO, not vertex buffer
                .input_assembly = pipeline_state.data.input_assembly,
                .multisample    = pipeline_state.data.multisample,
                .viewport_depth_range = pipeline_state.data.viewport_depth_range,
                .rasterization  = pipeline_state.data.rasterization,
                .depth_stencil  = pipeline_state.data.depth_stencil,
                .color_blend    = pipeline_state.data.color_blend
            }
        };

        render_encoder.set_bind_group_layout(m_multiview_graphics_bind_group_layout.get());
        render_encoder.set_render_pipeline_state(temp_state, m_multiview_graphics_shader_stages);

        for (const Dispatch_entry& dispatch : m_dispatches) {
            if (!dispatch.dispatched || (dispatch.group != group)) {
                continue;
            }

            // Bind the triangle SSBO read-only at the same binding
            // point as the compute write-output so the multiview
            // vertex shader can index it as triangle_vertex_buffer
            // [gl_VertexID + gl_ViewIndex * stride_per_view].
            m_triangle_vertex_buffer_client.bind(render_encoder, dispatch.triangle_buffer_range);
            // Re-bind the view UBO carried over from compute() so the
            // multiview vertex shader can read stride_per_view. The
            // camera UBO at binding 4 is bound by the surrounding
            // forward_renderer call earlier in the same render pass;
            // the fragment shader reads camera.cameras[c_view_index].
            // viewport.xy from it without us needing to rebind.
            m_view_buffer.bind(render_encoder, dispatch.view_buffer_range);

            render_encoder.draw_primitives(
                erhe::graphics::Primitive_type::triangle,
                0,
                static_cast<uint32_t>(6 * dispatch.edge_count)
            );
        }
        return;
    }

    erhe::graphics::Render_pipeline* render_pipeline = pipeline_state.get_pipeline_for(render_pass.get_descriptor());
    if (render_pipeline == nullptr) {
        return;
    }
    render_encoder.set_bind_group_layout(m_bind_group_layout.get());
    render_encoder.set_render_pipeline(*render_pipeline);

    for (const Dispatch_entry& dispatch : m_dispatches) {
        if (!dispatch.dispatched || (dispatch.group != group)) {
            continue;
        }

        erhe::graphics::Buffer* triangle_buffer        = dispatch.triangle_buffer_range.get_buffer()->get_buffer();
        std::size_t             triangle_buffer_offset = dispatch.triangle_buffer_range.get_byte_start_offset_in_buffer();
        render_encoder.set_vertex_buffer(triangle_buffer, triangle_buffer_offset, 0);
        render_encoder.draw_primitives(
            erhe::graphics::Primitive_type::triangle,
            0,
            static_cast<uint32_t>(6 * dispatch.edge_count)
        );
    }
}

void Content_wide_line_renderer::end_frame()
{
    if (!m_enabled) {
        return;
    }

    for (Dispatch_entry& dispatch : m_dispatches) {
        if (dispatch.dispatched) {
            dispatch.triangle_buffer_range.release();
            dispatch.view_buffer_range.release();
        }
    }
    m_dispatches.clear();

    // No cross-frame memory_barrier is emitted here. Across device
    // frames the triangle vertex buffer is accessed through a
    // Ring_buffer_client that hands out disjoint byte ranges per
    // dispatch: frame N writes range R_N, frame N-1 reads range
    // R_(N-1), R_N != R_(N-1). Vulkan tracks hazards per byte range,
    // so even if the GPU overlaps frame N-1's draws with frame N's
    // compute across submit boundaries there is no WAR on the same
    // bytes. Range reuse is gated by the ring buffer's per-frame
    // bookkeeping, which is informed by the frame fence (so a range
    // is only returned to the free pool after its frame is known
    // complete on the GPU). The previous memory_barrier call here
    // was also illegal under Vulkan because this function is invoked
    // from inside an active render pass.
}

} // namespace erhe::scene_renderer
