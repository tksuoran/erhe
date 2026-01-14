#include "erhe_graphics/gl/gl_render_command_encoder.hpp"
#include "erhe_graphics/gl/gl_buffer.hpp"
#include "erhe_graphics/gl/gl_device.hpp"
#include "erhe_graphics/gl/gl_render_pass.hpp"
#include "erhe_graphics/gl/gl_state_tracker.hpp"
#include "erhe_graphics/state/viewport_state.hpp"
#include "erhe_gl/wrapper_enums.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_gl/gl_helpers.hpp"
#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::graphics {

Render_command_encoder_impl::Render_command_encoder_impl(Device& device, Render_pass& render_pass)
    : Command_encoder_impl{device}
    , m_render_pass       {render_pass}
{
    start_render_pass();
}

Render_command_encoder_impl::~Render_command_encoder_impl()
{
    end_render_pass();
}

void Render_command_encoder_impl::set_render_pipeline_state(const Render_pipeline_state& pipeline)
{
    m_device.get_impl().m_gl_state_tracker.execute_(pipeline, false);
}

void Render_command_encoder_impl::set_render_pipeline_state(const Render_pipeline_state& pipeline, const Shader_stages* override_shader_stages)
{
    m_device.get_impl().m_gl_state_tracker.execute_(pipeline, true);
    m_device.get_impl().m_gl_state_tracker.shader_stages.execute(override_shader_stages);
}

void Render_command_encoder_impl::set_viewport_rect(int x, int y, int width, int height)
{
    m_device.get_impl().m_gl_state_tracker.viewport_rect.execute(
        Viewport_rect_state{
            .x      = static_cast<float>(x),
            .y      = static_cast<float>(y),
            .width  = static_cast<float>(width),
            .height = static_cast<float>(height),
        }
    );
}

void Render_command_encoder_impl::set_viewport_depth_range(const float min_depth, const float max_depth)
{
    m_device.get_impl().m_gl_state_tracker.viewport_depth_range.execute(
        Viewport_depth_range_state{
            .min_depth = min_depth,
            .max_depth = max_depth
        }
    );
}

void Render_command_encoder_impl::set_scissor_rect(int x, int y, int width, int height)
{
    m_device.get_impl().m_gl_state_tracker.scissor.execute(
        Scissor_state{
            .x      = x,
            .y      = y,
            .width  = width,
            .height = height
        }
    );
}

void Render_command_encoder_impl::start_render_pass()
{
    m_render_pass.get_impl().start_render_pass();
}

void Render_command_encoder_impl::end_render_pass()
{
    m_render_pass.get_impl().end_render_pass();
}

void Render_command_encoder_impl::set_index_buffer(const Buffer* buffer)
{
    m_device.get_impl().m_gl_state_tracker.vertex_input.set_index_buffer(buffer);
}

void Render_command_encoder_impl::set_vertex_buffer(const Buffer* buffer, std::uintptr_t offset, std::uintptr_t index)
{
    m_device.get_impl().m_gl_state_tracker.vertex_input.set_vertex_buffer(index, buffer, offset);
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

void Render_command_encoder_impl::draw_primitives(
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

void Render_command_encoder_impl::draw_primitives(
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

void Render_command_encoder_impl::draw_indexed_primitives(
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

void Render_command_encoder_impl::draw_indexed_primitives(
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

void Render_command_encoder_impl::multi_draw_indexed_primitives_indirect(
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

    uint64_t offset = static_cast<uint64_t>(indirect_offset);
    //const uint32_t IdxSize = static_cast<uint32_t>(erhe::dataformat::get_format_size_bytes(index_type));
    struct DrawElementsIndirectCommand
    {
        uint32_t count;
        uint32_t instanceCount;
        uint32_t firstIndex;
        int32_t baseVertex;
        uint32_t baseInstance;
    };

    // From renderdoc
    GLintptr offs = (GLintptr)offset;

    for(GLsizei i = 0; i < drawcount; i++)
    {
        DrawElementsIndirectCommand params = {};

        gl::get_buffer_sub_data(gl::Buffer_target::draw_indirect_buffer, offs, sizeof(params), &params);

        if(stride)
            offs += stride;
        else
            offs += sizeof(params);
    }
}

} // namespace erhe::graphics
