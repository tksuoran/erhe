#include "erhe_graphics/gl/gl_state_tracker.hpp"
#include "erhe_graphics/gl/gl_buffer.hpp"
#include "erhe_graphics/gl/gl_helpers.hpp"
#include "erhe_graphics/gl/gl_render_pass.hpp"
#include "erhe_graphics/gl/gl_gpu_timer.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/compute_pipeline_state.hpp"
#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#define DISABLE_CACHE 0

namespace erhe::graphics {

//[[nodiscard]] auto get_gl_attribute_type    (erhe::dataformat::Format format) -> gl::Attribute_type;
//[[nodiscard]] auto get_gl_vertex_attrib_type(erhe::dataformat::Format format) -> gl::Vertex_attrib_type;
//[[nodiscard]] auto get_gl_normalized        (erhe::dataformat::Format format) -> bool;

void Color_blend_state_tracker::reset()
{
    gl::blend_color(0.0f, 0.0f, 0.0f, 0.0f);
    gl::blend_equation_separate(gl::Blend_equation_mode::func_add, gl::Blend_equation_mode::func_add);
    gl::blend_func_separate(
        gl::Blending_factor::one,
        gl::Blending_factor::zero,
        gl::Blending_factor::one,
        gl::Blending_factor::zero
    );
    gl::disable(gl::Enable_cap::blend);
    gl::color_mask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    m_cache = Color_blend_state{};
}

void Color_blend_state_tracker::execute(const Color_blend_state& state) noexcept
{
#if DISABLE_CACHE
    if (state.enabled) {
        gl::enable(gl::Enable_cap::blend);
        gl::blend_color(
            state.constant[0],
            state.constant[1],
            state.constant[2],
            state.constant[3]
        );
        gl::blend_equation_separate(to_gl(state.rgb.equation_mode), to_gl(state.alpha.equation_mode));
        gl::blend_func_separate(
            to_gl(state.rgb.source_factor),
            to_gl(state.rgb.destination_factor),
            to_gl(state.alpha.source_factor),
            to_gl(state.alpha.destination_factor)
        );
    } else {
        gl::disable(gl::Enable_cap::blend);
    }

    gl::color_mask(
        state.write_mask.red   ? GL_TRUE : GL_FALSE,
        state.write_mask.green ? GL_TRUE : GL_FALSE,
        state.write_mask.blue  ? GL_TRUE : GL_FALSE,
        state.write_mask.alpha ? GL_TRUE : GL_FALSE
    );
#else
    if (state.enabled) {
        if (!m_cache.enabled) {
            gl::enable(gl::Enable_cap::blend);
            m_cache.enabled = true;
        }
        if (
            (m_cache.constant[0] != state.constant[0]) ||
            (m_cache.constant[1] != state.constant[1]) ||
            (m_cache.constant[2] != state.constant[2]) ||
            (m_cache.constant[3] != state.constant[3])
        ) {
            gl::blend_color(
                state.constant[0],
                state.constant[1],
                state.constant[2],
                state.constant[3]
            );
            m_cache.constant[0] = state.constant[0];
            m_cache.constant[1] = state.constant[1];
            m_cache.constant[2] = state.constant[2];
            m_cache.constant[3] = state.constant[3];
        }
        if (
            (m_cache.rgb.equation_mode   != state.rgb.equation_mode) ||
            (m_cache.alpha.equation_mode != state.alpha.equation_mode)
        ) {
            gl::blend_equation_separate(to_gl(state.rgb.equation_mode), to_gl(state.alpha.equation_mode));
            m_cache.rgb.equation_mode   = state.rgb.equation_mode;
            m_cache.alpha.equation_mode = state.alpha.equation_mode;
        }
        if (
            (m_cache.rgb.source_factor        != state.rgb.source_factor       ) ||
            (m_cache.rgb.destination_factor   != state.rgb.destination_factor  ) ||
            (m_cache.alpha.source_factor      != state.alpha.source_factor     ) ||
            (m_cache.alpha.destination_factor != state.alpha.destination_factor)
        ) {
            gl::blend_func_separate(
                to_gl(state.rgb.source_factor),
                to_gl(state.rgb.destination_factor),
                to_gl(state.alpha.source_factor),
                to_gl(state.alpha.destination_factor)
            );
            m_cache.rgb.source_factor        = state.rgb.source_factor;
            m_cache.rgb.destination_factor   = state.rgb.destination_factor;
            m_cache.alpha.source_factor      = state.alpha.source_factor;
            m_cache.alpha.destination_factor = state.alpha.destination_factor;
        }
    } else {
        if (m_cache.enabled) {
            gl::disable(gl::Enable_cap::blend);
            m_cache.enabled = false;
        }
    }

    if (
        (m_cache.write_mask.red   != state.write_mask.red  ) ||
        (m_cache.write_mask.green != state.write_mask.green) ||
        (m_cache.write_mask.blue  != state.write_mask.blue ) ||
        (m_cache.write_mask.alpha != state.write_mask.alpha)
    ) {
        gl::color_mask(
            state.write_mask.red   ? GL_TRUE : GL_FALSE,
            state.write_mask.green ? GL_TRUE : GL_FALSE,
            state.write_mask.blue  ? GL_TRUE : GL_FALSE,
            state.write_mask.alpha ? GL_TRUE : GL_FALSE
        );
        m_cache.write_mask.red   = state.write_mask.red;
        m_cache.write_mask.green = state.write_mask.green;
        m_cache.write_mask.blue  = state.write_mask.blue;
        m_cache.write_mask.alpha = state.write_mask.alpha;
    }
#endif
}

void Depth_stencil_state_tracker::reset()
{
    gl::disable(gl::Enable_cap::depth_test);
    gl::depth_func(gl::Depth_function::less); // Not Maybe_reversed::less, this has to match default OpenGL state
    gl::depth_mask(GL_TRUE);

    gl::stencil_op(gl::Stencil_op::keep, gl::Stencil_op::keep, gl::Stencil_op::keep);
    gl::stencil_mask(0xffu);
    gl::stencil_func(gl::Stencil_function::always, 0, 0xffu);
    m_cache = Depth_stencil_state{};
}

void Depth_stencil_state_tracker::execute_component(
    Stencil_face_direction  face,
    const Stencil_op_state& state,
    Stencil_op_state&       cache
)
{
#if DISABLE_CACHE
    static_cast<void>(cache);
    gl::stencil_op_separate(face, state.stencil_fail_op, state.z_fail_op, state.z_pass_op);
    gl::stencil_mask_separate(face, state.write_mask);
    gl::stencil_func_separate(face, state.function, state.reference, state.test_mask);
#else
    if (
        (cache.stencil_fail_op != state.stencil_fail_op) ||
        (cache.z_fail_op       != state.z_fail_op) ||
        (cache.z_pass_op       != state.z_pass_op)
    ) {
        gl::stencil_op_separate(to_gl(face), to_gl(state.stencil_fail_op), to_gl(state.z_fail_op), to_gl(state.z_pass_op));
        cache.stencil_fail_op = state.stencil_fail_op;
        cache.z_fail_op       = state.z_fail_op;
        cache.z_pass_op       = state.z_pass_op;
    }
    if (cache.write_mask != state.write_mask) {
        gl::stencil_mask_separate(to_gl(face), state.write_mask);
        cache.write_mask = state.write_mask;
    }

    if (
        (cache.function  != state.function) ||
        (cache.reference != state.reference) ||
        (cache.test_mask != state.test_mask)
    ) {
        gl::stencil_func_separate(to_gl(face), to_gl_stencil_function(state.function), state.reference, state.test_mask);
        cache.function  = state.function;
        cache.reference = state.reference;
        cache.test_mask = state.test_mask;
    }
#endif
}

void Depth_stencil_state_tracker::execute_shared(const Stencil_op_state& state, Depth_stencil_state& cache)
{
#if DISABLE_CACHE
    static_cast<void>(cache);
    gl::stencil_op(state.stencil_fail_op, state.z_fail_op, state.z_pass_op);
    gl::stencil_mask(state.write_mask);
    gl::stencil_func(state.function, state.reference, state.test_mask);
#else
    if (
        (cache.stencil_front.stencil_fail_op != state.stencil_fail_op) ||
        (cache.stencil_front.z_fail_op       != state.z_fail_op)       ||
        (cache.stencil_front.z_pass_op       != state.z_pass_op)       ||
        (cache.stencil_back.stencil_fail_op  != state.stencil_fail_op) ||
        (cache.stencil_back.z_fail_op        != state.z_fail_op)       ||
        (cache.stencil_back.z_pass_op        != state.z_pass_op)
    ) {
        gl::stencil_op(to_gl(state.stencil_fail_op), to_gl(state.z_fail_op), to_gl(state.z_pass_op));
        cache.stencil_front.stencil_fail_op = state.stencil_fail_op;
        cache.stencil_front.z_fail_op       = state.z_fail_op;
        cache.stencil_front.z_pass_op       = state.z_pass_op;
        cache.stencil_back.stencil_fail_op  = state.stencil_fail_op;
        cache.stencil_back.z_fail_op        = state.z_fail_op;
        cache.stencil_back.z_pass_op        = state.z_pass_op;
    }

    if (
        (cache.stencil_front.write_mask != state.write_mask) ||
        (cache.stencil_back.write_mask  != state.write_mask)
    ) {
        gl::stencil_mask(state.write_mask);
        cache.stencil_front.write_mask = state.write_mask;
        cache.stencil_back.write_mask  = state.write_mask;
    }

    if (
        (cache.stencil_front.function  != state.function)  ||
        (cache.stencil_front.reference != state.reference) ||
        (cache.stencil_front.test_mask != state.test_mask) ||
        (cache.stencil_back.function   != state.function)  ||
        (cache.stencil_back.reference  != state.reference) ||
        (cache.stencil_back.test_mask  != state.test_mask)
    ) {
        gl::stencil_func(to_gl_stencil_function(state.function), state.reference, state.test_mask);
        cache.stencil_front.function  = state.function;
        cache.stencil_front.reference = state.reference;
        cache.stencil_front.test_mask = state.test_mask;
        cache.stencil_back.function   = state.function;
        cache.stencil_back.reference  = state.reference;
        cache.stencil_back.test_mask  = state.test_mask;
    }
#endif
}

void Depth_stencil_state_tracker::execute(const Depth_stencil_state& state)
{
#if DISABLE_CACHE
    if (state.depth_test_enable)
    {
        gl::enable(gl::Enable_cap::depth_test);
        gl::depth_func(state.depth_compare_op);
    } else {
        gl::disable(gl::Enable_cap::depth_test);
        gl::depth_func(state.depth_compare_op);
    }

    gl::depth_mask(state.depth_write_enable ? GL_TRUE : GL_FALSE);

    if (state.stencil_test_enable) {
        gl::enable(gl::Enable_cap::stencil_test);
        execute_component(gl::Stencil_face_direction::front, state.stencil_front, m_cache.stencil_front);
        execute_component(gl::Stencil_face_direction::back,  state.stencil_back,  m_cache.stencil_back);
    } else {
        gl::disable(gl::Enable_cap::stencil_test);
        execute_component(gl::Stencil_face_direction::front, state.stencil_front, m_cache.stencil_front);
        execute_component(gl::Stencil_face_direction::back,  state.stencil_back,  m_cache.stencil_back);
    }
#else
    if (state.depth_test_enable) {
        if (!m_cache.depth_test_enable) {
            gl::enable(gl::Enable_cap::depth_test);
            m_cache.depth_test_enable = true;
        }

        if (m_cache.depth_compare_op != state.depth_compare_op) {
            gl::depth_func(to_gl_depth_function(state.depth_compare_op));
            m_cache.depth_compare_op = state.depth_compare_op;
        }
    } else {
        if (m_cache.depth_test_enable) {
            gl::disable(gl::Enable_cap::depth_test);
            m_cache.depth_test_enable = false;
        }
    }

    if (m_cache.depth_write_enable != state.depth_write_enable) {
        gl::depth_mask(state.depth_write_enable ? GL_TRUE : GL_FALSE);
        m_cache.depth_write_enable = state.depth_write_enable;
    }

    if (state.stencil_test_enable) {
        if (!m_cache.stencil_test_enable) {
            gl::enable(gl::Enable_cap::stencil_test);
            m_cache.stencil_test_enable = true;
        }
        execute_component(Stencil_face_direction::front, state.stencil_front, m_cache.stencil_front);
        execute_component(Stencil_face_direction::back,  state.stencil_back,  m_cache.stencil_back);
    } else {
        if (m_cache.stencil_test_enable) {
            gl::disable(gl::Enable_cap::stencil_test);
            m_cache.stencil_test_enable = false;
        }
        execute_component(Stencil_face_direction::front, state.stencil_front, m_cache.stencil_front);
        execute_component(Stencil_face_direction::back,  state.stencil_back,  m_cache.stencil_back);
    }
#endif
}

void Multisample_state_tracker::reset()
{
    gl::disable(gl::Enable_cap::sample_shading);
    gl::disable(gl::Enable_cap::sample_alpha_to_coverage);
    gl::disable(gl::Enable_cap::sample_alpha_to_one);
    gl::min_sample_shading(1.0f);
}

void Multisample_state_tracker::execute(const Multisample_state& state)
{
#if DISABLE_CACHE
    if (state.sample_shading_enable) {
        gl::enable(gl::Enable_cap::sample_shading);
    } else {
        gl::disable(gl::Enable_cap::sample_shading);
    }
    if (state.alpha_to_coverage_enable) {
        gl::enable(gl::Enable_cap::sample_alpha_to_coverage);
    } else {
        gl::disable(gl::Enable_cap::sample_alpha_to_coverage);
    }
    if (state.alpha_to_one_enable) {
        gl::enable(gl::Enable_cap::sample_alpha_to_one);
    } else {
        gl::disable(gl::Enable_cap::sample_alpha_to_one);
    }
    gl::min_sample_shading(state.min_sample_shading);
#else
    if (state.sample_shading_enable) {
        if (!m_cache.sample_shading_enable) {
            gl::enable(gl::Enable_cap::sample_shading);
            m_cache.sample_shading_enable = true;
        }
        if (m_cache.min_sample_shading != state.min_sample_shading) {
            gl::min_sample_shading(state.min_sample_shading);
            m_cache.min_sample_shading = state.min_sample_shading;
        }
    } else {
        if (m_cache.sample_shading_enable) {
            gl::disable(gl::Enable_cap::sample_shading);
            m_cache.sample_shading_enable = false;
        }
    }

    if (state.alpha_to_coverage_enable) {
        if (!m_cache.alpha_to_coverage_enable) {
            gl::enable(gl::Enable_cap::sample_alpha_to_coverage);
            m_cache.alpha_to_coverage_enable = true;
        }
    } else {
        if (m_cache.alpha_to_coverage_enable) {
            gl::disable(gl::Enable_cap::sample_alpha_to_coverage);
            m_cache.alpha_to_coverage_enable = false;
        }
    }

    if (state.alpha_to_one_enable) {
        if (!m_cache.alpha_to_one_enable) {
            gl::enable(gl::Enable_cap::sample_alpha_to_one);
            m_cache.alpha_to_one_enable = true;
        }
    } else {
        if (m_cache.alpha_to_one_enable) {
            gl::disable(gl::Enable_cap::sample_alpha_to_one);
            m_cache.alpha_to_one_enable = false;
        }
    }
#endif
}

void Rasterization_state_tracker::reset()
{
    gl::disable     (gl::Enable_cap::cull_face);
    gl::cull_face   (gl::Cull_face_mode::back);
    gl::front_face  (gl::Front_face_direction::ccw);
    gl::polygon_mode(gl::Material_face::front_and_back, gl::Polygon_mode::fill);
    m_cache = Rasterization_state{};
}

void Rasterization_state_tracker::execute(const Rasterization_state& state)
{
#if DISABLE_CACHE
    if (state.face_cull_enable) {
        gl::enable(gl::Enable_cap::cull_face);
        gl::cull_face(state.cull_face_mode);
    } else {
        gl::disable(gl::Enable_cap::cull_face);
    }
    if (state.depth_clamp_enable) {
        gl::enable(gl::Enable_cap::depth_clamp);
    } else {
        gl::disable(gl::Enable_cap::depth_clamp);
    }

    gl::front_face(state.front_face_direction);
    gl::polygon_mode(gl::Material_face::front_and_back, state.polygon_mode);
#else
    if (state.face_cull_enable) {
        if (!m_cache.face_cull_enable) {
            gl::enable(gl::Enable_cap::cull_face);
            m_cache.face_cull_enable = true;
        }
        if (m_cache.cull_face_mode != state.cull_face_mode) {
            gl::cull_face(to_gl(state.cull_face_mode));
            m_cache.cull_face_mode = state.cull_face_mode;
        }
    } else {
        if (m_cache.face_cull_enable) {
            gl::disable(gl::Enable_cap::cull_face);
            m_cache.face_cull_enable = false;
        }
    }

    if (state.depth_clamp_enable) {
        gl::enable(gl::Enable_cap::depth_clamp);
        m_cache.depth_clamp_enable = true;
    } else {
        gl::disable(gl::Enable_cap::depth_clamp);
        m_cache.depth_clamp_enable = false;
    }

    if (m_cache.front_face_direction != state.front_face_direction) {
        gl::front_face(to_gl(state.front_face_direction));
        m_cache.front_face_direction = state.front_face_direction;
    }

    if (m_cache.polygon_mode != state.polygon_mode) {
        gl::polygon_mode(gl::Material_face::front_and_back, to_gl(state.polygon_mode));
        m_cache.polygon_mode = state.polygon_mode;
    }
#endif
}

void Scissor_state_tracker::reset()
{
    gl::scissor(0, 0, 0xffff, 0xffff);
    m_cache = Scissor_state{};
}

void Scissor_state_tracker::execute(Scissor_state const& state)
{
#if !DISABLE_CACHE
    if (
        (m_cache.x      != state.x)      ||
        (m_cache.y      != state.y)      ||
        (m_cache.width  != state.width)  ||
        (m_cache.height != state.height)
    )
#endif
    {
        gl::scissor(state.x, state.y, state.width, state.height);
        m_cache = state;
    }
}

void Vertex_input_state_tracker::reset()
{
    gl::bind_vertex_array(0);
    m_last = 0;
}

void Vertex_input_state_tracker::execute(const Vertex_input_state* const state)
{
    const unsigned int name = (state != nullptr) ? state->get_impl().gl_name() : 0;
    if (m_last == name) {
        return;
    }
    gl::bind_vertex_array(name);
    m_last = name;

    // For set_vertex_buffer()
    if (state != nullptr) {
        m_bindings = state->get_data().bindings;
    } else {
        m_bindings.clear();
    }
}

void Vertex_input_state_tracker::set_index_buffer(const Buffer* buffer) const
{
    ERHE_VERIFY(m_last != 0); // Must have VAO bound
    gl::vertex_array_element_buffer(
        m_last,
        (buffer != nullptr) 
            ? buffer->get_impl().gl_name()
            : 0
    );
}

void Vertex_input_state_tracker::set_vertex_buffer(const std::uintptr_t binding_index, const Buffer* const buffer, const std::uintptr_t offset)
{
    ERHE_VERIFY(m_last != 0); // Must have VAO bound
    ERHE_VERIFY(buffer != nullptr);
    for (const Vertex_input_binding& binding : m_bindings) {
        if (binding.binding == binding_index) {
            gl::vertex_array_vertex_buffer(
                m_last,
                static_cast<GLuint>(binding_index),
                (buffer != nullptr) ? buffer->get_impl().gl_name() : 0,
                static_cast<GLintptr>(offset),
                static_cast<GLsizei>(binding.stride)
            );
            break;
        }
    }
}

void Viewport_rect_state_tracker::reset()
{
    gl::viewport_indexed_f(0, 0.0f, 0.0f, 0.0f, 0.0f);
    m_cache = Viewport_rect_state{};
}

void Viewport_rect_state_tracker::execute(const Viewport_rect_state& state)
{
#if !DISABLE_CACHE
    if (
        (m_cache.x      != state.x)      ||
        (m_cache.y      != state.y)      ||
        (m_cache.width  != state.width)  ||
        (m_cache.height != state.height)
    )
#endif
    {
        gl::viewport_indexed_f(0, state.x, state.y, state.width, state.height);
        m_cache.x      = state.x;
        m_cache.y      = state.y;
        m_cache.width  = state.width;
        m_cache.height = state.height;
    }
}

void Viewport_depth_range_state_tracker::reset()
{
    gl::depth_range(0.0f, 1.0f);
    m_cache = Viewport_depth_range_state{};
}

void Viewport_depth_range_state_tracker::execute(const Viewport_depth_range_state& state)
{

#if !DISABLE_CACHE
    if (
        (m_cache.min_depth != state.min_depth) ||
        (m_cache.max_depth != state.max_depth)
    )
#endif
    {
        gl::depth_range_f(state.min_depth, state.max_depth);
        m_cache.min_depth = state.min_depth;
        m_cache.max_depth = state.max_depth;
    }
}

OpenGL_state_tracker::OpenGL_state_tracker() = default;

void OpenGL_state_tracker::on_thread_exit()
{
    vertex_input .reset();
    shader_stages.reset();

    Render_pass_impl       ::on_thread_exit();
    Vertex_input_state_impl::on_thread_exit();
    Gpu_timer_impl         ::on_thread_exit();
}

void OpenGL_state_tracker::on_thread_enter()
{
    Render_pass_impl       ::on_thread_enter();
    Vertex_input_state_impl::on_thread_enter();
    Gpu_timer_impl         ::on_thread_enter();
}

void OpenGL_state_tracker::reset()
{
    ERHE_PROFILE_FUNCTION();

    shader_stages       .reset();
    vertex_input        .reset();
    input_assembly      .reset();
    multisample         .reset();
    viewport_rect       .reset();
    viewport_depth_range.reset();
    scissor             .reset();
    rasterization       .reset();
    depth_stencil       .reset();
    color_blend         .reset();
}

void OpenGL_state_tracker::execute_(const Render_pipeline_state& pipeline, const bool skip_shader_stages)
{
    ERHE_PROFILE_FUNCTION();

    if (!skip_shader_stages) {
        shader_stages.execute(pipeline.data.shader_stages);
    }
    vertex_input  .execute(pipeline.data.vertex_input);
    input_assembly.execute(pipeline.data.input_assembly);
    // tessellation

    rasterization       .execute(pipeline.data.rasterization);
    multisample         .execute(pipeline.data.multisample);
    viewport_depth_range.execute(pipeline.data.viewport_depth_range);
    scissor             .execute(pipeline.data.scissor);
    depth_stencil       .execute(pipeline.data.depth_stencil);
    color_blend         .execute(pipeline.data.color_blend);

}

void OpenGL_state_tracker::execute_(const Compute_pipeline_state& pipeline)
{
    shader_stages.execute(pipeline.data.shader_stages);
}

} // namespace erhe::graphics
