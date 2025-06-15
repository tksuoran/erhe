#include "erhe_graphics/gl_objects.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_gl/enum_string_functions.hpp"
#include "erhe_verify/verify.hpp"

#include <new>
#include <utility>

namespace erhe::graphics {

Gl_texture::Gl_texture(Device& device, gl::Texture_target target, bool for_texture_view)
{
    static_cast<void>(device);

    if (!for_texture_view) {
        gl::create_textures(target, 1, &m_gl_name);
    } else {
        gl::gen_textures(1, &m_gl_name);
    }
    ERHE_VERIFY(m_gl_name != 0);
}

Gl_texture::Gl_texture(Device& device, gl::Texture_target target, GLuint wrap_name, bool for_texture_view)
    : m_gl_name{wrap_name}
    , m_owned  {wrap_name == 0}
{
    static_cast<void>(device);

    if (m_owned) {
        if (!for_texture_view) {
            gl::create_textures(target, 1, &m_gl_name);
        } else {
            gl::gen_textures(1, &m_gl_name);
        }
    }
    ERHE_VERIFY((wrap_name == 0) || (m_gl_name != 0));
}

Gl_texture::Gl_texture(Gl_texture&& old) noexcept
    : m_gl_name{std::exchange(old.m_gl_name, 0)}
    , m_owned  {std::exchange(old.m_owned, false)}
{
}

auto Gl_texture::operator=(Gl_texture&& old) noexcept -> Gl_texture&
{
    if (&old == this) {
        return *this;
    }
    this->~Gl_texture();
    return *new (this) Gl_texture(std::move(old));
}


Gl_texture::~Gl_texture() noexcept
{
    if (m_owned && (m_gl_name != 0)) {
        gl::delete_textures(1, &m_gl_name);
    }
}

auto Gl_texture::gl_name() const -> GLuint
{
    return m_gl_name;
}


Gl_program::Gl_program(Device&)
{
    m_gl_name = gl::create_program();
    ERHE_VERIFY(m_gl_name != 0);
}

Gl_program::Gl_program(Gl_program&& old) noexcept
    : m_gl_name{std::exchange(old.m_gl_name, 0)}
{
}

auto Gl_program::operator=(Gl_program&& old) noexcept -> Gl_program&
{
    if (&old == this) {
        return *this;
    }
    this->~Gl_program();
    return *new (this) Gl_program(std::move(old));
}

Gl_program::~Gl_program() noexcept
{
    if (m_gl_name != 0) {
        gl::delete_program(m_gl_name);
    }
}

auto Gl_program::gl_name() const -> GLuint
{
    return m_gl_name;
}

Gl_shader::Gl_shader(Device&, gl::Shader_type shader_type)
{
    m_gl_name = gl::create_shader(shader_type);
    ERHE_VERIFY(m_gl_name != 0);
}

Gl_shader::~Gl_shader() noexcept
{
    if (m_gl_name != 0) {
        gl::delete_shader(m_gl_name);
    }
}

Gl_shader::Gl_shader(Gl_shader&& old) noexcept
    : m_gl_name{std::exchange(old.m_gl_name, 0)}
{
}

auto Gl_shader::operator=(Gl_shader&& old) noexcept -> Gl_shader&
{
    if (&old == this) {
        return *this;
    }
    this->~Gl_shader();
    return *new (this) Gl_shader(std::move(old));
}

auto Gl_shader::gl_name() const -> unsigned int
{
    return m_gl_name;
}

Gl_sampler::Gl_sampler(Device& device)
{
    static_cast<void>(device);

    gl::create_samplers(1, &m_gl_name);
    ERHE_VERIFY(m_gl_name != 0);
}

Gl_sampler::~Gl_sampler() noexcept
{
    if (m_gl_name != 0) {
        gl::delete_samplers(1, &m_gl_name);
    }
}

Gl_sampler::Gl_sampler(Gl_sampler&& old) noexcept
    : m_gl_name{std::exchange(old.m_gl_name, 0)}
{
}

auto Gl_sampler::operator=(Gl_sampler&& old) noexcept -> Gl_sampler&
{
    if (&old == this) {
        return *this;
    }
    this->~Gl_sampler();
    return *new (this) Gl_sampler(std::move(old));
}

auto Gl_sampler::gl_name() const -> unsigned int
{
    return m_gl_name;
}

Gl_framebuffer::Gl_framebuffer(Device& device)
{
    static_cast<void>(device);

    gl::create_framebuffers(1, &m_gl_name);
    ERHE_VERIFY(m_gl_name != 0);
}

Gl_framebuffer::~Gl_framebuffer() noexcept
{
    if (m_gl_name != 0) {
        gl::delete_framebuffers(1, &m_gl_name);
    }
}

Gl_framebuffer::Gl_framebuffer(Gl_framebuffer&& old) noexcept
    : m_gl_name{std::exchange(old.m_gl_name, 0)}
{
}

auto Gl_framebuffer::operator=(Gl_framebuffer&& old) noexcept -> Gl_framebuffer&
{
    if (&old == this) {
        return *this;
    }
    this->~Gl_framebuffer();
    return *new (this) Gl_framebuffer(std::move(old));
}

auto Gl_framebuffer::gl_name() const -> GLuint
{
    return m_gl_name;
}

Gl_renderbuffer::Gl_renderbuffer(Device& device)
{
    static_cast<void>(device);

    gl::create_renderbuffers(1, &m_gl_name);
    ERHE_VERIFY(m_gl_name != 0);
}

Gl_renderbuffer::~Gl_renderbuffer() noexcept
{
    if (m_gl_name != 0) {
        gl::delete_renderbuffers(1, &m_gl_name);
    }
}

Gl_renderbuffer::Gl_renderbuffer(Gl_renderbuffer&& old) noexcept
    : m_gl_name{std::exchange(old.m_gl_name, 0)}
{
}

auto Gl_renderbuffer::operator=(Gl_renderbuffer&& old) noexcept -> Gl_renderbuffer&
{
    if (&old == this) {
        return *this;
    }
    this->~Gl_renderbuffer();
    return *new (this) Gl_renderbuffer(std::move(old));
}

auto Gl_renderbuffer::gl_name() const -> GLuint
{
    return m_gl_name;
}

Gl_buffer::Gl_buffer(Device& device)
{
    static_cast<void>(device);

    gl::create_buffers(1, &m_gl_name);
    ERHE_VERIFY(m_gl_name != 0);
}

Gl_buffer::~Gl_buffer() noexcept
{
    if (m_gl_name != 0) {
        gl::delete_buffers(1, &m_gl_name);
    }
}

Gl_buffer::Gl_buffer(Gl_buffer&& old) noexcept
    : m_gl_name{std::exchange(old.m_gl_name, 0)}
{
}

auto Gl_buffer::operator=(Gl_buffer&& old) noexcept -> Gl_buffer&
{
    if (&old == this) {
        return *this;
    }
    this->~Gl_buffer();
    return *new (this) Gl_buffer(std::move(old));
}

auto Gl_buffer::gl_name() const -> GLuint
{
    return m_gl_name;
}


Gl_transform_feedback::Gl_transform_feedback(Device& device)
{
    static_cast<void>(device);

    gl::create_transform_feedbacks(1, &m_gl_name);
    ERHE_VERIFY(m_gl_name != 0);
}

Gl_transform_feedback::~Gl_transform_feedback() noexcept
{
    if (m_gl_name != 0) {
        gl::delete_transform_feedbacks(1, &m_gl_name);
        m_gl_name = 0;
    }
}

Gl_transform_feedback::Gl_transform_feedback(Gl_transform_feedback&& old) noexcept
    : m_gl_name{std::exchange(old.m_gl_name, 0)}
{
}

auto Gl_transform_feedback::operator=(Gl_transform_feedback&& old) noexcept -> Gl_transform_feedback&
{
    if (&old == this) {
        return *this;
    }
    this->~Gl_transform_feedback();
    return *new (this) Gl_transform_feedback(std::move(old));
}

auto Gl_transform_feedback::gl_name() const -> GLuint
{
    return m_gl_name;
}

Gl_query::Gl_query(Device& device, gl::Query_target target)
{
    static_cast<void>(device);

    gl::create_queries(target, 1, &m_gl_name);
    ERHE_VERIFY(m_gl_name != 0);
}

Gl_query::~Gl_query() noexcept
{
    if (m_gl_name != 0) {
        gl::delete_queries(1, &m_gl_name);
    }
}

Gl_query::Gl_query(Gl_query&& old) noexcept
    : m_gl_name{std::exchange(old.m_gl_name, 0)}
{
}

auto Gl_query::operator=(Gl_query&& old) noexcept -> Gl_query&
{
    if (&old == this) {
        return *this;
    }
    this->~Gl_query();
    return *new (this) Gl_query(std::move(old));
}

auto Gl_query::gl_name() const -> GLuint
{
    return m_gl_name;
}

Gl_vertex_array::Gl_vertex_array(Device& device)
{
    static_cast<void>(device);

    gl::create_vertex_arrays(1, &m_gl_name);
    ERHE_VERIFY(m_gl_name != 0);
}

Gl_vertex_array::~Gl_vertex_array() noexcept
{
    if (m_gl_name != 0) {
        gl::delete_vertex_arrays(1, &m_gl_name);
    }
}

Gl_vertex_array::Gl_vertex_array(Gl_vertex_array&& old) noexcept
    : m_gl_name{std::exchange(old.m_gl_name, 0)}
{
}

auto Gl_vertex_array::operator=(Gl_vertex_array&& old) noexcept -> Gl_vertex_array&
{
    if (&old == this) {
        return *this;
    }
    this->~Gl_vertex_array();
    return *new (this) Gl_vertex_array(std::move(old));
}

auto Gl_vertex_array::gl_name() const -> GLuint
{
    return m_gl_name;
}

} // namespace erhe::graphics
