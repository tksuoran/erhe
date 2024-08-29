#pragma once

#include "erhe_gl/wrapper_enums.hpp"

typedef unsigned int GLuint;

namespace erhe::graphics {

class Gl_texture final
{
public:
    static constexpr bool not_texture_view = false;
    static constexpr bool texture_view     = true;
    explicit Gl_texture(gl::Texture_target target, bool for_texture_view = not_texture_view);
    Gl_texture         (gl::Texture_target target, GLuint wrap_name, bool for_texture_view = not_texture_view);
    ~Gl_texture        () noexcept;
    Gl_texture         (const Gl_texture&) = delete;
    void operator=     (const Gl_texture&) = delete;
    Gl_texture         (Gl_texture&& old) noexcept;
    auto operator=     (Gl_texture&& old) noexcept -> Gl_texture&;

    [[nodiscard]] auto gl_name() const -> GLuint;

private:
    GLuint m_gl_name{0};
    bool   m_owned  {true};
};

class Gl_program final
{
public:
    Gl_program    ();
    ~Gl_program   () noexcept;
    Gl_program    (const Gl_program&) = delete;
    void operator=(const Gl_program&) = delete;
    Gl_program    (Gl_program&& other) noexcept;
    auto operator=(Gl_program&& other) noexcept -> Gl_program&;

    [[nodiscard]] auto gl_name() const -> GLuint;

private:
    GLuint m_gl_name{0};
};

class Gl_shader final
{
public:
    explicit Gl_shader(gl::Shader_type shader_type);
    ~Gl_shader        () noexcept;
    Gl_shader         (const Gl_shader&) = delete;
    void operator=    (const Gl_shader&) = delete;
    Gl_shader         (Gl_shader&& other) noexcept;
    auto operator=    (Gl_shader&& other) noexcept -> Gl_shader&;

    [[nodiscard]] auto gl_name() const -> unsigned int;

private:
    GLuint m_gl_name{0};
};

class Gl_sampler final
{
public:
    Gl_sampler    ();
    ~Gl_sampler   () noexcept;
    Gl_sampler    (const Gl_sampler&) = delete;
    void operator=(const Gl_sampler&) = delete;
    Gl_sampler    (Gl_sampler&& other) noexcept;
    auto operator=(Gl_sampler&& other) noexcept -> Gl_sampler&;

    [[nodiscard]] auto gl_name() const -> unsigned int;

private:
    GLuint m_gl_name{0};
};

class Gl_framebuffer final
{
public:
    Gl_framebuffer ();
    ~Gl_framebuffer() noexcept;
    Gl_framebuffer (const Gl_framebuffer&) = delete;
    void operator= (const Gl_framebuffer&) = delete;
    Gl_framebuffer (Gl_framebuffer&& other) noexcept;
    auto operator= (Gl_framebuffer&& other) noexcept -> Gl_framebuffer&;

    [[nodiscard]] auto gl_name() const -> GLuint;

private:
    GLuint m_gl_name{0};
};

class Gl_renderbuffer final
{
public:
    Gl_renderbuffer ();
    ~Gl_renderbuffer() noexcept;
    Gl_renderbuffer (const Gl_renderbuffer&) = delete;
    void operator=  (const Gl_renderbuffer&) = delete;
    Gl_renderbuffer (Gl_renderbuffer&& other) noexcept;
    auto operator=  (Gl_renderbuffer&& other) noexcept -> Gl_renderbuffer&;

    [[nodiscard]] auto gl_name() const -> GLuint;

private:
    GLuint m_gl_name{0};
};

class Gl_buffer final
{
public:
    Gl_buffer     ();
    ~Gl_buffer    () noexcept;
    Gl_buffer     (const Gl_buffer&) = delete;
    void operator=(const Gl_buffer&) = delete;
    Gl_buffer     (Gl_buffer&& other) noexcept;
    auto operator=(Gl_buffer&& other) noexcept -> Gl_buffer&;

    [[nodiscard]] auto gl_name() const -> GLuint;

private:
    GLuint m_gl_name{0};
};

class Gl_transform_feedback final
{
public:
    Gl_transform_feedback ();
    ~Gl_transform_feedback() noexcept;
    Gl_transform_feedback (const Gl_transform_feedback&) = delete;
    void operator=        (const Gl_transform_feedback&) = delete;
    Gl_transform_feedback (Gl_transform_feedback&& other) noexcept;
    auto operator=        (Gl_transform_feedback&& other) noexcept -> Gl_transform_feedback&;

    [[nodiscard]] auto gl_name() const -> GLuint;

private:
    GLuint m_gl_name{0};
};

class Gl_query final
{
public:
    explicit Gl_query(gl::Query_target target);
    ~Gl_query        () noexcept;
    Gl_query         (const Gl_query&) = delete;
    void operator=   (const Gl_query&) = delete;
    Gl_query         (Gl_query&& other) noexcept;
    auto operator=   (Gl_query&& other) noexcept -> Gl_query&;

    [[nodiscard]] auto gl_name() const -> GLuint;

private:
    GLuint m_gl_name{0};
};

class Gl_vertex_array final
{
public:
    Gl_vertex_array ();
    ~Gl_vertex_array() noexcept;
    Gl_vertex_array (const Gl_vertex_array&) = delete;
    void operator=  (const Gl_vertex_array&) = delete;
    Gl_vertex_array (Gl_vertex_array&& other) noexcept;
    auto operator=  (Gl_vertex_array&& other) noexcept -> Gl_vertex_array&;

    [[nodiscard]] auto gl_name() const -> GLuint;

private:
    GLuint m_gl_name{0};
};

} // namespace erhe::graphics
