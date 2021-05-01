#include "erhe/graphics/gl_objects.hpp"
#include "erhe/gl/gl.hpp"

#include <gsl/assert>

namespace erhe::graphics
{

Gl_texture::Gl_texture(gl::Texture_target target)
{
    gl::create_textures(target, 1, &m_gl_name);
    Ensures(m_gl_name != 0);
}

Gl_texture::~Gl_texture()
{
    if (m_gl_name != 0)
    {
        gl::delete_textures(1, &m_gl_name);
        m_gl_name = 0;
    }
}

Gl_program::Gl_program()
{
    m_gl_name = gl::create_program();
    Ensures(m_gl_name != 0);
}

Gl_program::~Gl_program()
{
    if (m_gl_name != 0)
    {
        gl::delete_program(m_gl_name);
        m_gl_name = 0;
    }
}

Gl_shader::Gl_shader(gl::Shader_type shader_type)
{
    m_gl_name = gl::create_shader(shader_type);
    Ensures(m_gl_name != 0);
}

Gl_shader::~Gl_shader()
{
    if (m_gl_name != 0)
    {
        gl::delete_shader(m_gl_name);
        m_gl_name = 0;
    }
}

Gl_sampler::Gl_sampler()
{
    gl::create_samplers(1, &m_gl_name);
    Ensures(m_gl_name != 0);
}

Gl_sampler::~Gl_sampler()
{
    if (m_gl_name != 0)
    {
        gl::delete_samplers(1, &m_gl_name);
        m_gl_name = 0;
    }
}

Gl_framebuffer::Gl_framebuffer()
{
    gl::create_framebuffers(1, &m_gl_name);
    Ensures(m_gl_name != 0);
}

Gl_framebuffer::~Gl_framebuffer()
{
    if (m_gl_name != 0)
    {
        gl::delete_framebuffers(1, &m_gl_name);
        m_gl_name = 0;
    }
}

Gl_renderbuffer::Gl_renderbuffer()
{
    gl::create_renderbuffers(1, &m_gl_name);
    Ensures(m_gl_name != 0);
}

Gl_renderbuffer::~Gl_renderbuffer()
{
    if (m_gl_name != 0)
    {
        gl::delete_renderbuffers(1, &m_gl_name);
        m_gl_name = 0;
    }
}

Gl_buffer::Gl_buffer()
{
    gl::create_buffers(1, &m_gl_name);
    Ensures(m_gl_name != 0);
}

Gl_buffer::~Gl_buffer()
{
    if (m_gl_name != 0)
    {
        gl::delete_buffers(1, &m_gl_name);
        m_gl_name = 0;
    }
}

Gl_transform_feedback::Gl_transform_feedback()
{
    gl::create_transform_feedbacks(1, &m_gl_name);
    Ensures(m_gl_name != 0);
}

Gl_transform_feedback::~Gl_transform_feedback()
{
    if (m_gl_name != 0)
    {
        gl::delete_transform_feedbacks(1, &m_gl_name);
        m_gl_name = 0;
    }
}

Gl_query::Gl_query(gl::Query_target target)
{
    gl::create_queries(target, 1, &m_gl_name);
    Ensures(m_gl_name != 0);
}

Gl_query::~Gl_query()
{
    if (m_gl_name != 0)
    {
        gl::delete_queries(1, &m_gl_name);
        m_gl_name = 0;
    }
}

Gl_vertex_array::Gl_vertex_array()
{
    gl::create_vertex_arrays(1, &m_gl_name);
    Ensures(m_gl_name != 0);
}

Gl_vertex_array::~Gl_vertex_array()
{
    if (m_gl_name != 0)
    {
        gl::delete_vertex_arrays(1, &m_gl_name);
        m_gl_name = 0;
    }
}

} // namespace erhe::graphics
