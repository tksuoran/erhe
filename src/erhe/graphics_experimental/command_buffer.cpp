#include "erhe/graphics_experimental/command_buffer.hpp"

#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/sampler.hpp"
#include "erhe/graphics/texture.hpp"

namespace erhe::graphics
{

auto Command_buffer::opengl_state_tracker()
-> erhe::graphics::OpenGL_state_tracker*
{
    return m_pipeline_state_tracker.get();
}

void Command_buffer::connect()
{
    m_pipeline_state_tracker = get<erhe::graphics::OpenGL_state_tracker>();
}

void Command_buffer::bind_pipeline(Pipeline* pipeline)
{
    Expects(m_pipeline_state_tracker);

    m_pipeline = pipeline;

    m_pipeline_state_tracker->execute(pipeline);
}

void Command_buffer::bind_texture_unit(unsigned int unit, Texture* texture)
{
    m_texture_bindings.texture_units[unit].texture = texture;

    unsigned int name = (texture != nullptr) ? texture->gl_name() : 0;
    gl::bind_texture_unit(unit, name);
    // TODO cache
}

void Command_buffer::bind_sampler(unsigned int unit, Sampler* sampler)
{
    m_texture_bindings.texture_units[unit].sampler = sampler;

    unsigned int name = (sampler != nullptr) ? sampler->gl_name() : 0;
    gl::bind_sampler(unit, name);
    // TODO cache
}

void Command_buffer::bind_buffer(unsigned int binding_point,
                                 Buffer*      buffer,
                                 size_t       byte_offset,
                                 size_t       byte_count)
{
    Expects(buffer != nullptr);
    gl::bind_buffer_range(buffer->target(), binding_point, buffer->gl_name(), byte_offset, byte_count);
    // TODO cache
}

void Command_buffer::bind_buffer(Buffer*  buffer)
{
    Expects(buffer != nullptr);
    gl::bind_buffer(buffer->target(), buffer->gl_name());
    // TODO cache
}

void Command_buffer::unbind_buffer(gl::Buffer_target target)
{
    gl::bind_buffer(target, 0);
}


void Command_buffer::draw_arrays(gl::Draw_arrays_indirect_command draw)
{
    auto primitive_type = m_pipeline->input_assembly->primitive_topology;
    gl::draw_arrays_instanced_base_instance(primitive_type,
                                            draw.first,
                                            draw.count,
                                            draw.instance_count,
                                            draw.base_instance);
}

void Command_buffer::draw_arrays(size_t draw_offset)
{
    auto primitive_type = m_pipeline->input_assembly->primitive_topology;
    gl::draw_arrays_indirect(primitive_type, reinterpret_cast<const void*>(draw_offset));
}

void Command_buffer::draw_elements(gl::Draw_elements_type             index_type,
                                   gl::Draw_elements_indirect_command draw)
{
    auto primitive_type = m_pipeline->input_assembly->primitive_topology;
    gl::draw_elements_base_vertex(primitive_type,
                                  draw.index_count,
                                  index_type,
                                  reinterpret_cast<const void*>(draw.first_index * size_of_type(index_type)),
                                  draw.base_vertex);
}

void Command_buffer::draw_elements(gl::Draw_elements_type index_type,
                                   size_t                 draw_offset)
{
    auto primitive_type = m_pipeline->input_assembly->primitive_topology;
    gl::draw_elements_indirect(primitive_type, index_type, reinterpret_cast<const void* >(draw_offset));
}

} // namespace erhe::graphics
