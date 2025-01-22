#include "erhe_renderer/line_renderer.hpp"

#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::renderer {

using glm::mat4;
using glm::vec2;
using glm::vec3;
using glm::vec4;

bool operator==(const Line_renderer_config& lhs, const Line_renderer_config& rhs)
{
    return
        (lhs.stencil_reference == rhs.stencil_reference) &&
        (lhs.draw_visible      == rhs.draw_visible     ) &&
        (lhs.draw_hidden       == rhs.draw_hidden      ) &&
        (lhs.reverse_depth     == rhs.reverse_depth    );
}

auto Line_renderer_bucket::Line_renderer_bucket::make_pipeline(const bool visible) -> erhe::graphics::Pipeline
{
    erhe::graphics::Shader_stages* const graphics_shader_stages = m_line_renderer.get_program_interface().graphics_shader_stages.get();

    const gl::Depth_function depth_compare_op0 = visible ? gl::Depth_function::less : gl::Depth_function::gequal;
    const gl::Depth_function depth_compare_op  = m_config.reverse_depth ? erhe::graphics::reverse(depth_compare_op0) : depth_compare_op0;
    return erhe::graphics::Pipeline{
        erhe::graphics::Pipeline_data{
            .name           = "Line Renderer",
            .shader_stages  = graphics_shader_stages,
            .vertex_input   = m_line_renderer.get_vertex_input(),
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
                    .function        = gl::Stencil_function::gequal,
                    .reference       = m_config.stencil_reference,
                    .test_mask       = visible ? 0x7fu : 0xffu,
                    .write_mask      = 0x7fu
                },
                .stencil_back = {
                    .stencil_fail_op = gl::Stencil_op::keep,
                    .z_fail_op       = gl::Stencil_op::keep,
                    .z_pass_op       = gl::Stencil_op::replace,
                    .function        = gl::Stencil_function::gequal,
                    .reference       = m_config.stencil_reference,
                    .test_mask       = visible ? 0x7fu : 0xffu,
                    .write_mask      = 0x7fu
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

Line_renderer_bucket::Line_renderer_bucket(Line_renderer& line_renderer, Line_renderer_config config)
    : m_line_renderer   {line_renderer}
    , m_config          {config}
    , m_pipeline_visible{make_pipeline(true )}
    , m_pipeline_hidden {make_pipeline(false)}
{
}

auto Line_renderer_bucket::match(const Line_renderer_config& config) const -> bool
{
    return m_config == config;
}

void Line_renderer_bucket::begin_frame()
{
    m_draws.clear();
}

void Line_renderer_bucket::append_lines(std::size_t first_line, std::size_t line_count)
{
    if (line_count == 0) {
        return;
    }

    // TODO Merge with previous if possible
    m_draws.emplace_back(first_line, line_count);
}

void Line_renderer_bucket::render(erhe::graphics::Instance& graphics_instance, bool draw_hidden, bool draw_visible)
{
    if (draw_hidden && m_config.draw_hidden) {
        graphics_instance.opengl_state_tracker.execute(m_pipeline_hidden);
        for (const Line_draw_entry& draw : m_draws) {
            gl::draw_arrays(
                m_pipeline_hidden.data.input_assembly.primitive_topology,
                static_cast<GLsizei>(6 * draw.first_line),
                static_cast<GLint  >(6 * draw.line_count)
            );
        }
    }

    if (draw_visible && m_config.draw_visible) {
        graphics_instance.opengl_state_tracker.execute(m_pipeline_visible);
        for (const Line_draw_entry& draw : m_draws) {
            gl::draw_arrays(
                m_pipeline_visible.data.input_assembly.primitive_topology,
                static_cast<GLsizei>(6 * draw.first_line),
                static_cast<GLint  >(6 * draw.line_count)
            );
        }
    }
}

} // namespace erhe::renderer
