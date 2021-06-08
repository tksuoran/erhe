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

Gl_texture::Gl_texture(gl::Texture_target target, GLuint wrap_name)
    : m_gl_name{wrap_name}
    , m_owned  {wrap_name != 0}
{
    if (!m_owned)
    {
        gl::create_textures(target, 1, &m_gl_name);
    }
    Ensures(m_gl_name != 0);
}

Gl_texture::Gl_texture(Gl_texture&& other) noexcept
    : m_gl_name{other.m_gl_name}
    , m_owned  {other.m_owned}
{
    other.m_gl_name = 0;
    other.m_owned   = false;
}

auto Gl_texture::operator=(Gl_texture&& other) noexcept -> Gl_texture&
{
    m_gl_name = other.m_gl_name;
    m_owned   = other.m_owned;
    other.m_gl_name = 0;
    other.m_owned   = false;
    return *this;
}


Gl_texture::~Gl_texture()
{
    if (m_owned && (m_gl_name != 0))
    {
        gl::delete_textures(1, &m_gl_name);
    }
}

auto Gl_texture::gl_name() const -> GLuint
{
    return m_gl_name;
}


Gl_program::Gl_program()
{
    m_gl_name = gl::create_program();
    Ensures(m_gl_name != 0);
}

Gl_program::Gl_program(Gl_program&& other) noexcept
    : m_gl_name{other.m_gl_name}
{
    other.m_gl_name = 0;
}

auto Gl_program::operator=(Gl_program&& other) noexcept
-> Gl_program&
{
    m_gl_name = other.m_gl_name;
    other.m_gl_name = 0;
    return *this;
}

Gl_program::~Gl_program()
{
    if (m_gl_name != 0)
    {
        gl::delete_program(m_gl_name);
        m_gl_name = 0;
    }
}

auto Gl_program::gl_name() const -> GLuint
{
    return m_gl_name;
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

Gl_shader::Gl_shader(Gl_shader&& other) noexcept
    : m_gl_name{other.m_gl_name}
{
    other.m_gl_name = 0;
}

auto Gl_shader::operator=(Gl_shader&& other) noexcept -> Gl_shader&
{
    m_gl_name = other.m_gl_name;
    other.m_gl_name = 0;
    return *this;
}

auto Gl_shader::gl_name() const
-> unsigned int
{
    return m_gl_name;
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

Gl_sampler::Gl_sampler(Gl_sampler&& other) noexcept
    : m_gl_name{other.m_gl_name}
{
    other.m_gl_name = 0;
}

auto Gl_sampler::operator=(Gl_sampler&& other) noexcept
-> Gl_sampler&
{
    m_gl_name = other.m_gl_name;
    other.m_gl_name = 0;
    return *this;
}

auto Gl_sampler::gl_name() const
-> unsigned int
{
    return m_gl_name;
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

Gl_framebuffer::Gl_framebuffer(Gl_framebuffer&& other) noexcept
    : m_gl_name{other.m_gl_name}
{
    other.m_gl_name = 0;
}

auto Gl_framebuffer::operator=(Gl_framebuffer&& other) noexcept
-> Gl_framebuffer&
{
    m_gl_name = other.m_gl_name;
    other.m_gl_name = 0;
    return *this;
}

auto Gl_framebuffer::gl_name() const
-> GLuint
{
    return m_gl_name;
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

Gl_renderbuffer::Gl_renderbuffer(Gl_renderbuffer&& other) noexcept
    : m_gl_name{other.m_gl_name}
{
    other.m_gl_name = 0;
}

auto Gl_renderbuffer::operator=(Gl_renderbuffer&& other) noexcept
-> Gl_renderbuffer&
{
    m_gl_name = other.m_gl_name;
    other.m_gl_name = 0;
    return *this;
}

auto Gl_renderbuffer::gl_name() const
-> GLuint
{
    return m_gl_name;
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

Gl_buffer::Gl_buffer(Gl_buffer&& other) noexcept
    : m_gl_name{other.m_gl_name}
{
    other.m_gl_name = 0;
}

auto Gl_buffer::operator=(Gl_buffer&& other) noexcept
-> Gl_buffer&
{
    m_gl_name = other.m_gl_name;
    other.m_gl_name = 0;
    return *this;
}

auto Gl_buffer::gl_name() const
-> GLuint
{
    return m_gl_name;
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

Gl_transform_feedback::Gl_transform_feedback(Gl_transform_feedback&& other) noexcept
    : m_gl_name{other.m_gl_name}
{
    other.m_gl_name = 0;
}

auto Gl_transform_feedback::operator=(Gl_transform_feedback&& other) noexcept
-> Gl_transform_feedback&
{
    m_gl_name = other.m_gl_name;
    other.m_gl_name = 0;
    return *this;
}

auto Gl_transform_feedback::gl_name() const
-> GLuint
{
    return m_gl_name;
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

Gl_query::Gl_query(Gl_query&& other) noexcept
    : m_gl_name{other.m_gl_name}
{
    other.m_gl_name = 0;
}

auto Gl_query::operator=(Gl_query&& other) noexcept
-> Gl_query&
{
    m_gl_name = other.m_gl_name;
    other.m_gl_name = 0;
    return *this;
}

auto Gl_query::gl_name() const
-> GLuint
{
    return m_gl_name;
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

Gl_vertex_array::Gl_vertex_array(Gl_vertex_array&& other) noexcept
    : m_gl_name{other.m_gl_name}
{
    other.m_gl_name = 0;
}

auto Gl_vertex_array::operator=(Gl_vertex_array&& other) noexcept
-> Gl_vertex_array&
{
    m_gl_name = other.m_gl_name;
    other.m_gl_name = 0;
    return *this;
}

auto Gl_vertex_array::gl_name() const
-> GLuint
{
    return m_gl_name;
}

} // namespace erhe::graphics
