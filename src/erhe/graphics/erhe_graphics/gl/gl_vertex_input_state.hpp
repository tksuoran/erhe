#pragma once

#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_graphics/gl/gl_objects.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_gl/wrapper_enums.hpp"
#include "erhe_profile/profile.hpp"

#include <mutex>
#include <optional>
#include <thread>

typedef int GLsizei;
typedef int GLint;

namespace erhe::graphics {

class Buffer;
class Device;

//// static constexpr int MAX_ATTRIBUTE_COUNT { 16 }; // TODO(tksuoran@gmail.com): Get rid of this kind of constant?

[[nodiscard]] auto get_vertex_divisor(erhe::dataformat::Vertex_step step) -> GLuint;
[[nodiscard]] auto get_gl_attribute_type(erhe::dataformat::Format format) -> gl::Attribute_type;
[[nodiscard]] auto get_gl_vertex_attrib_type(erhe::dataformat::Format format) -> gl::Vertex_attrib_type;
[[nodiscard]] auto get_gl_normalized(erhe::dataformat::Format format) -> bool;

class Vertex_input_state_impl
{
public:
    Vertex_input_state_impl(Device& device);
    Vertex_input_state_impl(Device& device, Vertex_input_state_data&& create_info);
    ~Vertex_input_state_impl() noexcept;

    void set    (const Vertex_input_state_data& data);
    auto gl_name() const -> unsigned int;
    void create ();
    void reset  ();
    void update ();

    [[nodiscard]] auto get_data() const -> const Vertex_input_state_data&;

    static void on_thread_enter();
    static void on_thread_exit();

private:
    Device&                        m_device;
    std::optional<Gl_vertex_array> m_gl_vertex_array;
    std::thread::id                m_owner_thread;
    Vertex_input_state_data        m_data;

    static ERHE_PROFILE_MUTEX_DECLARATION(std::mutex, s_mutex);
    static std::vector<Vertex_input_state_impl*>      s_all_vertex_input_states;
};

} // namespace erhe::graphics
