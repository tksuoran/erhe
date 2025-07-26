#include "erhe_renderer/debug_renderer.hpp"
#include "erhe_renderer/debug_renderer_bucket.hpp"

#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_verify/verify.hpp"

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
        (lhs.draw_hidden       == rhs.draw_hidden      );
}

auto Debug_renderer_bucket::Debug_renderer_bucket::make_pipeline(const bool visible, const bool reverse_depth) -> erhe::graphics::Render_pipeline_state
{
    using namespace erhe::graphics;

    Shader_stages* const graphics_shader_stages = m_debug_renderer.get_program_interface().graphics_shader_stages.get();

    const Compare_operation depth_compare_op0 = visible ? Compare_operation::less : Compare_operation::greater_or_equal;
    const Compare_operation depth_compare_op  = reverse_depth ? reverse(depth_compare_op0) : depth_compare_op0;
    return Render_pipeline_state{
        Render_pipeline_data{
            .name           = "Line Renderer",
            .shader_stages  = graphics_shader_stages,
            .vertex_input   = m_debug_renderer.get_vertex_input(),
            .input_assembly = Input_assembly_state::triangle,
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

            .color_blend = visible ? Color_blend_state::color_blend_premultiplied : Color_blend_state{
                .enabled  = true,
                .rgb      = {
                    .equation_mode      = Blend_equation_mode::func_add,
                    .source_factor      = Blending_factor::constant_alpha,
                    .destination_factor = Blending_factor::one_minus_constant_alpha
                },
                .alpha    = {
                    .equation_mode      = Blend_equation_mode::func_add,
                    .source_factor      = Blending_factor::constant_alpha,
                    .destination_factor = Blending_factor::one_minus_constant_alpha
                },
                .constant = { 0.0f, 0.0f, 0.0f, 0.1f },
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
    , m_vertex_ssbo_buffer{ // compute read
        graphics_device,
        erhe::graphics::Buffer_target::storage,
        "Debug_renderer_bucket::m_vertex_ssbo_buffer",
        debug_renderer.get_program_interface().line_vertex_buffer_block->get_binding_point()
    }
    , m_triangle_vertex_buffer{ // compute write, vs read
        graphics_device,
        erhe::graphics::Buffer_target::storage,
        "Debug_renderer_bucket::m_triangle_vertex_buffer",
        debug_renderer.get_program_interface().triangle_vertex_buffer_block->get_binding_point()
    }
    , m_config            {config}
    , m_pipeline_visible  {make_pipeline(true )}
    , m_pipeline_hidden   {make_pipeline(false)}
{
}

auto Debug_renderer_bucket::make_draw(std::size_t vertex_byte_count, std::size_t primitive_count) -> std::span<std::byte>
{
    constexpr std::size_t min_range_size = 8192; // TODO
    if (m_draws.empty()) {
        m_draws.emplace_back(
            m_vertex_ssbo_buffer.acquire(
                erhe::graphics::Ring_buffer_usage::CPU_write,
                std::max(vertex_byte_count, min_range_size)
            ),
            erhe::graphics::Buffer_range{},
            0
        );
    }
    if (m_draws.back().input_buffer_range.get_writable_byte_count() < vertex_byte_count) {
        m_draws.emplace_back(
            m_vertex_ssbo_buffer.acquire(
                erhe::graphics::Ring_buffer_usage::CPU_write,
                std::max(vertex_byte_count, min_range_size)
            ),
            erhe::graphics::Buffer_range{},
            0
        );
    }

    Debug_draw_entry& draw                = m_draws.back();
    std::size_t       buffer_range_offset = draw.input_buffer_range.get_written_byte_count();
    const auto        buffer_range_span   = draw.input_buffer_range.get_span();
    draw.input_buffer_range.bytes_written(vertex_byte_count);
    draw.primitive_count += primitive_count;
    return buffer_range_span.subspan(buffer_range_offset, vertex_byte_count);
}

auto Debug_renderer_bucket::match(const Debug_renderer_config& config) const -> bool
{
    return m_config == config;
}

void Debug_renderer_bucket::clear()
{
    m_draws.clear();
}

[[nodiscard]] auto vertex_count_from_primitive_count(std::size_t primitive_count, gl::Primitive_type primitive_type) -> std::size_t
{
    switch (primitive_type) {
        case gl::Primitive_type::points:    return primitive_count;
        case gl::Primitive_type::lines:     return 2 * primitive_count;
        case gl::Primitive_type::triangles: return 3 * primitive_count;
        default: {
            ERHE_FATAL("TODO");
            return 0;
        }
    }
}

void Debug_renderer_bucket::dispatch_compute(erhe::graphics::Compute_command_encoder& encoder)
{
    if (m_config.primitive_type != gl::Primitive_type::lines) {
        ERHE_FATAL("TODO");
    }

    const std::size_t triangle_vertex_stride = m_debug_renderer.get_program_interface().triangle_vertex_format.streams.front().stride;

    for (Debug_draw_entry& draw : m_draws) {
        ERHE_VERIFY(draw.primitive_count > 0);

        draw.input_buffer_range.close();
        m_vertex_ssbo_buffer.bind(encoder, draw.input_buffer_range);

        const std::size_t triangle_byte_count = 6 * draw.primitive_count * triangle_vertex_stride;

        // TODO Instead of open(), close() there should be a dedicated
        //      API for allocating GPU write range; Writing to that
        //      range potentially needs gl::wait_sync() if there are
        //      previous GPU reads, and possibly also
        //      gl::memory_barrier(gl::Memory_barrier_mask::shader_storage_barrier_bit)
        draw.draw_buffer_range = m_triangle_vertex_buffer.acquire(erhe::graphics::Ring_buffer_usage::GPU_access, triangle_byte_count);
        draw.draw_buffer_range.bytes_written(triangle_byte_count);
        draw.draw_buffer_range.close();

        m_triangle_vertex_buffer.bind(encoder, draw.draw_buffer_range);

        encoder.dispatch_compute(draw.primitive_count, 1, 1);

        draw.input_buffer_range.release();
    }
}

void Debug_renderer_bucket::release_buffers()
{
    for (Debug_draw_entry& draw : m_draws) {
        draw.draw_buffer_range.release();
    }
    m_draws.clear();
}

void Debug_renderer_bucket::render(erhe::graphics::Render_command_encoder& render_encoder, bool draw_hidden, bool draw_visible)
{
    if (draw_hidden && m_config.draw_hidden) {
        render_encoder.set_render_pipeline_state(m_pipeline_hidden);
        for (const Debug_draw_entry& draw : m_draws) {
            erhe::graphics::Buffer* triangle_vertex_buffer        = draw.draw_buffer_range.get_buffer()->get_buffer();
            size_t                  triangle_vertex_buffer_offset = draw.draw_buffer_range.get_byte_start_offset_in_buffer();
            render_encoder.set_vertex_buffer(triangle_vertex_buffer, triangle_vertex_buffer_offset, 0);
            render_encoder.draw_primitives(
                m_pipeline_hidden.data.input_assembly.primitive_topology,
                0,
                static_cast<GLint>(6 * draw.primitive_count)
            );
        }
    }

    if (draw_visible && m_config.draw_visible) {
        render_encoder.set_render_pipeline_state(m_pipeline_visible);
        for (const Debug_draw_entry& draw : m_draws) {
            erhe::graphics::Buffer* triangle_vertex_buffer        = draw.draw_buffer_range.get_buffer()->get_buffer();
            size_t                  triangle_vertex_buffer_offset = draw.draw_buffer_range.get_byte_start_offset_in_buffer();
            render_encoder.set_vertex_buffer(triangle_vertex_buffer, triangle_vertex_buffer_offset, 0);
            render_encoder.draw_primitives(
                m_pipeline_visible.data.input_assembly.primitive_topology,
                0,
                static_cast<GLint>(6 * draw.primitive_count)
            );
        }
    }
}

} // namespace erhe::renderer
