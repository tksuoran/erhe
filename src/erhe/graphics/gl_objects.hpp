#pragma once

#include "erhe/gl/strong_gl_enums.hpp"

namespace erhe::graphics
{

class Gl_texture final
{
public:
    explicit Gl_texture(gl::Texture_target target);
    explicit Gl_texture(gl::Texture_target target, GLuint wrap_name);
    ~Gl_texture        ();
    Gl_texture         (const Gl_texture&) = delete;
    void operator=     (const Gl_texture&) = delete;
    Gl_texture         (Gl_texture&& other) noexcept;
    auto operator=     (Gl_texture&& other) noexcept -> Gl_texture&;

    auto gl_name       () const -> GLuint;

private:
    GLuint m_gl_name{0};
    bool   m_owned  {true};
};

class Gl_program final
{
public:
    Gl_program    ();
    ~Gl_program   ();
    Gl_program    (const Gl_program&) = delete;
    void operator=(const Gl_program&) = delete;
    Gl_program    (Gl_program&& other) noexcept;
    auto operator=(Gl_program&& other) noexcept -> Gl_program&;

    auto gl_name() const -> GLuint;

private:
    GLuint m_gl_name{0};
};

class Gl_shader final
{
public:
    explicit Gl_shader(gl::Shader_type shader_type);
    ~Gl_shader        ();
    Gl_shader         (const Gl_shader&) = delete;
    void operator=    (const Gl_shader&) = delete;
    Gl_shader         (Gl_shader&& other) noexcept;
    auto operator=    (Gl_shader&& other) noexcept -> Gl_shader&;

    auto gl_name      () const -> unsigned int;

private:
    GLuint m_gl_name{0};
};

class Gl_sampler final
{
public:
    Gl_sampler    ();
    ~Gl_sampler   ();
    Gl_sampler    (const Gl_sampler&) = delete;
    void operator=(const Gl_sampler&) = delete;
    Gl_sampler    (Gl_sampler&& other) noexcept;
    auto operator=(Gl_sampler&& other) noexcept -> Gl_sampler&;

    auto gl_name  () const -> unsigned int;

private:
    GLuint m_gl_name{0};
};

class Gl_framebuffer final
{
public:
    Gl_framebuffer ();
    ~Gl_framebuffer();
    Gl_framebuffer (const Gl_framebuffer&) = delete;
    void operator= (const Gl_framebuffer&) = delete;
    Gl_framebuffer (Gl_framebuffer&& other) noexcept;
    auto operator= (Gl_framebuffer&& other) noexcept -> Gl_framebuffer&;


    auto gl_name   () const -> GLuint;

private:
    GLuint m_gl_name{0};
};

class Gl_renderbuffer final
{
public:
    Gl_renderbuffer ();
    ~Gl_renderbuffer();
    Gl_renderbuffer (const Gl_renderbuffer&) = delete;
    void operator=  (const Gl_renderbuffer&) = delete;
    Gl_renderbuffer (Gl_renderbuffer&& other) noexcept;
    auto operator=  (Gl_renderbuffer&& other) noexcept -> Gl_renderbuffer&;

    auto gl_name    () const -> GLuint;

private:
    GLuint m_gl_name{0};
};

class Gl_buffer final
{
public:
    Gl_buffer     ();
    ~Gl_buffer    ();
    Gl_buffer     (const Gl_buffer&) = delete;
    void operator=(const Gl_buffer&) = delete;
    Gl_buffer     (Gl_buffer&& other) noexcept;
    auto operator=(Gl_buffer&& other) noexcept -> Gl_buffer&;

    auto gl_name  () const -> GLuint;

private:
    GLuint m_gl_name{0};
};

class Gl_transform_feedback final
{
public:
    Gl_transform_feedback ();
    ~Gl_transform_feedback();
    Gl_transform_feedback (const Gl_transform_feedback&) = delete;
    void operator=        (const Gl_transform_feedback&) = delete;
    Gl_transform_feedback (Gl_transform_feedback&& other) noexcept;
    auto operator=        (Gl_transform_feedback&& other) noexcept -> Gl_transform_feedback&;

    auto gl_name          () const -> GLuint;

private:
    GLuint m_gl_name{0};
};

class Gl_query final
{
public:
    explicit Gl_query(gl::Query_target target);
    ~Gl_query        ();
    Gl_query         (const Gl_query&) = delete;
    void operator=   (const Gl_query&) = delete;
    Gl_query         (Gl_query&& other) noexcept;
    auto operator=   (Gl_query&& other) noexcept -> Gl_query&;

    auto gl_name     () const -> GLuint;

private:
    GLuint m_gl_name{0};
};

class Gl_vertex_array final
{
public:
    Gl_vertex_array ();
    ~Gl_vertex_array();
    Gl_vertex_array (const Gl_vertex_array&) = delete;
    void operator=  (const Gl_vertex_array&) = delete;
    Gl_vertex_array (Gl_vertex_array&& other) noexcept;
    auto operator=  (Gl_vertex_array&& other) noexcept -> Gl_vertex_array&;

    auto gl_name    () const -> GLuint;

private:
    GLuint m_gl_name{0};
};

} // namespace erhe::graphics
