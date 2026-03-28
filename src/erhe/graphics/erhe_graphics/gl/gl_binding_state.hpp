#pragma once

#include "erhe_gl/wrapper_enums.hpp"

#include <array>
#include <cstddef>
#include <vector>

typedef unsigned int GLuint;

namespace erhe::graphics {

class Gl_binding_state;

// RAII scope guards - returned by push_* methods, pop on destruction.

class Buffer_binding_guard final
{
public:
    Buffer_binding_guard(Gl_binding_state& state, gl::Buffer_target target);
    ~Buffer_binding_guard() noexcept;
    Buffer_binding_guard(const Buffer_binding_guard&) = delete;
    void operator=(const Buffer_binding_guard&) = delete;
    Buffer_binding_guard(Buffer_binding_guard&& other) noexcept;
    void operator=(Buffer_binding_guard&&) = delete;

private:
    Gl_binding_state* m_state;
    gl::Buffer_target m_target;
};

class Texture_binding_guard final
{
public:
    Texture_binding_guard(Gl_binding_state& state, GLuint unit, gl::Texture_target target);
    ~Texture_binding_guard() noexcept;
    Texture_binding_guard(const Texture_binding_guard&) = delete;
    void operator=(const Texture_binding_guard&) = delete;
    Texture_binding_guard(Texture_binding_guard&& other) noexcept;
    void operator=(Texture_binding_guard&&) = delete;

private:
    Gl_binding_state*  m_state;
    GLuint             m_unit;
    gl::Texture_target m_target;
};

class Framebuffer_binding_guard final
{
public:
    Framebuffer_binding_guard(Gl_binding_state& state, gl::Framebuffer_target target);
    ~Framebuffer_binding_guard() noexcept;
    Framebuffer_binding_guard(const Framebuffer_binding_guard&) = delete;
    void operator=(const Framebuffer_binding_guard&) = delete;
    Framebuffer_binding_guard(Framebuffer_binding_guard&& other) noexcept;
    void operator=(Framebuffer_binding_guard&&) = delete;

private:
    Gl_binding_state*      m_state;
    gl::Framebuffer_target m_target;
};

class Renderbuffer_binding_guard final
{
public:
    explicit Renderbuffer_binding_guard(Gl_binding_state& state);
    ~Renderbuffer_binding_guard() noexcept;
    Renderbuffer_binding_guard(const Renderbuffer_binding_guard&) = delete;
    void operator=(const Renderbuffer_binding_guard&) = delete;
    Renderbuffer_binding_guard(Renderbuffer_binding_guard&& other) noexcept;
    void operator=(Renderbuffer_binding_guard&&) = delete;

private:
    Gl_binding_state* m_state;
};

class Vertex_array_binding_guard final
{
public:
    explicit Vertex_array_binding_guard(Gl_binding_state& state);
    ~Vertex_array_binding_guard() noexcept;
    Vertex_array_binding_guard(const Vertex_array_binding_guard&) = delete;
    void operator=(const Vertex_array_binding_guard&) = delete;
    Vertex_array_binding_guard(Vertex_array_binding_guard&& other) noexcept;
    void operator=(Vertex_array_binding_guard&&) = delete;

private:
    Gl_binding_state* m_state;
};

// Shadows OpenGL binding state to avoid querying GL and to allow
// save/restore when DSA emulation needs to temporarily bind objects.
//
// Push methods save the current binding onto a stack, bind the new value,
// and return an RAII guard that pops the stack (restoring the old binding)
// on destruction.

class Gl_binding_state
{
public:
    Gl_binding_state();

    void reset();

    // -- Buffers ----------------------------------------------------------
    auto push_buffer        (gl::Buffer_target target, GLuint buffer) -> Buffer_binding_guard;
    void pop_buffer         (gl::Buffer_target target);
    void bind_buffer        (gl::Buffer_target target, GLuint buffer);
    auto get_bound_buffer   (gl::Buffer_target target) const -> GLuint;

    // -- Textures ---------------------------------------------------------
    auto push_texture       (GLuint unit, gl::Texture_target target, GLuint texture) -> Texture_binding_guard;
    void pop_texture        (GLuint unit, gl::Texture_target target);
    void bind_texture       (GLuint unit, gl::Texture_target target, GLuint texture);
    auto get_bound_texture  (GLuint unit, gl::Texture_target target) const -> GLuint;

    // -- Framebuffers -----------------------------------------------------
    auto push_framebuffer   (gl::Framebuffer_target target, GLuint framebuffer) -> Framebuffer_binding_guard;
    void pop_framebuffer    (gl::Framebuffer_target target);
    void bind_framebuffer   (gl::Framebuffer_target target, GLuint framebuffer);
    auto get_draw_framebuffer() const -> GLuint;
    auto get_read_framebuffer() const -> GLuint;

    // -- Renderbuffers ----------------------------------------------------
    auto push_renderbuffer  (GLuint renderbuffer) -> Renderbuffer_binding_guard;
    void pop_renderbuffer   ();
    void bind_renderbuffer  (GLuint renderbuffer);
    auto get_bound_renderbuffer() const -> GLuint;

    // -- Vertex Arrays ----------------------------------------------------
    auto push_vertex_array  (GLuint vao) -> Vertex_array_binding_guard;
    void pop_vertex_array   ();
    void bind_vertex_array  (GLuint vao);
    auto get_bound_vertex_array() const -> GLuint;

    // -- Samplers ---------------------------------------------------------
    void bind_sampler       (GLuint unit, GLuint sampler);
    auto get_bound_sampler  (GLuint unit) const -> GLuint;

    // -- Programs ---------------------------------------------------------
    void use_program        (GLuint program);
    auto get_current_program() const -> GLuint;

    static constexpr std::size_t s_max_texture_units = 32;

private:
    auto buffer_target_to_index (gl::Buffer_target target) const -> std::size_t;
    auto texture_target_to_index(gl::Texture_target target) const -> std::size_t;

    // element_array_buffer is excluded - it is part of VAO state
    static constexpr std::size_t s_buffer_target_count  = 11;
    static constexpr std::size_t s_texture_target_count = 10;

    // Per-target buffer bindings (current + stack)
    std::array<GLuint, s_buffer_target_count>              m_bound_buffers{};
    std::array<std::vector<GLuint>, s_buffer_target_count> m_buffer_stack;

    // Active texture unit
    GLuint m_active_texture_unit{0};

    // Per-unit, per-target texture bindings (current + stack)
    std::array<std::array<GLuint, s_texture_target_count>, s_max_texture_units>              m_bound_textures{};
    std::array<std::array<std::vector<GLuint>, s_texture_target_count>, s_max_texture_units> m_texture_stack;

    // Per-unit sampler bindings
    std::array<GLuint, s_max_texture_units> m_bound_samplers{};

    // Framebuffer bindings (current + stacks)
    GLuint              m_draw_framebuffer{0};
    GLuint              m_read_framebuffer{0};
    std::vector<GLuint> m_draw_framebuffer_stack;
    std::vector<GLuint> m_read_framebuffer_stack;

    // Renderbuffer binding (current + stack)
    GLuint              m_bound_renderbuffer{0};
    std::vector<GLuint> m_renderbuffer_stack;

    // Vertex array binding (current + stack)
    GLuint              m_bound_vertex_array{0};
    std::vector<GLuint> m_vertex_array_stack;

    // Program binding
    GLuint m_current_program{0};
};

} // namespace erhe::graphics
