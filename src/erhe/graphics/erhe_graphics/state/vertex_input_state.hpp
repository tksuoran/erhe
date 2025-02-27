#pragma once

#include "erhe_graphics/gl_objects.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_gl/wrapper_enums.hpp"
#include "erhe_profile/profile.hpp"

#include <cstddef>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

typedef int GLsizei;
typedef int GLint;

namespace erhe::graphics {

class Buffer;

static constexpr int MAX_ATTRIBUTE_COUNT { 16 }; // TODO(tksuoran@gmail.com): Get rid of this kind of constant?

class Gl_vertex_input_attribute // VkVertexInputAttributeDescription
{
public:
    GLuint                 layout_location{0};
    GLuint                 binding;
    GLsizei                stride;
    GLint                  dimension;
    gl::Attribute_type     gl_attribute_type;
    gl::Vertex_attrib_type gl_vertex_atrib_type;
    bool                   normalized;
    GLuint                 offset;
    std::string            name;
};

class Vertex_input_binding // VkVertexInputBindingDescription
{
public:
    GLuint  binding;
    GLsizei stride;
    GLuint  divisor;
};

[[nodiscard]] auto get_vertex_divisor(erhe::dataformat::Vertex_step step) -> GLuint;

[[nodiscard]] auto get_gl_attribute_type(erhe::dataformat::Format format) -> gl::Attribute_type;

[[nodiscard]] auto get_gl_vertex_attrib_type(erhe::dataformat::Format format) -> gl::Vertex_attrib_type;

[[nodiscard]] auto get_gl_normalized(erhe::dataformat::Format format) -> bool;

class Vertex_input_state_data
{
public:
    std::vector<Gl_vertex_input_attribute> attributes;
    std::vector<Vertex_input_binding>      bindings;

    static auto make(const erhe::dataformat::Vertex_format& vertex_format) -> Vertex_input_state_data;
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

    [[nodiscard]] auto get_data() const -> const Vertex_input_state_data&;

    static void on_thread_enter();
    static void on_thread_exit();

private:
    std::optional<Gl_vertex_array> m_gl_vertex_array;
    std::thread::id                m_owner_thread;
    Vertex_input_state_data        m_data;

    static ERHE_PROFILE_MUTEX_DECLARATION(std::mutex, s_mutex);
    static std::vector<Vertex_input_state*>           s_all_vertex_input_states;
};

class Vertex_input_state_tracker
{
public:
    void reset  ();
    void execute(const Vertex_input_state* state);

    void set_index_buffer (erhe::graphics::Buffer* buffer) const;
    void set_vertex_buffer(uint32_t binding, const erhe::graphics::Buffer* buffer, std::size_t offset);

private:
    std::vector<Vertex_input_binding> m_bindings;
    unsigned int m_last{0};
};

} // namespace erhe::graphics
