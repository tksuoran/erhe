#pragma once

#include "erhe_graphics/gl_objects.hpp"
#include "erhe_graphics/vertex_attribute.hpp"

#include "erhe_gl/wrapper_enums.hpp"

#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

typedef int GLsizei;
typedef int GLint;

namespace erhe::graphics
{

class Buffer;
class Vertex_attribute_mapping;
class Vertex_attribute_mappings;
class Vertex_format;

static constexpr int MAX_ATTRIBUTE_COUNT { 16 }; // TODO(tksuoran@gmail.com): Get rid of this kind of constant?

class Vertex_input_attribute
{
public:
    GLuint                 layout_location{0};
    const Buffer*          vertex_buffer;
    GLsizei                stride;
    GLint                  dimension;
    gl::Attribute_type     shader_type;
    gl::Vertex_attrib_type data_type;
    bool                   normalized;
    GLuint                 offset;
    GLuint                 divisor;
};

class Vertex_input_state_data
{
public:
    const Buffer*                       index_buffer{nullptr};
    std::vector<Vertex_input_attribute> attributes;

    static auto make(
        const Vertex_attribute_mappings& mappings,
        const Vertex_format&             vertex_format,
        Buffer*                          vertex_buffer,
        Buffer*                          index_buffer
    ) -> Vertex_input_state_data;
};

class Vertex_input_state
{
public:
    Vertex_input_state();
    explicit Vertex_input_state(Vertex_input_state_data&& create_info);
    ~Vertex_input_state() noexcept;

    void set    (const Vertex_input_state_data& data);
    auto gl_name() const -> unsigned int;
    void create ();
    void reset  ();
    void update ();

    static void on_thread_enter();
    static void on_thread_exit();

private:
    std::optional<Gl_vertex_array> m_gl_vertex_array;
    std::thread::id                m_owner_thread;
    Vertex_input_state_data        m_data;

    static std::mutex                       s_mutex;
    static std::vector<Vertex_input_state*> s_all_vertex_input_states;
};

class Vertex_input_state_tracker
{
public:
    void reset  ();
    void execute(const Vertex_input_state* state);

private:
    unsigned int m_last{0};
};

} // namespace erhe::graphics
