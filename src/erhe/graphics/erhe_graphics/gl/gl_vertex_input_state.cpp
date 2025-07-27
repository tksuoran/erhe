#include <fmt/core.h>
#include <fmt/ostream.h>

#include "erhe_graphics/gl/gl_vertex_input_state.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_verify/verify.hpp"

#include <memory>
#include <sstream>

namespace erhe::graphics {

ERHE_PROFILE_MUTEX(std::mutex,   Vertex_input_state_impl::s_mutex);
std::vector<Vertex_input_state_impl*> Vertex_input_state_impl::s_all_vertex_input_states;

void Vertex_input_state_impl::on_thread_enter()
{
    const std::lock_guard lock{s_mutex};

    std::stringstream this_thread_id_ss;
    this_thread_id_ss << std::this_thread::get_id();
    const std::string this_thread_id_string = this_thread_id_ss.str();

    for (auto* vertex_input_state : s_all_vertex_input_states) {
        std::stringstream owner_thread_ss;
        owner_thread_ss << vertex_input_state->m_owner_thread;
        log_threads->trace(
            "{}: on thread enter: vertex input state @ {} owned by thread {}",
            this_thread_id_string, //std::this_thread::get_id(),
            fmt::ptr(vertex_input_state),
            owner_thread_ss.str() //vertex_input_state->m_owner_thread
        );
        if (vertex_input_state->m_owner_thread == std::thread::id{}) {
            vertex_input_state->create();
        }
    }
}

void Vertex_input_state_impl::on_thread_exit()
{
    const std::lock_guard lock{s_mutex};

    gl::bind_vertex_array(0);
    auto this_thread_id = std::this_thread::get_id();
    std::stringstream this_thread_id_ss;
    this_thread_id_ss << this_thread_id;
    const std::string this_thread_id_string = this_thread_id_ss.str();
    for (auto* vertex_input_state : s_all_vertex_input_states) {
        std::stringstream owner_thread_ss;
        owner_thread_ss << vertex_input_state->m_owner_thread;
        log_threads->trace(
            "{}: on thread exit: vertex input state @ {} owned by thread {}",
            this_thread_id_string, //std::this_thread::get_id(),
            fmt::ptr(vertex_input_state),
            owner_thread_ss.str() // vertex_input_state->m_owner_thread
        );
        if (vertex_input_state->m_owner_thread == this_thread_id) {
            vertex_input_state->reset();
        }
    }
}

auto get_gl_attribute_type(const erhe::dataformat::Format format) -> gl::Attribute_type
{
    using namespace erhe::dataformat;
    switch (format) {
        case Format::format_8_scalar_unorm:           return gl::Attribute_type::float_;
        case Format::format_8_scalar_snorm:           return gl::Attribute_type::float_;
        case Format::format_8_scalar_uscaled:         return gl::Attribute_type::float_;
        case Format::format_8_scalar_sscaled:         return gl::Attribute_type::float_;
        case Format::format_8_scalar_uint:            return gl::Attribute_type::unsigned_int;
        case Format::format_8_scalar_sint:            return gl::Attribute_type::int_;
        case Format::format_8_vec2_unorm:             return gl::Attribute_type::float_vec2;
        case Format::format_8_vec2_snorm:             return gl::Attribute_type::float_vec2;
        case Format::format_8_vec2_uscaled:           return gl::Attribute_type::float_vec2;
        case Format::format_8_vec2_sscaled:           return gl::Attribute_type::float_vec2;
        case Format::format_8_vec2_uint:              return gl::Attribute_type::unsigned_int_vec2;
        case Format::format_8_vec2_sint:              return gl::Attribute_type::int_vec2;
        case Format::format_8_vec3_unorm:             return gl::Attribute_type::float_vec3;
        case Format::format_8_vec3_snorm:             return gl::Attribute_type::float_vec3;
        case Format::format_8_vec3_uscaled:           return gl::Attribute_type::float_vec3;
        case Format::format_8_vec3_sscaled:           return gl::Attribute_type::float_vec3;
        case Format::format_8_vec3_uint:              return gl::Attribute_type::unsigned_int_vec3;
        case Format::format_8_vec3_sint:              return gl::Attribute_type::int_vec3;
        case Format::format_8_vec4_unorm:             return gl::Attribute_type::float_vec4;
        case Format::format_8_vec4_snorm:             return gl::Attribute_type::float_vec4;
        case Format::format_8_vec4_uscaled:           return gl::Attribute_type::float_vec4;
        case Format::format_8_vec4_sscaled:           return gl::Attribute_type::float_vec4;
        case Format::format_8_vec4_uint:              return gl::Attribute_type::unsigned_int_vec4;
        case Format::format_8_vec4_sint:              return gl::Attribute_type::int_vec4;
        case Format::format_16_scalar_unorm:          return gl::Attribute_type::float_;
        case Format::format_16_scalar_snorm:          return gl::Attribute_type::float_;
        case Format::format_16_scalar_uscaled:        return gl::Attribute_type::float_;
        case Format::format_16_scalar_sscaled:        return gl::Attribute_type::float_;
        case Format::format_16_scalar_uint:           return gl::Attribute_type::unsigned_int;
        case Format::format_16_scalar_sint:           return gl::Attribute_type::int_;
        case Format::format_16_scalar_float:          return gl::Attribute_type::float_;
        case Format::format_16_vec2_unorm:            return gl::Attribute_type::float_vec2;
        case Format::format_16_vec2_snorm:            return gl::Attribute_type::float_vec2;
        case Format::format_16_vec2_uscaled:          return gl::Attribute_type::float_vec2;
        case Format::format_16_vec2_sscaled:          return gl::Attribute_type::float_vec2;
        case Format::format_16_vec2_uint:             return gl::Attribute_type::unsigned_int_vec2;
        case Format::format_16_vec2_sint:             return gl::Attribute_type::int_vec2;
        case Format::format_16_vec2_float:            return gl::Attribute_type::float_vec2;
        case Format::format_16_vec3_unorm:            return gl::Attribute_type::float_vec3;
        case Format::format_16_vec3_snorm:            return gl::Attribute_type::float_vec3;
        case Format::format_16_vec3_uscaled:          return gl::Attribute_type::float_vec3;
        case Format::format_16_vec3_sscaled:          return gl::Attribute_type::float_vec3;
        case Format::format_16_vec3_uint:             return gl::Attribute_type::unsigned_int_vec3;
        case Format::format_16_vec3_sint:             return gl::Attribute_type::int_vec3;
        case Format::format_16_vec3_float:            return gl::Attribute_type::float_vec3;
        case Format::format_16_vec4_unorm:            return gl::Attribute_type::float_vec4;
        case Format::format_16_vec4_snorm:            return gl::Attribute_type::float_vec4;
        case Format::format_16_vec4_uscaled:          return gl::Attribute_type::float_vec4;
        case Format::format_16_vec4_sscaled:          return gl::Attribute_type::float_vec4;
        case Format::format_16_vec4_uint:             return gl::Attribute_type::unsigned_int_vec4;
        case Format::format_16_vec4_sint:             return gl::Attribute_type::int_vec4;
        case Format::format_16_vec4_float:            return gl::Attribute_type::float_vec4;
        case Format::format_32_scalar_uscaled:        return gl::Attribute_type::float_;
        case Format::format_32_scalar_sscaled:        return gl::Attribute_type::float_;
        case Format::format_32_scalar_uint:           return gl::Attribute_type::unsigned_int;
        case Format::format_32_scalar_sint:           return gl::Attribute_type::int_;
        case Format::format_32_scalar_float:          return gl::Attribute_type::float_;
        case Format::format_32_vec2_uscaled:          return gl::Attribute_type::float_vec2;
        case Format::format_32_vec2_sscaled:          return gl::Attribute_type::float_vec2;
        case Format::format_32_vec2_uint:             return gl::Attribute_type::unsigned_int_vec2;
        case Format::format_32_vec2_sint:             return gl::Attribute_type::int_vec2;
        case Format::format_32_vec2_float:            return gl::Attribute_type::float_vec2;
        case Format::format_32_vec3_uscaled:          return gl::Attribute_type::float_vec3;
        case Format::format_32_vec3_sscaled:          return gl::Attribute_type::float_vec3;
        case Format::format_32_vec3_uint:             return gl::Attribute_type::unsigned_int_vec3;
        case Format::format_32_vec3_sint:             return gl::Attribute_type::int_vec3;
        case Format::format_32_vec3_float:            return gl::Attribute_type::float_vec3;
        case Format::format_32_vec4_uscaled:          return gl::Attribute_type::float_vec4;
        case Format::format_32_vec4_sscaled:          return gl::Attribute_type::float_vec4;
        case Format::format_32_vec4_uint:             return gl::Attribute_type::unsigned_int_vec4;
        case Format::format_32_vec4_sint:             return gl::Attribute_type::int_vec4;
        case Format::format_32_vec4_float:            return gl::Attribute_type::float_vec4;
        case Format::format_packed1010102_vec4_unorm: return gl::Attribute_type::float_vec4;
        case Format::format_packed1010102_vec4_snorm: return gl::Attribute_type::float_vec4;
        case Format::format_packed1010102_vec4_uint:  return gl::Attribute_type::unsigned_int_vec4;
        case Format::format_packed1010102_vec4_sint:  return gl::Attribute_type::int_vec4;
        case Format::format_packed111110_vec3_unorm:  return gl::Attribute_type::float_vec3;

        default: {
            ERHE_FATAL("Bad format");
            return static_cast<gl::Attribute_type>(0);
        }
    }
}

auto get_gl_vertex_attrib_type(const erhe::dataformat::Format format) -> gl::Vertex_attrib_type
{
    using namespace erhe::dataformat;
    switch (format) {
        case Format::format_8_scalar_unorm:           return gl::Vertex_attrib_type::unsigned_byte;
        case Format::format_8_scalar_snorm:           return gl::Vertex_attrib_type::byte;
        case Format::format_8_scalar_uscaled:         return gl::Vertex_attrib_type::unsigned_byte;
        case Format::format_8_scalar_sscaled:         return gl::Vertex_attrib_type::byte;
        case Format::format_8_scalar_uint:            return gl::Vertex_attrib_type::unsigned_byte;
        case Format::format_8_scalar_sint:            return gl::Vertex_attrib_type::byte;
        case Format::format_8_vec2_unorm:             return gl::Vertex_attrib_type::unsigned_byte;
        case Format::format_8_vec2_snorm:             return gl::Vertex_attrib_type::byte;
        case Format::format_8_vec2_uscaled:           return gl::Vertex_attrib_type::unsigned_byte;
        case Format::format_8_vec2_sscaled:           return gl::Vertex_attrib_type::byte;
        case Format::format_8_vec2_uint:              return gl::Vertex_attrib_type::unsigned_byte;
        case Format::format_8_vec2_sint:              return gl::Vertex_attrib_type::byte;
        case Format::format_8_vec3_unorm:             return gl::Vertex_attrib_type::unsigned_byte;
        case Format::format_8_vec3_snorm:             return gl::Vertex_attrib_type::byte;
        case Format::format_8_vec3_uscaled:           return gl::Vertex_attrib_type::unsigned_byte;
        case Format::format_8_vec3_sscaled:           return gl::Vertex_attrib_type::byte;
        case Format::format_8_vec3_uint:              return gl::Vertex_attrib_type::unsigned_byte;
        case Format::format_8_vec3_sint:              return gl::Vertex_attrib_type::byte;
        case Format::format_8_vec4_unorm:             return gl::Vertex_attrib_type::unsigned_byte;
        case Format::format_8_vec4_snorm:             return gl::Vertex_attrib_type::byte;
        case Format::format_8_vec4_uscaled:           return gl::Vertex_attrib_type::unsigned_byte;
        case Format::format_8_vec4_sscaled:           return gl::Vertex_attrib_type::byte;
        case Format::format_8_vec4_uint:              return gl::Vertex_attrib_type::unsigned_byte;
        case Format::format_8_vec4_sint:              return gl::Vertex_attrib_type::byte;
        case Format::format_16_scalar_unorm:          return gl::Vertex_attrib_type::unsigned_short;
        case Format::format_16_scalar_snorm:          return gl::Vertex_attrib_type::short_;
        case Format::format_16_scalar_uscaled:        return gl::Vertex_attrib_type::unsigned_short;
        case Format::format_16_scalar_sscaled:        return gl::Vertex_attrib_type::short_;
        case Format::format_16_scalar_uint:           return gl::Vertex_attrib_type::unsigned_short;
        case Format::format_16_scalar_sint:           return gl::Vertex_attrib_type::short_;
        case Format::format_16_scalar_float:          return gl::Vertex_attrib_type::half_float;
        case Format::format_16_vec2_unorm:            return gl::Vertex_attrib_type::unsigned_short;
        case Format::format_16_vec2_snorm:            return gl::Vertex_attrib_type::short_;
        case Format::format_16_vec2_uscaled:          return gl::Vertex_attrib_type::unsigned_short;
        case Format::format_16_vec2_sscaled:          return gl::Vertex_attrib_type::short_;
        case Format::format_16_vec2_uint:             return gl::Vertex_attrib_type::unsigned_short;
        case Format::format_16_vec2_sint:             return gl::Vertex_attrib_type::short_;
        case Format::format_16_vec2_float:            return gl::Vertex_attrib_type::half_float;
        case Format::format_16_vec3_unorm:            return gl::Vertex_attrib_type::unsigned_short;
        case Format::format_16_vec3_snorm:            return gl::Vertex_attrib_type::short_;
        case Format::format_16_vec3_uscaled:          return gl::Vertex_attrib_type::unsigned_short;
        case Format::format_16_vec3_sscaled:          return gl::Vertex_attrib_type::short_;
        case Format::format_16_vec3_uint:             return gl::Vertex_attrib_type::unsigned_short;
        case Format::format_16_vec3_sint:             return gl::Vertex_attrib_type::short_;
        case Format::format_16_vec3_float:            return gl::Vertex_attrib_type::half_float;
        case Format::format_16_vec4_unorm:            return gl::Vertex_attrib_type::unsigned_short;
        case Format::format_16_vec4_snorm:            return gl::Vertex_attrib_type::short_;
        case Format::format_16_vec4_uscaled:          return gl::Vertex_attrib_type::unsigned_short;
        case Format::format_16_vec4_sscaled:          return gl::Vertex_attrib_type::short_;
        case Format::format_16_vec4_uint:             return gl::Vertex_attrib_type::unsigned_short;
        case Format::format_16_vec4_sint:             return gl::Vertex_attrib_type::short_;
        case Format::format_16_vec4_float:            return gl::Vertex_attrib_type::half_float;
        case Format::format_32_scalar_uscaled:        return gl::Vertex_attrib_type::unsigned_int;
        case Format::format_32_scalar_sscaled:        return gl::Vertex_attrib_type::int_;
        case Format::format_32_scalar_uint:           return gl::Vertex_attrib_type::unsigned_int;
        case Format::format_32_scalar_sint:           return gl::Vertex_attrib_type::int_;
        case Format::format_32_scalar_float:          return gl::Vertex_attrib_type::float_;
        case Format::format_32_vec2_uscaled:          return gl::Vertex_attrib_type::float_;
        case Format::format_32_vec2_sscaled:          return gl::Vertex_attrib_type::float_;
        case Format::format_32_vec2_uint:             return gl::Vertex_attrib_type::unsigned_int;
        case Format::format_32_vec2_sint:             return gl::Vertex_attrib_type::int_;
        case Format::format_32_vec2_float:            return gl::Vertex_attrib_type::float_;
        case Format::format_32_vec3_uscaled:          return gl::Vertex_attrib_type::float_;
        case Format::format_32_vec3_sscaled:          return gl::Vertex_attrib_type::float_;
        case Format::format_32_vec3_uint:             return gl::Vertex_attrib_type::unsigned_int;
        case Format::format_32_vec3_sint:             return gl::Vertex_attrib_type::int_;
        case Format::format_32_vec3_float:            return gl::Vertex_attrib_type::float_;
        case Format::format_32_vec4_uscaled:          return gl::Vertex_attrib_type::float_;
        case Format::format_32_vec4_sscaled:          return gl::Vertex_attrib_type::float_;
        case Format::format_32_vec4_uint:             return gl::Vertex_attrib_type::unsigned_int;
        case Format::format_32_vec4_sint:             return gl::Vertex_attrib_type::int_;
        case Format::format_32_vec4_float:            return gl::Vertex_attrib_type::float_;
        case Format::format_packed1010102_vec4_unorm: return gl::Vertex_attrib_type::unsigned_int_2_10_10_10_rev;
        case Format::format_packed1010102_vec4_snorm: return gl::Vertex_attrib_type::unsigned_int_2_10_10_10_rev;
        case Format::format_packed1010102_vec4_uint:  return gl::Vertex_attrib_type::unsigned_int_2_10_10_10_rev;
        case Format::format_packed1010102_vec4_sint:  return gl::Vertex_attrib_type::int_2_10_10_10_rev;
        case Format::format_packed111110_vec3_unorm:  return gl::Vertex_attrib_type::unsigned_int_10f_11f_11f_rev;

        default: {
            ERHE_FATAL("Bad format");
            return static_cast<gl::Vertex_attrib_type>(0);
        }
    }
}

auto get_gl_normalized(erhe::dataformat::Format format) -> bool
{
    using namespace erhe::dataformat;
    switch (format) {
        case Format::format_8_scalar_unorm:           return true;
        case Format::format_8_scalar_snorm:           return true;
        case Format::format_8_scalar_uscaled:         return false;
        case Format::format_8_scalar_sscaled:         return false;
        case Format::format_8_scalar_uint:            return false;
        case Format::format_8_scalar_sint:            return false;
        case Format::format_8_vec2_unorm:             return true;
        case Format::format_8_vec2_snorm:             return true;
        case Format::format_8_vec2_uscaled:           return false;
        case Format::format_8_vec2_sscaled:           return false;
        case Format::format_8_vec2_uint:              return false;
        case Format::format_8_vec2_sint:              return false;
        case Format::format_8_vec3_unorm:             return true;
        case Format::format_8_vec3_snorm:             return true;
        case Format::format_8_vec3_uscaled:           return false;
        case Format::format_8_vec3_sscaled:           return false;
        case Format::format_8_vec3_uint:              return false;
        case Format::format_8_vec3_sint:              return false;
        case Format::format_8_vec4_unorm:             return true;
        case Format::format_8_vec4_snorm:             return true;
        case Format::format_8_vec4_uscaled:           return false;
        case Format::format_8_vec4_sscaled:           return false;
        case Format::format_8_vec4_uint:              return false;
        case Format::format_8_vec4_sint:              return false;
        case Format::format_16_scalar_unorm:          return true;
        case Format::format_16_scalar_snorm:          return true;
        case Format::format_16_scalar_uscaled:        return false;
        case Format::format_16_scalar_sscaled:        return false;
        case Format::format_16_scalar_uint:           return false;
        case Format::format_16_scalar_sint:           return false;
        case Format::format_16_scalar_float:          return false;
        case Format::format_16_vec2_unorm:            return true;
        case Format::format_16_vec2_snorm:            return true;
        case Format::format_16_vec2_uscaled:          return false;
        case Format::format_16_vec2_sscaled:          return false;
        case Format::format_16_vec2_uint:             return false;
        case Format::format_16_vec2_sint:             return false;
        case Format::format_16_vec2_float:            return false;
        case Format::format_16_vec3_unorm:            return true;
        case Format::format_16_vec3_snorm:            return true;
        case Format::format_16_vec3_uscaled:          return false;
        case Format::format_16_vec3_sscaled:          return false;
        case Format::format_16_vec3_uint:             return false;
        case Format::format_16_vec3_sint:             return false;
        case Format::format_16_vec3_float:            return false;
        case Format::format_16_vec4_unorm:            return true;
        case Format::format_16_vec4_snorm:            return true;
        case Format::format_16_vec4_uscaled:          return false;
        case Format::format_16_vec4_sscaled:          return false;
        case Format::format_16_vec4_uint:             return false;
        case Format::format_16_vec4_sint:             return false;
        case Format::format_16_vec4_float:            return false;
        case Format::format_32_scalar_uscaled:        return false;
        case Format::format_32_scalar_sscaled:        return false;
        case Format::format_32_scalar_uint:           return false;
        case Format::format_32_scalar_sint:           return false;
        case Format::format_32_scalar_float:          return false;
        case Format::format_32_vec2_uscaled:          return false;
        case Format::format_32_vec2_sscaled:          return false;
        case Format::format_32_vec2_uint:             return false;
        case Format::format_32_vec2_sint:             return false;
        case Format::format_32_vec2_float:            return false;
        case Format::format_32_vec3_uscaled:          return false;
        case Format::format_32_vec3_sscaled:          return false;
        case Format::format_32_vec3_uint:             return false;
        case Format::format_32_vec3_sint:             return false;
        case Format::format_32_vec3_float:            return false;
        case Format::format_32_vec4_uscaled:          return false;
        case Format::format_32_vec4_sscaled:          return false;
        case Format::format_32_vec4_uint:             return false;
        case Format::format_32_vec4_sint:             return false;
        case Format::format_32_vec4_float:            return false;
        case Format::format_packed1010102_vec4_unorm: return true;
        case Format::format_packed1010102_vec4_snorm: return true;
        case Format::format_packed1010102_vec4_uint:  return false;
        case Format::format_packed1010102_vec4_sint:  return false;
        case Format::format_packed111110_vec3_unorm:  return false;

        default: {
            ERHE_FATAL("Bad format");
            return false;
        }
    }
}

Vertex_input_state_impl::Vertex_input_state_impl(Device& device)
    : m_device{device}
{
    const std::lock_guard lock{s_mutex};

    s_all_vertex_input_states.push_back(this);

    create();
}

Vertex_input_state_impl::Vertex_input_state_impl(Device& device, Vertex_input_state_data&& create_info)
    : m_device{device}
    , m_data  {std::move(create_info)}
{
    const std::lock_guard lock{s_mutex};

    s_all_vertex_input_states.push_back(this);

    create();
}

Vertex_input_state_impl::~Vertex_input_state_impl() noexcept
{
    if (!m_gl_vertex_array.has_value()) {
        return;
    }

    const std::lock_guard lock{s_mutex};

    s_all_vertex_input_states.erase(
        std::remove(s_all_vertex_input_states.begin(), s_all_vertex_input_states.end(), this),
        s_all_vertex_input_states.end()
    );
}

void Vertex_input_state_impl::set(const Vertex_input_state_data& data)
{
    m_data = data;
    update();
}

void Vertex_input_state_impl::reset()
{
    // Delete VAO
    std::stringstream this_thread_id_ss;
    this_thread_id_ss << std::this_thread::get_id();
    //log_threads.trace("{}: reset @ {}\n", std::this_thread::get_id(), fmt::ptr(this));
    log_threads->trace("{}: reset @ {}", this_thread_id_ss.str(), fmt::ptr(this));
    m_owner_thread = {};
    m_gl_vertex_array.reset();

    ERHE_VERIFY(!m_gl_vertex_array.has_value());
}

void Vertex_input_state_impl::create()
{
    std::stringstream this_thread_id_ss;
    this_thread_id_ss << std::this_thread::get_id();
    //log_threads.trace("{}: create @ {}\n", std::this_thread::get_id(), fmt::ptr(this));
    log_threads->trace("{}: create @ {}", this_thread_id_ss.str(), fmt::ptr(this));
    if (m_gl_vertex_array.has_value()) {
        ERHE_VERIFY(m_owner_thread == std::this_thread::get_id());
        return;
    }

    m_owner_thread = std::this_thread::get_id();
    m_gl_vertex_array.emplace(Gl_vertex_array{});

    update();

    ERHE_VERIFY(m_gl_vertex_array.has_value());
}

void Vertex_input_state_impl::update()
{
    ERHE_VERIFY(m_owner_thread == std::this_thread::get_id());
    ERHE_VERIFY(m_gl_vertex_array.has_value());
    ERHE_VERIFY(gl_name() > 0);

    for (const auto& attribute : m_data.attributes) {
        switch (get_gl_attribute_type(attribute.format)) {
            //using enum gl::Attribute_type;
            case gl::Attribute_type::bool_:
            case gl::Attribute_type::bool_vec2:
            case gl::Attribute_type::bool_vec3:
            case gl::Attribute_type::bool_vec4:
            case gl::Attribute_type::int_:
            case gl::Attribute_type::int_vec2:
            case gl::Attribute_type::int_vec3:
            case gl::Attribute_type::int_vec4:
            case gl::Attribute_type::unsigned_int:
            case gl::Attribute_type::unsigned_int_vec2:
            case gl::Attribute_type::unsigned_int_vec3:
            case gl::Attribute_type::unsigned_int_vec4: {
                gl::vertex_array_attrib_i_format(
                    gl_name(),
                    attribute.layout_location,
                    static_cast<GLint>(erhe::dataformat::get_component_count(attribute.format)),
                    static_cast<gl::Vertex_attrib_i_type>(get_gl_vertex_attrib_type(attribute.format)),
                    static_cast<GLuint>(attribute.offset)
                );
                break;
            }

            case gl::Attribute_type::float_:
            case gl::Attribute_type::float_vec2:
            case gl::Attribute_type::float_vec3:
            case gl::Attribute_type::float_vec4:
            case gl::Attribute_type::float_mat2:
            case gl::Attribute_type::float_mat2x3:
            case gl::Attribute_type::float_mat2x4:
            case gl::Attribute_type::float_mat3:
            case gl::Attribute_type::float_mat3x2:
            case gl::Attribute_type::float_mat3x4:
            case gl::Attribute_type::float_mat4:
            case gl::Attribute_type::float_mat4x2:
            case gl::Attribute_type::float_mat4x3: {
                gl::vertex_array_attrib_format(
                    gl_name(),
                    attribute.layout_location,
                    static_cast<GLint>(erhe::dataformat::get_component_count(attribute.format)),
                    get_gl_vertex_attrib_type(attribute.format),
                    get_gl_normalized(attribute.format) ? GL_TRUE : GL_FALSE,
                    static_cast<GLuint>(attribute.offset)
                );
                break;
            }

            case gl::Attribute_type::unsigned_int64_arb:
            case gl::Attribute_type::double_:
            case gl::Attribute_type::double_vec2:
            case gl::Attribute_type::double_vec3:
            case gl::Attribute_type::double_vec4:
            case gl::Attribute_type::double_mat2:
            case gl::Attribute_type::double_mat2x3:
            case gl::Attribute_type::double_mat2x4:
            case gl::Attribute_type::double_mat3:
            case gl::Attribute_type::double_mat3x2:
            case gl::Attribute_type::double_mat3x4:
            case gl::Attribute_type::double_mat4:
            case gl::Attribute_type::double_mat4x2:
            case gl::Attribute_type::double_mat4x3: {
                gl::vertex_array_attrib_l_format(
                    gl_name(),
                    attribute.layout_location,
                    static_cast<GLint>(erhe::dataformat::get_component_count(attribute.format)),
                    static_cast<gl::Vertex_attrib_l_type>(get_gl_vertex_attrib_type(attribute.format)),
                    0
                );
                break;
            }

            default: {
                ERHE_FATAL("Bad vertex attrib pointer type");
            }
        }

        gl::vertex_array_attrib_binding(gl_name(), attribute.layout_location, static_cast<GLuint>(attribute.binding));
        gl::enable_vertex_array_attrib(gl_name(), attribute.layout_location);
    }

    for (const Vertex_input_binding& binding : m_data.bindings) {
        gl::vertex_array_binding_divisor(gl_name(), static_cast<GLuint>(binding.binding), binding.divisor);
    }
}

auto Vertex_input_state_impl::gl_name() const -> unsigned int
{
    ERHE_VERIFY(m_owner_thread == std::this_thread::get_id());

    return m_gl_vertex_array.has_value()
        ? m_gl_vertex_array.value().gl_name()
        : 0;
}

auto Vertex_input_state_impl::get_data() const -> const Vertex_input_state_data&
{
    return m_data;
}


} // namespace erhe::graphics
