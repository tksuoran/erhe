#ifndef gl_texture_hpp_erhe_graphics
#define gl_texture_hpp_erhe_graphics

#include "erhe/gl/strong_gl_enums.hpp"

namespace erhe::graphics
{

class Gl_texture
{
public:
    explicit Gl_texture(gl::Texture_target target);

    Gl_texture(const Gl_texture&) = delete;

    auto operator=(const Gl_texture&)
    -> Gl_texture& = delete;

    Gl_texture(Gl_texture&& other) noexcept
        : m_gl_name{other.m_gl_name}
    {
        other.m_gl_name = 0;
    }

    auto operator=(Gl_texture&& other) noexcept
    -> Gl_texture&
    {
        m_gl_name = other.m_gl_name;
        other.m_gl_name = 0;
        return *this;
    }

    ~Gl_texture();

    auto gl_name() const
    -> GLuint
    {
        return m_gl_name;
    }

private:
    GLuint m_gl_name{0};
};

class Gl_program
{
public:
    Gl_program();

    Gl_program(const Gl_program&) = delete;

    auto operator=(const Gl_program&)
    -> Gl_program& = delete;

    Gl_program(Gl_program&& other) noexcept
        : m_gl_name{other.m_gl_name}
    {
        other.m_gl_name = 0;
    }

    auto operator=(Gl_program&& other) noexcept
    -> Gl_program&
    {
        m_gl_name = other.m_gl_name;
        other.m_gl_name = 0;
        return *this;
    }

    ~Gl_program();

    auto gl_name() const
    -> GLuint
    {
        return m_gl_name;
    }

private:
    GLuint m_gl_name{0};
};

class Gl_shader
{
public:
    explicit Gl_shader(gl::Shader_type shader_type);

    Gl_shader(const Gl_shader&) = delete;

    auto operator=(const Gl_shader&)
    -> Gl_shader& = delete;

    Gl_shader(Gl_shader&& other) noexcept
        : m_gl_name{other.m_gl_name}
    {
        other.m_gl_name = 0;
    }

    auto operator=(Gl_shader&& other) noexcept
    -> Gl_shader&
    {
        m_gl_name = other.m_gl_name;
        other.m_gl_name = 0;
        return *this;
    }

    ~Gl_shader();

    auto gl_name() const
    -> unsigned int
    {
        return m_gl_name;
    }

private:
    GLuint m_gl_name{0};
};

class Gl_sampler
{
public:
    Gl_sampler();

    Gl_sampler(const Gl_sampler&) = delete;

    auto operator=(const Gl_sampler&)
    -> Gl_sampler& = delete;

    Gl_sampler(Gl_sampler&& other) noexcept
        : m_gl_name{other.m_gl_name}
    {
        other.m_gl_name = 0;
    }

    auto operator=(Gl_sampler&& other) noexcept
    -> Gl_sampler&
    {
        m_gl_name = other.m_gl_name;
        other.m_gl_name = 0;
        return *this;
    }

    ~Gl_sampler();

    auto gl_name() const
    -> unsigned int
    {
        return m_gl_name;
    }

private:
    GLuint m_gl_name{0};
};

class Gl_framebuffer
{
public:
    Gl_framebuffer();

    Gl_framebuffer(const Gl_framebuffer&) = delete;

    auto operator=(const Gl_framebuffer&)
    -> Gl_framebuffer& = delete;

    Gl_framebuffer(Gl_framebuffer&& other) noexcept
        : m_gl_name{other.m_gl_name}
    {
        other.m_gl_name = 0;
    }

    auto operator=(Gl_framebuffer&& other) noexcept
    -> Gl_framebuffer&
    {
        m_gl_name = other.m_gl_name;
        other.m_gl_name = 0;
        return *this;
    }

    ~Gl_framebuffer();

    auto gl_name() const
    -> GLuint
    {
        return m_gl_name;
    }

private:
    GLuint m_gl_name{0};
};

class Gl_renderbuffer
{
public:
    Gl_renderbuffer();

    Gl_renderbuffer(const Gl_renderbuffer&) = delete;

    auto operator=(const Gl_renderbuffer&)
    -> Gl_renderbuffer& = delete;

    Gl_renderbuffer(Gl_renderbuffer&& other) noexcept
        : m_gl_name{other.m_gl_name}
    {
        other.m_gl_name = 0;
    }

    auto operator=(Gl_renderbuffer&& other) noexcept
    -> Gl_renderbuffer&
    {
        m_gl_name = other.m_gl_name;
        other.m_gl_name = 0;
        return *this;
    }

    ~Gl_renderbuffer();

    auto gl_name() const
    -> GLuint
    {
        return m_gl_name;
    }

private:
    GLuint m_gl_name{0};
};

class Gl_buffer
{
public:
    Gl_buffer();

    Gl_buffer(const Gl_buffer&) = delete;

    auto operator=(const Gl_buffer&)
    -> Gl_buffer& = delete;

    Gl_buffer(Gl_buffer&& other) noexcept
        : m_gl_name{other.m_gl_name}
    {
        other.m_gl_name = 0;
    }

    auto operator=(Gl_buffer&& other) noexcept
    -> Gl_buffer&
    {
        m_gl_name = other.m_gl_name;
        other.m_gl_name = 0;
        return *this;
    }

    ~Gl_buffer();

    auto gl_name() const
    -> GLuint
    {
        return m_gl_name;
    }

private:
    GLuint m_gl_name{0};
};

class Gl_transform_feedback
{
public:
    Gl_transform_feedback();

    Gl_transform_feedback(const Gl_transform_feedback&) = delete;

    auto operator=(const Gl_transform_feedback&)
    -> Gl_transform_feedback& = delete;

    Gl_transform_feedback(Gl_transform_feedback&& other) noexcept
        : m_gl_name{other.m_gl_name}
    {
        other.m_gl_name = 0;
    }

    auto operator=(Gl_transform_feedback&& other) noexcept
    -> Gl_transform_feedback&
    {
        m_gl_name = other.m_gl_name;
        other.m_gl_name = 0;
        return *this;
    }

    ~Gl_transform_feedback();

    auto gl_name() const
    -> GLuint
    {
        return m_gl_name;
    }

private:
    GLuint m_gl_name{0};
};

class Gl_query
{
public:
    explicit Gl_query(gl::Query_target target);

    Gl_query(const Gl_query&) = delete;

    auto operator=(const Gl_query&)
    -> Gl_query& = delete;

    Gl_query(Gl_query&& other) noexcept
        : m_gl_name{other.m_gl_name}
    {
        other.m_gl_name = 0;
    }

    auto operator=(Gl_query&& other) noexcept
    -> Gl_query&
    {
        m_gl_name = other.m_gl_name;
        other.m_gl_name = 0;
        return *this;
    }

    ~Gl_query();

    auto gl_name() const
    -> GLuint
    {
        return m_gl_name;
    }

private:
    GLuint m_gl_name{0};
};

class Gl_vertex_array
{
public:
    Gl_vertex_array();

    Gl_vertex_array(const Gl_vertex_array&) = delete;

    auto operator=(const Gl_vertex_array&)
    -> Gl_vertex_array& = delete;

    Gl_vertex_array(Gl_vertex_array&& other) noexcept
        : m_gl_name{other.m_gl_name}
    {
        other.m_gl_name = 0;
    }

    auto operator=(Gl_vertex_array&& other) noexcept
    -> Gl_vertex_array&
    {
        m_gl_name = other.m_gl_name;
        other.m_gl_name = 0;
        return *this;
    }

    ~Gl_vertex_array();

    auto gl_name() const
    -> GLuint
    {
        return m_gl_name;
    }

private:
    GLuint m_gl_name{0};
};

} // namespace erhe::graphics

#endif
