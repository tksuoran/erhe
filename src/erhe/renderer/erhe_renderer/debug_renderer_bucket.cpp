#include "erhe_renderer/debug_renderer.hpp"
#include "erhe_renderer/debug_renderer_bucket.hpp"

#include "erhe_gl/wrapper_functions.hpp"
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
        (lhs.draw_hidden       == rhs.draw_hidden      ) &&
        (lhs.reverse_depth     == rhs.reverse_depth    );
}

auto Debug_renderer_bucket::Debug_renderer_bucket::make_pipeline(const bool visible) -> erhe::graphics::Pipeline
{
    erhe::graphics::Shader_stages* const graphics_shader_stages = m_debug_renderer.get_program_interface().graphics_shader_stages.get();

    const gl::Depth_function depth_compare_op0 = visible ? gl::Depth_function::less : gl::Depth_function::gequal;
    const gl::Depth_function depth_compare_op  = m_config.reverse_depth ? erhe::graphics::reverse(depth_compare_op0) : depth_compare_op0;
    return erhe::graphics::Pipeline{
        erhe::graphics::Pipeline_data{
            .name           = "Line Renderer",
            .shader_stages  = graphics_shader_stages,
            .vertex_input   = m_debug_renderer.get_vertex_input(),
            .input_assembly = erhe::graphics::Input_assembly_state::triangles,
            .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
            .depth_stencil  = {
                .depth_test_enable   = true,
                .depth_write_enable  = false,
                .depth_compare_op    = depth_compare_op,
                .stencil_test_enable = true,
                .stencil_front = {
                    .stencil_fail_op = gl::Stencil_op::keep,
                    .z_fail_op       = gl::Stencil_op::keep,
                    .z_pass_op       = gl::Stencil_op::replace,
                    .function        = gl::Stencil_function::greater, //gequal,
                    .reference       = m_config.stencil_reference,
                    .test_mask       = visible ? 0b01111111u : 0b11111111u,
                    .write_mask      = 0b01111111u
                },
                .stencil_back = {
                    .stencil_fail_op = gl::Stencil_op::keep,
                    .z_fail_op       = gl::Stencil_op::keep,
                    .z_pass_op       = gl::Stencil_op::replace,
                    .function        = gl::Stencil_function::greater, //gequal,
                    .reference       = m_config.stencil_reference,
                    .test_mask       = visible ? 0b01111111u : 0b11111111u,
                    .write_mask      = 0b01111111u
                },
            },

            .color_blend = visible ? erhe::graphics::Color_blend_state::color_blend_premultiplied : erhe::graphics::Color_blend_state{
                .enabled  = true,
                .rgb      = {
                    .equation_mode      = gl::Blend_equation_mode::func_add,
                    .source_factor      = gl::Blending_factor::constant_alpha,
                    .destination_factor = gl::Blending_factor::one_minus_constant_alpha
                },
                .alpha    = {
                    .equation_mode      = gl::Blend_equation_mode::func_add,
                    .source_factor      = gl::Blending_factor::constant_alpha,
                    .destination_factor = gl::Blending_factor::one_minus_constant_alpha
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
    , m_vertex_ssbo_buffer{
        graphics_device,
        "Debug_renderer_bucket::m_vertex_ssbo_buffer",
        gl::Buffer_target::shader_storage_buffer, // for compute bind range
        debug_renderer.get_program_interface().line_vertex_buffer_block->binding_point()
    }
    , m_triangle_vertex_buffer{
        graphics_device,
        "Debug_renderer_bucket::m_triangle_vertex_buffer",
        gl::Buffer_target::shader_storage_buffer, // for compute bind range
        debug_renderer.get_program_interface().triangle_vertex_buffer_block->binding_point()
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

void Debug_renderer_bucket::dispatch_compute()
{
    if (m_config.primitive_type != gl::Primitive_type::lines) {
        ERHE_FATAL("TODO");
    }

    const std::size_t triangle_vertex_stride = m_debug_renderer.get_program_interface().triangle_vertex_format.streams.front().stride;

    for (Debug_draw_entry& draw : m_draws) {
        ERHE_VERIFY(draw.primitive_count > 0);

        draw.input_buffer_range.close();
        m_vertex_ssbo_buffer.bind(draw.input_buffer_range);

        const std::size_t triangle_byte_count = 6 * draw.primitive_count * triangle_vertex_stride;

        // TODO Instead of open(), close() there should be a dedicated
        //      API for allocating GPU write range; Writing to that
        //      range potentially needs gl::wait_sync() if there are
        //      previous GPU reads, and possibly also
        //      gl::memory_barrier(gl::Memory_barrier_mask::shader_storage_barrier_bit)
        draw.draw_buffer_range = m_triangle_vertex_buffer.acquire(erhe::graphics::Ring_buffer_usage::GPU_access, triangle_byte_count);
        draw.draw_buffer_range.bytes_written(triangle_byte_count);
        draw.draw_buffer_range.close();

        m_triangle_vertex_buffer.bind(draw.draw_buffer_range);

        gl::dispatch_compute(static_cast<unsigned int>(draw.primitive_count), 1, 1);

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

void Debug_renderer_bucket::render(bool draw_hidden, bool draw_visible)
{
    if (draw_hidden && m_config.draw_hidden) {
        m_graphics_device.opengl_state_tracker.execute(m_pipeline_hidden);
        for (const Debug_draw_entry& draw : m_draws) {
            erhe::graphics::Buffer* triangle_vertex_buffer        = draw.draw_buffer_range.get_buffer()->get_buffer();
            size_t                  triangle_vertex_buffer_offset = draw.draw_buffer_range.get_byte_start_offset_in_buffer();
            m_graphics_device.opengl_state_tracker.vertex_input.set_vertex_buffer(
                0,
                triangle_vertex_buffer,
                triangle_vertex_buffer_offset
            );

            gl::draw_arrays(
                m_pipeline_hidden.data.input_assembly.primitive_topology,
                0,
                static_cast<GLint>(6 * draw.primitive_count)
            );
        }
    }

    if (draw_visible && m_config.draw_visible) {
        m_graphics_device.opengl_state_tracker.execute(m_pipeline_visible);
        for (const Debug_draw_entry& draw : m_draws) {
            erhe::graphics::Buffer* triangle_vertex_buffer        = draw.draw_buffer_range.get_buffer()->get_buffer();
            size_t                  triangle_vertex_buffer_offset = draw.draw_buffer_range.get_byte_start_offset_in_buffer();
            m_graphics_device.opengl_state_tracker.vertex_input.set_vertex_buffer(
                0,
                triangle_vertex_buffer,
                triangle_vertex_buffer_offset
            );
            gl::draw_arrays(
                m_pipeline_visible.data.input_assembly.primitive_topology,
                0,
                static_cast<GLint>(6 * draw.primitive_count)
            );
        }
    }
}

} // namespace erhe::renderer
