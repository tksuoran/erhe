#include "erhe_renderer/debug_renderer.hpp"
#include "erhe_renderer/debug_renderer_bucket.hpp"

#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/span.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_verify/verify.hpp"

#include <cstring>

namespace erhe::renderer {

using glm::mat4;
using glm::vec2;
using glm::vec3;
using glm::vec4;

bool operator==(const Debug_renderer_config& lhs, const Debug_renderer_config& rhs)
{
    return
        (lhs.primitive_type    == rhs.primitive_type   ) &&
        (lhs.stencil_reference == rhs.stencil_reference) &&
        (lhs.draw_visible      == rhs.draw_visible     ) &&
        (lhs.draw_hidden       == rhs.draw_hidden      ) &&
        (lhs.thin_lines        == rhs.thin_lines       );
}

auto Debug_renderer_shader_key::derive(
    const bool                   device_use_compute,
    const bool                   device_use_geometry_shader,
    const Debug_renderer_config& config
) -> Debug_renderer_shader_key
{
    Debug_renderer_shader_key key;
    key.primitive_type = config.primitive_type;
    if (config.primitive_type != erhe::graphics::Primitive_type::line) {
        // Triangles and points render directly; there is no wide-line
        // expansion to do, so the compute / geometry tiers do not apply.
        key.tier = Tier::simple;
    } else if (device_use_compute && !config.thin_lines) {
        key.tier = Tier::compute;
    } else if (device_use_geometry_shader && !config.thin_lines) {
        key.tier = Tier::geometry;
    } else {
        key.tier = Tier::simple;
    }
    return key;
}

auto Debug_renderer_bucket::Debug_renderer_bucket::make_pipeline(const bool visible) -> erhe::graphics::Base_render_pipeline
{
    const bool reverse_depth = (m_graphics_device.get_info().coordinate_conventions.native_depth_range == erhe::math::Depth_range::zero_to_one);
    using namespace erhe::graphics;

    // Three tiers: compute (triangles) > geometry shader (GL_LINES + geom expand) > simple.
    // The compute tier expands lines into triangles, so it draws GL_TRIANGLES;
    // the geometry tier and the simple-line variant draw GL_LINES; the simple
    // tier for non-line primitives draws that primitive's topology directly.
    // The shader stages and vertex input vary across tiers but live outside the
    // base pipeline state -- they are passed to get_pipeline_for() at draw time
    // (see render_compute_draws / render_line_draws below).
    Input_assembly_state input_assembly = Input_assembly_state::line;
    switch (m_shader_key.tier) {
        case Debug_renderer_shader_key::Tier::compute:  input_assembly = Input_assembly_state::triangle; break;
        case Debug_renderer_shader_key::Tier::geometry: input_assembly = Input_assembly_state::line;     break;
        case Debug_renderer_shader_key::Tier::simple:
        default: {
            switch (m_shader_key.primitive_type) {
                case erhe::graphics::Primitive_type::triangle: input_assembly = Input_assembly_state::triangle; break;
                case erhe::graphics::Primitive_type::point:    input_assembly = Input_assembly_state::point;    break;
                case erhe::graphics::Primitive_type::line:
                default:                                       input_assembly = Input_assembly_state::line;     break;
            }
            break;
        }
    }

    // Filled-triangle overlays are coplanar with the surface they highlight,
    // so the visible pass (depth compare less / reversed greater) would reject
    // them on equal depth. Enabling depth bias (polygon offset) nudges them
    // toward the viewer; the magnitudes are dynamic state set per pass in
    // render() (sign matched to the reverse-depth convention there).
    const Rasterization_state rasterization =
        (m_shader_key.primitive_type == erhe::graphics::Primitive_type::triangle)
            ? Rasterization_state::cull_mode_none.with_depth_bias()
            : Rasterization_state::cull_mode_none;

    const Compare_operation depth_compare_op0 = visible ? Compare_operation::less : Compare_operation::greater_or_equal;
    const Compare_operation depth_compare_op  = reverse_depth ? reverse(depth_compare_op0) : depth_compare_op0;
    return Base_render_pipeline{
        m_graphics_device,
        Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"Line Renderer"},
            .input_assembly = input_assembly,
            .rasterization  = rasterization,
            .depth_stencil  = {
                .depth_test_enable   = true,
                .depth_write_enable  = false,
                .depth_compare_op    = depth_compare_op,
                .stencil_test_enable = true,
                .stencil_front = {
                    .stencil_fail_op = Stencil_op::keep,
                    .z_fail_op       = Stencil_op::keep,
                    .z_pass_op       = Stencil_op::replace,
                    .function        = Compare_operation::greater, //gequal,
                    .reference       = m_config.stencil_reference,
                    .test_mask       = visible ? 0b01111111u : 0b11111111u,
                    .write_mask      = 0b01111111u
                },
                .stencil_back = {
                    .stencil_fail_op = Stencil_op::keep,
                    .z_fail_op       = Stencil_op::keep,
                    .z_pass_op       = Stencil_op::replace,
                    .function        = Compare_operation::greater, //gequal,
                    .reference       = m_config.stencil_reference,
                    .test_mask       = visible ? 0b01111111u : 0b11111111u,
                    .write_mask      = 0b01111111u
                },
            },
        }
    };
}

// Note that this relies on bucket being stable, as in etl::vector<> when elements are never removed.

Debug_renderer_bucket::Debug_renderer_bucket(
    erhe::graphics::Device& graphics_device,
    Debug_renderer&         debug_renderer,
    Debug_renderer_config   config
)
    : m_graphics_device   {graphics_device}
    , m_debug_renderer    {debug_renderer}
    , m_shader_key{
        Debug_renderer_shader_key::derive(
            debug_renderer.use_compute(),
            debug_renderer.use_geometry_shader(),
            config
        )
    }
    , m_view_buffer{
        graphics_device,
        erhe::graphics::Buffer_target::uniform,
        "Debug_renderer::m_view_buffer",
        debug_renderer.get_program_interface().view_block->get_binding_point()
    }
    , m_config            {config}
    , m_pipeline_visible  {make_pipeline(true )}
    , m_pipeline_hidden   {make_pipeline(false)}
{
    const auto& program_interface = debug_renderer.get_program_interface();
    if (uses_compute()) {
        m_vertex_ssbo_buffer.emplace(
            graphics_device,
            erhe::graphics::Buffer_target::storage,
            "Debug_renderer_bucket::m_vertex_ssbo_buffer",
            program_interface.line_vertex_buffer_block->get_binding_point()
        );
        m_triangle_vertex_buffer.emplace(
            graphics_device,
            erhe::graphics::Buffer_target::storage,
            "Debug_renderer_bucket::m_triangle_vertex_buffer",
            program_interface.triangle_vertex_buffer_block->get_binding_point()
        );
    } else {
        m_line_vertex_buffer.emplace(
            graphics_device,
            erhe::graphics::Buffer_target::vertex,
            "Debug_renderer_bucket::m_line_vertex_buffer",
            0 // binding point not used for vertex buffers
        );
    }
}

auto Debug_renderer_bucket::update_view_buffer(
    std::span<const View> views,
    std::size_t           primitive_count_for_stride
) -> erhe::graphics::Ring_buffer_range
{
    const Debug_renderer_program_interface& program_interface = m_debug_renderer.get_program_interface();
    const erhe::graphics::Shader_resource&  view_block        = *program_interface.view_block.get();
    const std::size_t                       view_block_size   = view_block.get_size_bytes();
    erhe::graphics::Ring_buffer_range       view_buffer_range = m_view_buffer.acquire(erhe::graphics::Ring_buffer_usage::CPU_write, view_block_size);
    const std::span<std::byte>              view_gpu_data     = view_buffer_range.get_span();

    // Zero the whole block so unused cameras[k>views.size()] entries
    // (and any padding) read as zeros on the GPU. MoltenVK descriptor
    // validation requires the bound range to cover the full block
    // size; leaving tail bytes uninitialised here would expose
    // ring-buffer leftovers from a prior frame to the shader.
    std::memset(view_gpu_data.data(), 0, view_gpu_data.size_bytes());

    using erhe::graphics::write;
    using erhe::graphics::as_span;

    // Populate cameras[0..views.size()-1]. Single-view = 1 entry.
    // Multiview = N entries (must equal view_count for the
    // multiview vertex shader to index correctly).
    const std::size_t cam_stride = program_interface.view_camera_stride;
    for (std::size_t i = 0; i < views.size(); ++i) {
        const std::size_t base = i * cam_stride;
        write(view_gpu_data, base + program_interface.view_camera_clip_from_world_offset,        as_span(views[i].clip_from_world       ));
        write(view_gpu_data, base + program_interface.view_camera_viewport_offset,               as_span(views[i].viewport              ));
        write(view_gpu_data, base + program_interface.view_camera_fov_offset,                    as_span(views[i].fov_sides             ));
        write(view_gpu_data, base + program_interface.view_camera_view_position_in_world_offset, as_span(views[i].view_position_in_world));
    }

    const uint32_t view_count_uint      = static_cast<uint32_t>(views.size());
    // stride_per_view is in vertices: each line emits 6 vertices. The
    // SSBO holds view_count contiguous slabs of this size; the
    // multiview vertex shader indexes
    //     gl_VertexID + gl_ViewIndex * stride_per_view
    // and the compute shader's loop over view.view_count writes one
    // slab per iteration at out_triangle_index = v * (stride/3) + 2*i.
    // Single-view: stride_per_view is unused (view_count = 1, only the
    // v=0 iteration runs); zero is fine and the caller passes 0.
    const uint32_t stride_per_view_uint = (views.size() >= 2)
        ? static_cast<uint32_t>(6u * primitive_count_for_stride)
        : 0u;
    write(view_gpu_data, program_interface.view_count_offset,      as_span(view_count_uint     ));
    write(view_gpu_data, program_interface.stride_per_view_offset, as_span(stride_per_view_uint));

    const bool  top_left  = (m_graphics_device.get_info().coordinate_conventions.framebuffer_origin == erhe::math::Framebuffer_origin::top_left);
    const float vp_y_sign = top_left ? -1.0f : 1.0f;
    write(view_gpu_data, program_interface.vp_y_sign_offset, as_span(vp_y_sign));

    // +1.0 forward depth, -1.0 reverse depth. Lets the line shaders push a
    // surface-aligned line toward the viewer regardless of depth convention.
    const bool  reverse_depth        = (m_graphics_device.get_info().coordinate_conventions.native_depth_range == erhe::math::Depth_range::zero_to_one);
    const float clip_depth_direction = reverse_depth ? -1.0f : 1.0f;
    write(view_gpu_data, program_interface.clip_depth_direction_offset, as_span(clip_depth_direction));

    // Surface-line bias headroom (ULPs) and the depth-range mapping factor.
    // zero_to_one: window depth == ndc z (scale 1). minus_one_to_one: window =
    // 0.5*ndc + 0.5 (scale 2). The line shaders measure the depth ULP in
    // window space and convert with this factor.
    const float line_bias_margin    = m_debug_renderer.get_line_bias_margin();
    const float window_to_ndc_scale = reverse_depth ? 1.0f : 2.0f;
    write(view_gpu_data, program_interface.line_bias_margin_offset,    as_span(line_bias_margin));
    write(view_gpu_data, program_interface.window_to_ndc_scale_offset, as_span(window_to_ndc_scale));

    view_buffer_range.bytes_written(view_block_size);
    view_buffer_range.close();
    return view_buffer_range;
}

auto Debug_renderer_bucket::make_draw(const std::size_t vertex_byte_count, const std::size_t primitive_count) -> std::span<std::byte>
{
    constexpr std::size_t min_range_size = 8192; // TODO
    ERHE_VERIFY(!m_view_spans.empty());

    auto& buffer_client = uses_compute() ? m_vertex_ssbo_buffer.value() : m_line_vertex_buffer.value();

    // When the buffer_client is an SSBO (compute path), also clamp to the
    // block's reported size so MoltenVK's Metal argument validation holds.
    // See note in joint_buffer.cpp.
    const std::size_t ssbo_min_byte_count = uses_compute()
        ? m_debug_renderer.get_program_interface().line_vertex_buffer_block->get_size_bytes()
        : std::size_t{0};
    const std::size_t acquire_byte_count = std::max({vertex_byte_count, min_range_size, ssbo_min_byte_count});

    if (m_draws.empty() || m_start_new_draw) {
        m_start_new_draw = false;
        m_draws.emplace_back(Debug_draw_entry{
            .input_buffer_range = buffer_client.acquire(erhe::graphics::Ring_buffer_usage::CPU_write, acquire_byte_count),
            .draw_buffer_range  = erhe::graphics::Ring_buffer_range{},
            .view_buffer_range  = erhe::graphics::Ring_buffer_range{},
            .primitive_count    = 0
        });
        ++m_view_spans.back().end;
        ERHE_VERIFY(m_view_spans.back().end == m_draws.size());
    }
    if (m_draws.back().input_buffer_range.get_writable_byte_count() < vertex_byte_count) {
        m_draws.emplace_back(Debug_draw_entry{
            .input_buffer_range = buffer_client.acquire(erhe::graphics::Ring_buffer_usage::CPU_write, acquire_byte_count),
            .draw_buffer_range  = erhe::graphics::Ring_buffer_range{},
            .view_buffer_range  = erhe::graphics::Ring_buffer_range{},
            .primitive_count    = 0
        });
        ++m_view_spans.back().end;
        ERHE_VERIFY(m_view_spans.back().end == m_draws.size());
    }

    Debug_draw_entry& draw                = m_draws.back();
    std::size_t       buffer_range_offset = draw.input_buffer_range.get_written_byte_count();
    const auto        buffer_range_span   = draw.input_buffer_range.get_span();
    draw.input_buffer_range.bytes_written(vertex_byte_count);
    draw.primitive_count += primitive_count;
    draw.compute_dispatched = false;

    return buffer_range_span.subspan(buffer_range_offset, vertex_byte_count);
}

auto Debug_renderer_bucket::match(const Debug_renderer_config& config) const -> bool
{
    return m_config == config;
}

void Debug_renderer_bucket::clear()
{
    m_draws.clear();
    m_view_spans.clear();
}

void Debug_renderer_bucket::start_view(const View& view)
{
    if (!m_view_spans.empty()) {
        ERHE_VERIFY(m_view_spans.back().end == m_draws.size());
    }
    m_view_spans.push_back(
        {
            .views = std::vector<View>{view},
            .begin = m_draws.size(),
            .end   = m_draws.size()
        }
    );
    m_start_new_draw = true;
}

void Debug_renderer_bucket::start_view(std::span<const View> views)
{
    if (!m_view_spans.empty()) {
        ERHE_VERIFY(m_view_spans.back().end == m_draws.size());
    }
    m_view_spans.push_back(
        {
            .views = std::vector<View>{views.begin(), views.end()},
            .begin = m_draws.size(),
            .end   = m_draws.size()
        }
    );
    m_start_new_draw = true;
}

[[nodiscard]] auto vertex_count_from_primitive_count(
    std::size_t                    primitive_count,
    erhe::graphics::Primitive_type primitive_type
) -> std::size_t
{
    switch (primitive_type) {
        case erhe::graphics::Primitive_type::point:    return primitive_count;
        case erhe::graphics::Primitive_type::line:     return 2 * primitive_count;
        case erhe::graphics::Primitive_type::triangle: return 3 * primitive_count;
        default: {
            ERHE_FATAL("TODO");
            return 0;
        }
    }
}

void Debug_renderer_bucket::dispatch_compute(erhe::graphics::Compute_command_encoder& encoder)
{
    if (!uses_compute()) {
        return;
    }

    // The compute tier is only ever derived for the line primitive (the
    // wide-line expansion); triangle / point buckets never reach here.
    ERHE_VERIFY(m_config.primitive_type == erhe::graphics::Primitive_type::line);

    const std::size_t triangle_vertex_stride = m_debug_renderer.get_program_interface().triangle_vertex_format.streams.front().stride;

    for (Debug_draw_view_span& view_span : m_view_spans) {
        const std::size_t view_count = view_span.views.size();

        // Always populate a per-draw view UBO range and hold it on the
        // Debug_draw_entry. Single-view fills stride_per_view with the
        // draw's primitive_count (matching multiview's layout) so the
        // vertex shader's gl_VertexID + c_view_index * stride_per_view
        // expression resolves to gl_VertexID with c_view_index = 0.
        // render() re-binds the same per-draw range on the graphics
        // encoder so the fragment shader can read the per-eye viewport.
        for (size_t i = view_span.begin; i < view_span.end; ++i) {
            Debug_draw_entry& draw = m_draws[i];
            ERHE_VERIFY(draw.primitive_count > 0);

            draw.view_buffer_range = update_view_buffer(view_span.views, draw.primitive_count);
            m_view_buffer.bind(encoder, draw.view_buffer_range);

            draw.input_buffer_range.close();
            m_vertex_ssbo_buffer->bind(encoder, draw.input_buffer_range);

            // Triangle SSBO must hold view_count contiguous slabs of
            // 6 * primitive_count vertices each. Single-view: 1 slab.
            const std::size_t per_view_triangle_byte_count = 6 * draw.primitive_count * triangle_vertex_stride;
            const std::size_t triangle_byte_count          = view_count * per_view_triangle_byte_count;
            // See note in joint_buffer.cpp.
            const std::size_t triangle_acquire_byte_count = std::max(
                triangle_byte_count,
                m_debug_renderer.get_program_interface().triangle_vertex_buffer_block->get_size_bytes()
            );

            draw.draw_buffer_range = m_triangle_vertex_buffer->acquire(erhe::graphics::Ring_buffer_usage::GPU_access, triangle_acquire_byte_count);
            ERHE_VERIFY(draw.draw_buffer_range.get_buffer() != nullptr);
            draw.draw_buffer_range.bytes_gpu_used(triangle_byte_count);
            draw.draw_buffer_range.close();

            m_triangle_vertex_buffer->bind(encoder, draw.draw_buffer_range);

            encoder.dispatch_compute(draw.primitive_count, 1, 1);

            draw.input_buffer_range.release();
            draw.compute_dispatched = true;
        }
    }
}

void Debug_renderer_bucket::release_buffers()
{
    for (Debug_draw_entry& draw : m_draws) {
        if (uses_compute()) {
            draw.draw_buffer_range.release();
        } else {
            draw.input_buffer_range.release();
        }
        // Multiview path holds a per-draw view UBO range across the
        // compute and render encoders; release here once both have run.
        if (draw.view_buffer_range.get_buffer() != nullptr) {
            draw.view_buffer_range.release();
        }
    }
    clear();
}

void Debug_renderer_bucket::render(
    erhe::graphics::Render_command_encoder& render_encoder,
    const erhe::graphics::Render_pass&      render_pass,
    bool                                    draw_hidden,
    bool                                    draw_visible,
    bool                                    multiview
)
{
    // Nothing was submitted to this bucket this frame (m_draws is cleared at
    // end_frame, while the bucket object persists for the Debug_renderer
    // lifetime). Return before any work -- and, crucially, before the direct
    // path's ERHE_VERIFY(!multiview): the shared Debug_renderer renders both
    // the single-view desktop viewport and the multiview headset, and a
    // triangle / thin-line (tier == simple) bucket left over from a desktop
    // frame would otherwise trip that assert during the headset's
    // multiview render. A genuine multiview direct-path submission still
    // asserts loudly below.
    if (m_draws.empty()) {
        return;
    }

    if (uses_compute()) {
        // Compute path. Both single-view and multiview read pre-transformed
        // triangle vertices from the triangle SSBO (binding 1, read-only
        // declaration) and per-eye viewport from the view UBO (binding 3).
        // The multiview vertex shader resolves c_view_index to
        // gl_ViewIndex; the single-view variant uses 0u. The Base_render_pipeline
        // cache (m_pipeline_visible/hidden) is keyed on single-view shader
        // stages, so both compiles bypass it via set_render_pipeline_state()
        // -- the encoder's internal pipeline cache handles VkPipeline reuse.
        const Debug_renderer_program_interface& pi = m_debug_renderer.get_program_interface();
        erhe::graphics::Shader_stages* shader_stages = multiview
            ? pi.multiview_graphics_shader_stages.get()
            : pi.graphics_shader_stages.get();
        if ((shader_stages == nullptr) || !shader_stages->is_valid()) {
            return;
        }

        auto render_compute_draws = [&](const bool visible, erhe::graphics::Base_render_pipeline& pipeline) {
            // Temp pipeline state mirrors the cached pipeline's
            // depth/stencil/blend etc. but overrides shader_stages.
            // pipeline.data is Render_pipeline_create_info (the cache
            // key); it has no scissor field, so the temp
            // Render_pipeline_data leaves scissor default-initialised.
            erhe::graphics::Render_pipeline_state temp_state{
                erhe::graphics::Render_pipeline_data{
                    .debug_label          = pipeline.data.debug_label,
                    .shader_stages        = shader_stages,
                    // Empty (no attributes) but non-null: OpenGL core
                    // profile requires a non-default VAO bound for the
                    // draw to fire. The triangles are read from the SSBO
                    // via gl_VertexID, not through the input assembler.
                    .vertex_input         = m_debug_renderer.get_empty_vertex_input(),
                    .input_assembly       = pipeline.data.input_assembly,
                    .multisample          = pipeline.data.multisample,
                    .viewport_depth_range = pipeline.data.viewport_depth_range,
                    .rasterization        = pipeline.data.rasterization,
                    .depth_stencil        = pipeline.data.depth_stencil,
                    .color_blend          = visible ? pi.color_blend_visible : pi.color_blend_xray
                }
            };

            render_encoder.set_bind_group_layout(pi.graphics_bind_group_layout.get());
            render_encoder.set_render_pipeline_state(temp_state);

            for (const Debug_draw_entry& draw : m_draws) {
                if (!draw.compute_dispatched) {
                    continue;
                }
                // Re-bind the per-draw view UBO carried across from
                // dispatch_compute(): the vertex shader reads
                // stride_per_view from it, and the fragment shader reads
                // view.cameras[c_view_index].viewport.xy.
                m_view_buffer.bind(render_encoder, draw.view_buffer_range);
                // Triangle SSBO at binding 1 (read-only declaration).
                // Same descriptor binding the compute side wrote to;
                // triangle_vertex_buffer_read_block declares it readonly
                // so the vertex shader can index without colliding with
                // the compute's writeonly declaration.
                m_triangle_vertex_buffer->bind(render_encoder, draw.draw_buffer_range);
                render_encoder.draw_primitives(
                    pipeline.data.input_assembly.primitive_topology,
                    0,
                    static_cast<uint32_t>(6 * draw.primitive_count)
                );
            }
        };

        if (draw_hidden && m_config.draw_hidden) {
            render_compute_draws(false, m_pipeline_hidden);
        }
        if (draw_visible && m_config.draw_visible) {
            render_compute_draws(true, m_pipeline_visible);
        }
    } else {
        // Direct path: close input ranges (upload CPU data to GPU), then render
        // GL_LINES / GL_TRIANGLES / GL_POINTS directly from the vertex buffer.
        // This serves the simple-line and geometry-line tiers and every
        // triangle / point bucket (which never use compute).
        // The direct path is single-view only -- its update_view_buffer
        // call below passes stride_per_view = 0 and the geometry-shader
        // pipeline reads cameras[c_view_index] with c_view_index = 0u. If
        // a caller ever opts into multiview here, both eyes would render
        // from cameras[0]. Fail loudly so the regression is caught at the
        // call site instead of silently degrading the right-eye output.
        ERHE_VERIFY(!multiview);
        // Close all input ranges first (can only be done once)
        for (Debug_draw_entry& draw : m_draws) {
            if (draw.primitive_count > 0 && !draw.input_buffer_range.is_closed()) {
                draw.input_buffer_range.close();
            }
        }

        auto render_line_draws = [&](const bool visible, erhe::graphics::Base_render_pipeline& pipeline) {
            const Debug_renderer_program_interface& pi = m_debug_renderer.get_program_interface();
            // graphics_shader_stages is built only on the compute path
            // (see Debug_renderer_program_interface ctor). On GL 4.1 / Metal,
            // where use_compute is false, that pointer is null and binding
            // it would issue glUseProgram(0). Pick the matching direct-path
            // shader instead -- geometry-expanded wide lines when the bucket's
            // tier is geometry, else the simple shader. The simple shader
            // (line_simple.{vert,frag}) only transforms position and passes
            // color through, so it serves line, triangle and point topologies
            // alike from the shared line_vertex_format.
            erhe::graphics::Shader_stages* line_shader_stages =
                uses_geometry()
                    ? pi.geometry_shader_stages.get()
                    : pi.line_shader_stages.get();
            erhe::graphics::Render_pipeline* p = pipeline.get_pipeline_for(
                render_pass.get_descriptor(),
                visible ? &pi.color_blend_visible : &pi.color_blend_xray,
                line_shader_stages,
                m_debug_renderer.get_line_vertex_input(),
                &pi.line_vertex_format
            );
            if (p == nullptr) {
                return;
            }
            render_encoder.set_bind_group_layout(pi.bind_group_layout.get());
            render_encoder.set_render_pipeline(*p);
            // Triangle pipelines enable depth bias (polygon offset) so the
            // coplanar fill wins the depth test against the surface it
            // overlays. Reverse depth (near = 1) needs a positive offset to
            // move toward the viewer; standard depth (near = 0) a negative one.
            if (m_shader_key.primitive_type == erhe::graphics::Primitive_type::triangle) {
                const bool  reverse_depth = (m_graphics_device.get_info().coordinate_conventions.native_depth_range == erhe::math::Depth_range::zero_to_one);
                const float bias_sign     = reverse_depth ? 1.0f : -1.0f;
                render_encoder.set_depth_bias(bias_sign * 4.0f, bias_sign * 1.0f, 0.0f);
            }
            for (Debug_draw_view_span& view_span : m_view_spans) {
                // Non-compute path is single-view only (the multiview
                // pipeline reads from the triangle SSBO produced by
                // the compute path, so the simple-line / geometry
                // fallback never goes through multiview).
                erhe::graphics::Ring_buffer_range view_buffer_range = update_view_buffer(view_span.views, /*primitive_count_for_stride*/ 0);
                m_view_buffer.bind(render_encoder, view_buffer_range);

                for (size_t i = view_span.begin; i < view_span.end; ++i) {
                    Debug_draw_entry& draw = m_draws[i];
                    if (draw.primitive_count == 0) {
                        continue;
                    }
                    erhe::graphics::Buffer* line_vertex_buffer       = draw.input_buffer_range.get_buffer()->get_buffer();
                    size_t                  line_vertex_buffer_offset = draw.input_buffer_range.get_byte_start_offset_in_buffer();
                    render_encoder.set_vertex_buffer(line_vertex_buffer, line_vertex_buffer_offset, 0);
                    render_encoder.draw_primitives(
                        pipeline.data.input_assembly.primitive_topology,
                        0,
                        vertex_count_from_primitive_count(draw.primitive_count, m_shader_key.primitive_type)
                    );
                }
                view_buffer_range.release();
            }
        };

        if (draw_hidden && m_config.draw_hidden) {
            render_line_draws(false, m_pipeline_hidden);
        }
        if (draw_visible && m_config.draw_visible) {
            render_line_draws(true, m_pipeline_visible);
        }
    }
}

} // namespace erhe::renderer
