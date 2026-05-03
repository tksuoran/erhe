#include "erhe_renderer/debug_renderer.hpp"
#include "erhe_renderer/debug_renderer_bucket.hpp"

#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
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

auto Debug_renderer_bucket::Debug_renderer_bucket::make_pipeline(const bool visible) -> erhe::graphics::Lazy_render_pipeline
{
    const bool reverse_depth = (m_graphics_device.get_info().coordinate_conventions.native_depth_range == erhe::math::Depth_range::zero_to_one);
    using namespace erhe::graphics;

    const auto& program_interface = m_debug_renderer.get_program_interface();

    // Three tiers: compute (triangles) > geometry shader (GL_LINES + geom expand) > simple (GL_LINES)
    // thin_lines config forces simple path regardless of capability
    Shader_stages*       shader_stages;
    Vertex_input_state*  vertex_input;
    Input_assembly_state input_assembly;
    if (m_use_compute && !m_config.thin_lines) {
        shader_stages  = program_interface.graphics_shader_stages.get();
        vertex_input   = m_debug_renderer.get_vertex_input();
        input_assembly = Input_assembly_state::triangle;
    } else if (m_use_geometry_shader && !m_config.thin_lines) {
        shader_stages  = program_interface.geometry_shader_stages.get();
        vertex_input   = m_debug_renderer.get_line_vertex_input();
        input_assembly = Input_assembly_state::line;
    } else {
        shader_stages  = program_interface.line_shader_stages.get();
        vertex_input   = m_debug_renderer.get_line_vertex_input();
        input_assembly = Input_assembly_state::line;
    }

    const Compare_operation depth_compare_op0 = visible ? Compare_operation::less : Compare_operation::greater_or_equal;
    const Compare_operation depth_compare_op  = reverse_depth ? reverse(depth_compare_op0) : depth_compare_op0;
    return Lazy_render_pipeline{
        m_graphics_device,
        Render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"Line Renderer"},
            .shader_stages  = shader_stages,
            .vertex_input   = vertex_input,
            .input_assembly = input_assembly,
            .rasterization  = Rasterization_state::cull_mode_none,
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

            // RGB blend factors use CONSTANT_COLOR rather than CONSTANT_ALPHA
            // because the Vulkan portability subset on MoltenVK rejects
            // CONSTANT_ALPHA / ONE_MINUS_CONSTANT_ALPHA in the color channel
            // (VUID-VkPipelineColorBlendAttachmentState-constantAlphaColorBlendFactors-04454).
            // Setting the blend constant's RGB equal to its alpha makes
            // CONSTANT_COLOR produce the same blend as CONSTANT_ALPHA would.
            .color_blend = visible ? Color_blend_state::color_blend_premultiplied : Color_blend_state{
                .enabled  = true,
                .rgb      = {
                    .equation_mode      = Blend_equation_mode::func_add,
                    .source_factor      = Blending_factor::constant_color,
                    .destination_factor = Blending_factor::one_minus_constant_color
                },
                .alpha    = {
                    .equation_mode      = Blend_equation_mode::func_add,
                    .source_factor      = Blending_factor::constant_alpha,
                    .destination_factor = Blending_factor::one_minus_constant_alpha
                },
                .constant = { 0.1f, 0.1f, 0.1f, 0.1f },
            }
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
    , m_use_compute       {debug_renderer.use_compute()}
    , m_use_geometry_shader{debug_renderer.use_geometry_shader()}
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
    if (m_use_compute) {
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

auto Debug_renderer_bucket::update_view_buffer(const View& view) -> erhe::graphics::Ring_buffer_range
{
    const Debug_renderer_program_interface& program_interface = m_debug_renderer.get_program_interface();
    const erhe::graphics::Shader_resource&  view_block        = *program_interface.view_block.get();
    const std::size_t                       view_block_size   = view_block.get_size_bytes();
    erhe::graphics::Ring_buffer_range       view_buffer_range = m_view_buffer.acquire(erhe::graphics::Ring_buffer_usage::CPU_write, view_block_size);
    const std::span<std::byte>              view_gpu_data     = view_buffer_range.get_span();

    // Zero the whole block so unused cameras[k>0] entries (and any
    // padding) read as zeros on the GPU. MoltenVK descriptor validation
    // requires the bound range to cover the full block size; leaving
    // tail bytes uninitialised here would expose ring-buffer leftovers
    // from a prior frame to the shader.
    std::memset(view_gpu_data.data(), 0, view_gpu_data.size_bytes());

    using erhe::graphics::write;
    using erhe::graphics::as_span;

    // Single-view: populate cameras[0] only. Phase 2 will extend this
    // to write all view_count entries when start_view supplies a
    // multi-camera view.
    write(view_gpu_data, program_interface.view_camera_clip_from_world_offset,        as_span(view.clip_from_world       ));
    write(view_gpu_data, program_interface.view_camera_viewport_offset,               as_span(view.viewport              ));
    write(view_gpu_data, program_interface.view_camera_fov_offset,                    as_span(view.fov_sides             ));
    write(view_gpu_data, program_interface.view_camera_view_position_in_world_offset, as_span(view.view_position_in_world));

    const uint32_t view_count_uint      = 1u;
    const uint32_t stride_per_view_uint = 0u; // unused in single-view; Phase 2 fills this for multiview
    write(view_gpu_data, program_interface.view_count_offset,      as_span(view_count_uint     ));
    write(view_gpu_data, program_interface.stride_per_view_offset, as_span(stride_per_view_uint));

    const bool  top_left  = (m_graphics_device.get_info().coordinate_conventions.framebuffer_origin == erhe::math::Framebuffer_origin::top_left);
    const float vp_y_sign = top_left ? -1.0f : 1.0f;
    write(view_gpu_data, program_interface.vp_y_sign_offset, as_span(vp_y_sign));
    // padding0 already zero-filled by the memset above.

    view_buffer_range.bytes_written(view_block_size);
    view_buffer_range.close();
    return view_buffer_range;
}

auto Debug_renderer_bucket::make_draw(std::size_t vertex_byte_count, std::size_t primitive_count) -> std::span<std::byte>
{
    constexpr std::size_t min_range_size = 8192; // TODO
    ERHE_VERIFY(!m_view_spans.empty());

    auto& buffer_client = m_use_compute ? m_vertex_ssbo_buffer.value() : m_line_vertex_buffer.value();

    // When the buffer_client is an SSBO (compute path), also clamp to the
    // block's reported size so MoltenVK's Metal argument validation holds.
    // See note in joint_buffer.cpp.
    const std::size_t ssbo_min_byte_count = m_use_compute
        ? m_debug_renderer.get_program_interface().line_vertex_buffer_block->get_size_bytes()
        : std::size_t{0};
    const std::size_t acquire_byte_count = std::max({vertex_byte_count, min_range_size, ssbo_min_byte_count});

    if (m_draws.empty() || m_start_new_draw) {
        m_start_new_draw = false;
        m_draws.emplace_back(
            buffer_client.acquire(erhe::graphics::Ring_buffer_usage::CPU_write, acquire_byte_count),
            erhe::graphics::Ring_buffer_range{},
            0
        );
        ++m_view_spans.back().end;
        ERHE_VERIFY(m_view_spans.back().end == m_draws.size());
    }
    if (m_draws.back().input_buffer_range.get_writable_byte_count() < vertex_byte_count) {
        m_draws.emplace_back(
            buffer_client.acquire(
                erhe::graphics::Ring_buffer_usage::CPU_write,
                acquire_byte_count
            ),
            erhe::graphics::Ring_buffer_range{},
            0
        );
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
            .view  = view,
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
    if (!m_use_compute) {
        return;
    }

    if (m_config.primitive_type != erhe::graphics::Primitive_type::line) {
        ERHE_FATAL("TODO");
    }

    const std::size_t triangle_vertex_stride = m_debug_renderer.get_program_interface().triangle_vertex_format.streams.front().stride;

    for (Debug_draw_view_span& view_span : m_view_spans) {
        erhe::graphics::Ring_buffer_range view_buffer_range = update_view_buffer(view_span.view);
        m_view_buffer.bind(encoder, view_buffer_range);

        for (size_t i = view_span.begin; i < view_span.end; ++i) {
            Debug_draw_entry& draw = m_draws[i];
            ERHE_VERIFY(draw.primitive_count > 0);

            draw.input_buffer_range.close();
            m_vertex_ssbo_buffer->bind(encoder, draw.input_buffer_range);

            const std::size_t triangle_byte_count = 6 * draw.primitive_count * triangle_vertex_stride;
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
        view_buffer_range.release();
    }
}

void Debug_renderer_bucket::release_buffers()
{
    for (Debug_draw_entry& draw : m_draws) {
        if (m_use_compute) {
            draw.draw_buffer_range.release();
        } else {
            draw.input_buffer_range.release();
        }
    }
    clear();
}

void Debug_renderer_bucket::render(erhe::graphics::Render_command_encoder& render_encoder, const erhe::graphics::Render_pass& render_pass, bool draw_hidden, bool draw_visible)
{
    if (m_use_compute) {
        // Compute path: render triangles from compute-generated triangle vertex buffer
        auto render_compute_draws = [&](erhe::graphics::Lazy_render_pipeline& pipeline) {
            erhe::graphics::Render_pipeline* p = pipeline.get_pipeline_for(render_pass.get_descriptor());
            if (p == nullptr) {
                return;
            }
            render_encoder.set_bind_group_layout(m_debug_renderer.get_program_interface().bind_group_layout.get());
            render_encoder.set_render_pipeline(*p);
            for (const Debug_draw_entry& draw : m_draws) {
                if (!draw.compute_dispatched) {
                    continue;
                }
                erhe::graphics::Buffer* triangle_vertex_buffer       = draw.draw_buffer_range.get_buffer()->get_buffer();
                size_t                  triangle_vertex_buffer_offset = draw.draw_buffer_range.get_byte_start_offset_in_buffer();
                render_encoder.set_vertex_buffer(triangle_vertex_buffer, triangle_vertex_buffer_offset, 0);
                render_encoder.draw_primitives(
                    pipeline.data.input_assembly.primitive_topology,
                    0,
                    6 * draw.primitive_count
                );
            }
        };

        if (draw_hidden && m_config.draw_hidden) {
            render_compute_draws(m_pipeline_hidden);
        }
        if (draw_visible && m_config.draw_visible) {
            render_compute_draws(m_pipeline_visible);
        }
    } else {
        // Non-compute path: close input ranges (upload CPU data to GPU), then render GL_LINES
        // Close all input ranges first (can only be done once)
        for (Debug_draw_entry& draw : m_draws) {
            if (draw.primitive_count > 0 && !draw.input_buffer_range.is_closed()) {
                draw.input_buffer_range.close();
            }
        }

        auto render_line_draws = [&](erhe::graphics::Lazy_render_pipeline& pipeline) {
            erhe::graphics::Render_pipeline* p = pipeline.get_pipeline_for(render_pass.get_descriptor());
            if (p == nullptr) {
                return;
            }
            render_encoder.set_bind_group_layout(m_debug_renderer.get_program_interface().bind_group_layout.get());
            render_encoder.set_render_pipeline(*p);
            for (Debug_draw_view_span& view_span : m_view_spans) {
                erhe::graphics::Ring_buffer_range view_buffer_range = update_view_buffer(view_span.view);
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
                        2 * draw.primitive_count
                    );
                }
                view_buffer_range.release();
            }
        };

        if (draw_hidden && m_config.draw_hidden) {
            render_line_draws(m_pipeline_hidden);
        }
        if (draw_visible && m_config.draw_visible) {
            render_line_draws(m_pipeline_visible);
        }
    }
}

} // namespace erhe::renderer
