#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/opengl_state_tracker.hpp"
#include "erhe_gl/wrapper_enums.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_gl/gl_helpers.hpp"
#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::graphics {

Render_command_encoder::Render_command_encoder(Device& device, Render_pass& render_pass)
    : Command_encoder{device}
    , m_render_pass  {render_pass}
{
    start_render_pass();
}

Render_command_encoder::~Render_command_encoder()
{
    end_render_pass();
}

void Render_command_encoder::set_render_pipeline_state(const Render_pipeline_state& pipeline)
{
    m_device.opengl_state_tracker.execute_(pipeline, false);
}

void Render_command_encoder::start_render_pass()
{
    m_render_pass.start_render_pass();
}

void Render_command_encoder::end_render_pass()
{
    m_render_pass.end_render_pass();
}

void Render_command_encoder::set_index_buffer(const Buffer* buffer)
{
    m_device.opengl_state_tracker.vertex_input.set_index_buffer(buffer);
}

void Render_command_encoder::set_vertex_buffer(const Buffer* buffer, std::uintptr_t offset, std::uintptr_t index)
{
    m_device.opengl_state_tracker.vertex_input.set_vertex_buffer(index, buffer, offset);
}

[[nodiscard]] auto to_gl(Primitive_type primitive_type) -> gl::Primitive_type
{
    switch (primitive_type) {
        case Primitive_type::point         : return gl::Primitive_type::points        ;
        case Primitive_type::line          : return gl::Primitive_type::lines         ;
        case Primitive_type::line_strip    : return gl::Primitive_type::line_strip    ;
        case Primitive_type::triangle      : return gl::Primitive_type::triangles     ;
        case Primitive_type::triangle_strip: return gl::Primitive_type::triangle_strip;
        default: {
            ERHE_FATAL("bad primitive type %u", static_cast<unsigned int>(primitive_type));
            return gl::Primitive_type::points;
        }
    }
}

void Render_command_encoder::draw_primitives(
    Primitive_type primitive_type,
    std::uintptr_t vertex_start,
    std::uintptr_t vertex_count,
    std::uintptr_t instance_count
)
{
    const gl::Primitive_type gl_primitive_type = to_gl(primitive_type);
    gl::draw_arrays_instanced(
        gl_primitive_type,
        static_cast<GLint>(vertex_start),
        static_cast<GLsizei>(vertex_count),
        static_cast<GLsizei>(instance_count)
    );
}

void Render_command_encoder::draw_primitives(
    Primitive_type primitive_type,
    std::uintptr_t vertex_start,
    std::uintptr_t vertex_count
)
{
    const gl::Primitive_type gl_primitive_type = to_gl(primitive_type);
    gl::draw_arrays(
        gl_primitive_type,
        static_cast<GLint>(vertex_start),
        static_cast<GLsizei>(vertex_count)
    );
}

void Render_command_encoder::draw_indexed_primitives(
    Primitive_type           primitive_type,
    std::uintptr_t           index_count,
    erhe::dataformat::Format index_type,
    std::uintptr_t           index_buffer_offset,
    std::uintptr_t           instance_count)
{
    const gl::Primitive_type     gl_primitive_type = to_gl(primitive_type);
    const gl::Draw_elements_type gl_index_type     = gl_helpers::convert_to_gl_index_type(index_type).value();
    gl::draw_elements_instanced(
        gl_primitive_type,
        static_cast<GLsizei>(index_count),
        gl_index_type,
        reinterpret_cast<const void *>(index_buffer_offset),
        static_cast<GLsizei>(instance_count)
    );
}

void Render_command_encoder::draw_indexed_primitives(
    Primitive_type           primitive_type,
    std::uintptr_t           index_count,
    erhe::dataformat::Format index_type,
    std::uintptr_t           index_buffer_offset
)
{
    const gl::Primitive_type     gl_primitive_type = to_gl(primitive_type);
    const gl::Draw_elements_type gl_index_type     = gl_helpers::convert_to_gl_index_type(index_type).value();
    gl::draw_elements(
        gl_primitive_type,
        static_cast<GLsizei>(index_count),
        gl_index_type,
        reinterpret_cast<const void *>(index_buffer_offset)
    );
}

void Render_command_encoder::multi_draw_indexed_primitives_indirect(
    Primitive_type           primitive_type,
    erhe::dataformat::Format index_type,
    std::uintptr_t           indirect_offset,
    std::uintptr_t           drawcount,
    std::uintptr_t           stride
)
{
    const gl::Primitive_type     gl_primitive_type = to_gl(primitive_type);
    const gl::Draw_elements_type gl_index_type     = gl_helpers::convert_to_gl_index_type(index_type).value();

    gl::multi_draw_elements_indirect(
        gl_primitive_type,
        gl_index_type,
        reinterpret_cast<const void *>(indirect_offset),
        static_cast<GLsizei>(drawcount),
        static_cast<GLsizei>(stride)
    );
}

} // namespace erhe::graphics
