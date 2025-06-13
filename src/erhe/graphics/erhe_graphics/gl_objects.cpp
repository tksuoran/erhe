#include "erhe_graphics/gl_objects.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_gl/enum_string_functions.hpp"
#include "erhe_verify/verify.hpp"

#include <new>
#include <utility>

namespace erhe::graphics {

[[nodiscard]] auto get_p_name_from_texture_target(gl::Texture_target target) -> gl::Get_p_name
{
    switch (target) {
        case gl::Texture_target::texture_2d:                   return gl::Get_p_name::texture_binding_2d;
        case gl::Texture_target::texture_2d_array:             return gl::Get_p_name::texture_binding_2d_array;
        case gl::Texture_target::texture_2d_multisample:       return gl::Get_p_name::texture_binding_2d_multisample;
        case gl::Texture_target::texture_2d_multisample_array: return gl::Get_p_name::texture_binding_2d_multisample_array;
        case gl::Texture_target::texture_cube_map:             return gl::Get_p_name::texture_binding_cube_map;
        case gl::Texture_target::texture_cube_map_array:       ERHE_FATAL("TODO"); return gl::Get_p_name{0};
        case gl::Texture_target::texture_3d:                   return gl::Get_p_name::texture_binding_3d;
        default: ERHE_FATAL("TODO");
    }
}


Gl_texture::Gl_texture(gl::Texture_target target, bool for_texture_view)
{
#if defined(ERHE_USE_OPENGL_DIRECT_STATE_ACCESS)
    if (!for_texture_view) {
        gl::create_textures(target, 1, &m_gl_name);
    } else
#else
    static_cast<void>(for_texture_view);
#endif
    {
        gl::gen_textures(1, &m_gl_name);
        int current_texture_binding = 0;
        gl::get_integer_v(get_p_name_from_texture_target(target), &current_texture_binding);
        gl::bind_texture(target, m_gl_name);
        gl::bind_texture(target, current_texture_binding);
    }
    ERHE_VERIFY(m_gl_name != 0);
}

Gl_texture::Gl_texture(gl::Texture_target target, GLuint wrap_name, bool for_texture_view)
    : m_gl_name{wrap_name}
    , m_owned  {wrap_name == 0}
{
    if (m_owned) {
#if defined(ERHE_USE_OPENGL_DIRECT_STATE_ACCESS)
        if (!for_texture_view) {
            gl::create_textures(target, 1, &m_gl_name);
        } else
#else
        static_cast<void>(for_texture_view);
#endif
        {
            gl::gen_textures(1, &m_gl_name);
            int current_texture_binding = 0;
            gl::get_integer_v(get_p_name_from_texture_target(target), &current_texture_binding);
            gl::bind_texture(target, m_gl_name);
            gl::bind_texture(target, current_texture_binding);
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


Gl_program::Gl_program()
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

Gl_shader::Gl_shader(gl::Shader_type shader_type)
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

Gl_sampler::Gl_sampler()
{
#if defined(ERHE_USE_OPENGL_DIRECT_STATE_ACCESS)
    gl::create_samplers(1, &m_gl_name);
#else
    gl::gen_samplers(1, &m_gl_name);
#endif
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

Gl_framebuffer::Gl_framebuffer()
{
#if defined(ERHE_USE_OPENGL_DIRECT_STATE_ACCESS)
    gl::create_framebuffers(1, &m_gl_name);
#else
    // DSA Emulation
    gl::gen_framebuffers(1, &m_gl_name);
    int current_read_framebuffer = 0;
    gl::get_integer_v(gl::Get_p_name::read_framebuffer_binding, &current_read_framebuffer);
    gl::bind_framebuffer(gl::Framebuffer_target::read_framebuffer, m_gl_name);
    gl::bind_framebuffer(gl::Framebuffer_target::read_framebuffer, current_read_framebuffer);
#endif
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

Gl_renderbuffer::Gl_renderbuffer()
{
#if defined(ERHE_USE_OPENGL_DIRECT_STATE_ACCESS)
    gl::create_renderbuffers(1, &m_gl_name);
#else
    // DSA emulation
    gl::gen_renderbuffers(1, &m_gl_name);
#endif
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

Gl_buffer::Gl_buffer()
{
#if defined(ERHE_USE_OPENGL_DIRECT_STATE_ACCESS)
    gl::create_buffers(1, &m_gl_name);
#else
    gl::gen_buffers(1, &m_gl_name);
    int current_buffer = 0;
    gl::get_integer_v(gl::Get_p_name::uniform_buffer_binding, &current_buffer);
    gl::bind_buffer(gl::Buffer_target::copy_read_buffer, m_gl_name);
    gl::bind_buffer(gl::Buffer_target::copy_read_buffer, current_buffer);
#endif
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


Gl_transform_feedback::Gl_transform_feedback()
{
#if defined(ERHE_USE_OPENGL_DIRECT_STATE_ACCESS)
    gl::create_transform_feedbacks(1, &m_gl_name);
#else
    // TODO
#endif
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

Gl_query::Gl_query(gl::Query_target target)
{
#if defined(ERHE_USE_OPENGL_DIRECT_STATE_ACCESS)
    gl::create_queries(target, 1, &m_gl_name);
#else
    gl::gen_queries(1, &m_gl_name);
    static_cast<void>(target);
#endif
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

Gl_vertex_array::Gl_vertex_array()
{
#if defined(ERHE_USE_OPENGL_DIRECT_STATE_ACCESS)
    gl::create_vertex_arrays(1, &m_gl_name);
#else
    int current_vao = 0;
    gl::gen_vertex_arrays(1, &m_gl_name);
    gl::get_integer_v(gl::Get_p_name::vertex_array_binding, &current_vao);
    gl::bind_vertex_array(m_gl_name);
    gl::bind_vertex_array(current_vao);
#endif
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
