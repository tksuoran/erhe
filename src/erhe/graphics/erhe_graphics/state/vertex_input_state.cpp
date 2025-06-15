#include <fmt/core.h>
#include <fmt/ostream.h>

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_verify/verify.hpp"

#include <memory>
#include <sstream>

namespace erhe::graphics {

ERHE_PROFILE_MUTEX(std::mutex,   Vertex_input_state::s_mutex);
std::vector<Vertex_input_state*> Vertex_input_state::s_all_vertex_input_states;

void Vertex_input_state::on_thread_enter()
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

void Vertex_input_state::on_thread_exit()
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

auto get_vertex_divisor(erhe::dataformat::Vertex_step step) -> GLuint
{
    switch (step) {
        case erhe::dataformat::Vertex_step::Step_per_vertex:   return 0;
        case erhe::dataformat::Vertex_step::Step_per_instance: return 1;
        default:
            ERHE_FATAL("Invalid Vertex_step value");
            return 0;
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
        case Format::format_16_vec2_unorm:            return gl::Attribute_type::float_vec2;
        case Format::format_16_vec2_snorm:            return gl::Attribute_type::float_vec2;
        case Format::format_16_vec2_uscaled:          return gl::Attribute_type::float_vec2;
        case Format::format_16_vec2_sscaled:          return gl::Attribute_type::float_vec2;
        case Format::format_16_vec2_uint:             return gl::Attribute_type::unsigned_int_vec2;
        case Format::format_16_vec2_sint:             return gl::Attribute_type::int_vec2;
        case Format::format_16_vec3_unorm:            return gl::Attribute_type::float_vec3;
        case Format::format_16_vec3_snorm:            return gl::Attribute_type::float_vec3;
        case Format::format_16_vec3_uscaled:          return gl::Attribute_type::float_vec3;
        case Format::format_16_vec3_sscaled:          return gl::Attribute_type::float_vec3;
        case Format::format_16_vec3_uint:             return gl::Attribute_type::unsigned_int_vec3;
        case Format::format_16_vec3_sint:             return gl::Attribute_type::int_vec3;
        case Format::format_16_vec4_unorm:            return gl::Attribute_type::float_vec4;
        case Format::format_16_vec4_snorm:            return gl::Attribute_type::float_vec4;
        case Format::format_16_vec4_uscaled:          return gl::Attribute_type::float_vec4;
        case Format::format_16_vec4_sscaled:          return gl::Attribute_type::float_vec4;
        case Format::format_16_vec4_uint:             return gl::Attribute_type::unsigned_int_vec4;
        case Format::format_16_vec4_sint:             return gl::Attribute_type::int_vec4;
        case Format::format_32_scalar_unorm:          return gl::Attribute_type::float_;
        case Format::format_32_scalar_snorm:          return gl::Attribute_type::float_;
        case Format::format_32_scalar_uscaled:        return gl::Attribute_type::float_;
        case Format::format_32_scalar_sscaled:        return gl::Attribute_type::float_;
        case Format::format_32_scalar_uint:           return gl::Attribute_type::unsigned_int;
        case Format::format_32_scalar_sint:           return gl::Attribute_type::int_;
        case Format::format_32_scalar_float:          return gl::Attribute_type::float_;
        case Format::format_32_vec2_unorm:            return gl::Attribute_type::float_vec2;
        case Format::format_32_vec2_snorm:            return gl::Attribute_type::float_vec2;
        case Format::format_32_vec2_uscaled:          return gl::Attribute_type::float_vec2;
        case Format::format_32_vec2_sscaled:          return gl::Attribute_type::float_vec2;
        case Format::format_32_vec2_uint:             return gl::Attribute_type::unsigned_int_vec2;
        case Format::format_32_vec2_sint:             return gl::Attribute_type::int_vec2;
        case Format::format_32_vec2_float:            return gl::Attribute_type::float_vec2;
        case Format::format_32_vec3_unorm:            return gl::Attribute_type::float_vec3;
        case Format::format_32_vec3_snorm:            return gl::Attribute_type::float_vec3;
        case Format::format_32_vec3_uscaled:          return gl::Attribute_type::float_vec3;
        case Format::format_32_vec3_sscaled:          return gl::Attribute_type::float_vec3;
        case Format::format_32_vec3_uint:             return gl::Attribute_type::unsigned_int_vec3;
        case Format::format_32_vec3_sint:             return gl::Attribute_type::int_vec3;
        case Format::format_32_vec3_float:            return gl::Attribute_type::float_vec3;
        case Format::format_32_vec4_unorm:            return gl::Attribute_type::float_vec4;
        case Format::format_32_vec4_snorm:            return gl::Attribute_type::float_vec4;
        case Format::format_32_vec4_uscaled:          return gl::Attribute_type::float_vec4;
        case Format::format_32_vec4_sscaled:          return gl::Attribute_type::float_vec4;
        case Format::format_32_vec4_uint:             return gl::Attribute_type::unsigned_int_vec4;
        case Format::format_32_vec4_sint:             return gl::Attribute_type::int_vec4;
        case Format::format_32_vec4_float:            return gl::Attribute_type::float_vec4;
        case Format::format_packed1010102_vec4_unorm: return gl::Attribute_type::float_vec4;
        case Format::format_packed1010102_vec4_snorm: return gl::Attribute_type::float_vec4;
        case Format::format_packed1010102_vec4_uint:  return gl::Attribute_type::unsigned_int_vec4;
        case Format::format_packed1010102_vec4_sint:  return gl::Attribute_type::int_vec4;

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
        case Format::format_16_vec2_unorm:            return gl::Vertex_attrib_type::unsigned_short;
        case Format::format_16_vec2_snorm:            return gl::Vertex_attrib_type::short_;
        case Format::format_16_vec2_uscaled:          return gl::Vertex_attrib_type::unsigned_short;
        case Format::format_16_vec2_sscaled:          return gl::Vertex_attrib_type::short_;
        case Format::format_16_vec2_uint:             return gl::Vertex_attrib_type::unsigned_short;
        case Format::format_16_vec2_sint:             return gl::Vertex_attrib_type::short_;
        case Format::format_16_vec3_unorm:            return gl::Vertex_attrib_type::unsigned_short;
        case Format::format_16_vec3_snorm:            return gl::Vertex_attrib_type::short_;
        case Format::format_16_vec3_uscaled:          return gl::Vertex_attrib_type::unsigned_short;
        case Format::format_16_vec3_sscaled:          return gl::Vertex_attrib_type::short_;
        case Format::format_16_vec3_uint:             return gl::Vertex_attrib_type::unsigned_short;
        case Format::format_16_vec3_sint:             return gl::Vertex_attrib_type::short_;
        case Format::format_16_vec4_unorm:            return gl::Vertex_attrib_type::unsigned_short;
        case Format::format_16_vec4_snorm:            return gl::Vertex_attrib_type::short_;
        case Format::format_16_vec4_uscaled:          return gl::Vertex_attrib_type::unsigned_short;
        case Format::format_16_vec4_sscaled:          return gl::Vertex_attrib_type::short_;
        case Format::format_16_vec4_uint:             return gl::Vertex_attrib_type::unsigned_short;
        case Format::format_16_vec4_sint:             return gl::Vertex_attrib_type::short_;
        case Format::format_32_scalar_unorm:          return gl::Vertex_attrib_type::unsigned_int;
        case Format::format_32_scalar_snorm:          return gl::Vertex_attrib_type::int_;
        case Format::format_32_scalar_uscaled:        return gl::Vertex_attrib_type::unsigned_int;
        case Format::format_32_scalar_sscaled:        return gl::Vertex_attrib_type::int_;
        case Format::format_32_scalar_uint:           return gl::Vertex_attrib_type::unsigned_int;
        case Format::format_32_scalar_sint:           return gl::Vertex_attrib_type::int_;
        case Format::format_32_scalar_float:          return gl::Vertex_attrib_type::float_;
        case Format::format_32_vec2_unorm:            return gl::Vertex_attrib_type::float_;
        case Format::format_32_vec2_snorm:            return gl::Vertex_attrib_type::float_;
        case Format::format_32_vec2_uscaled:          return gl::Vertex_attrib_type::float_;
        case Format::format_32_vec2_sscaled:          return gl::Vertex_attrib_type::float_;
        case Format::format_32_vec2_uint:             return gl::Vertex_attrib_type::unsigned_int;
        case Format::format_32_vec2_sint:             return gl::Vertex_attrib_type::int_;
        case Format::format_32_vec2_float:            return gl::Vertex_attrib_type::float_;
        case Format::format_32_vec3_unorm:            return gl::Vertex_attrib_type::float_;
        case Format::format_32_vec3_snorm:            return gl::Vertex_attrib_type::float_;
        case Format::format_32_vec3_uscaled:          return gl::Vertex_attrib_type::float_;
        case Format::format_32_vec3_sscaled:          return gl::Vertex_attrib_type::float_;
        case Format::format_32_vec3_uint:             return gl::Vertex_attrib_type::unsigned_int;
        case Format::format_32_vec3_sint:             return gl::Vertex_attrib_type::int_;
        case Format::format_32_vec3_float:            return gl::Vertex_attrib_type::float_;
        case Format::format_32_vec4_unorm:            return gl::Vertex_attrib_type::float_;
        case Format::format_32_vec4_snorm:            return gl::Vertex_attrib_type::float_;
        case Format::format_32_vec4_uscaled:          return gl::Vertex_attrib_type::float_;
        case Format::format_32_vec4_sscaled:          return gl::Vertex_attrib_type::float_;
        case Format::format_32_vec4_uint:             return gl::Vertex_attrib_type::unsigned_int;
        case Format::format_32_vec4_sint:             return gl::Vertex_attrib_type::int_;
        case Format::format_32_vec4_float:            return gl::Vertex_attrib_type::float_;
        case Format::format_packed1010102_vec4_unorm: return gl::Vertex_attrib_type::unsigned_int_2_10_10_10_rev;
        case Format::format_packed1010102_vec4_snorm: return gl::Vertex_attrib_type::unsigned_int_2_10_10_10_rev;
        case Format::format_packed1010102_vec4_uint:  return gl::Vertex_attrib_type::unsigned_int_2_10_10_10_rev;
        case Format::format_packed1010102_vec4_sint:  return gl::Vertex_attrib_type::int_2_10_10_10_rev;

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
        case Format::format_16_vec2_unorm:            return true;
        case Format::format_16_vec2_snorm:            return true;
        case Format::format_16_vec2_uscaled:          return false;
        case Format::format_16_vec2_sscaled:          return false;
        case Format::format_16_vec2_uint:             return false;
        case Format::format_16_vec2_sint:             return false;
        case Format::format_16_vec3_unorm:            return true;
        case Format::format_16_vec3_snorm:            return true;
        case Format::format_16_vec3_uscaled:          return false;
        case Format::format_16_vec3_sscaled:          return false;
        case Format::format_16_vec3_uint:             return false;
        case Format::format_16_vec3_sint:             return false;
        case Format::format_16_vec4_unorm:            return true;
        case Format::format_16_vec4_snorm:            return true;
        case Format::format_16_vec4_uscaled:          return false;
        case Format::format_16_vec4_sscaled:          return false;
        case Format::format_16_vec4_uint:             return false;
        case Format::format_16_vec4_sint:             return false;
        case Format::format_32_scalar_unorm:          return true;
        case Format::format_32_scalar_snorm:          return true;
        case Format::format_32_scalar_uscaled:        return false;
        case Format::format_32_scalar_sscaled:        return false;
        case Format::format_32_scalar_uint:           return false;
        case Format::format_32_scalar_sint:           return false;
        case Format::format_32_scalar_float:          return false;
        case Format::format_32_vec2_unorm:            return true;
        case Format::format_32_vec2_snorm:            return true;
        case Format::format_32_vec2_uscaled:          return false;
        case Format::format_32_vec2_sscaled:          return false;
        case Format::format_32_vec2_uint:             return false;
        case Format::format_32_vec2_sint:             return false;
        case Format::format_32_vec2_float:            return false;
        case Format::format_32_vec3_unorm:            return true;
        case Format::format_32_vec3_snorm:            return true;
        case Format::format_32_vec3_uscaled:          return false;
        case Format::format_32_vec3_sscaled:          return false;
        case Format::format_32_vec3_uint:             return false;
        case Format::format_32_vec3_sint:             return false;
        case Format::format_32_vec3_float:            return false;
        case Format::format_32_vec4_unorm:            return true;
        case Format::format_32_vec4_snorm:            return true;
        case Format::format_32_vec4_uscaled:          return false;
        case Format::format_32_vec4_sscaled:          return false;
        case Format::format_32_vec4_uint:             return false;
        case Format::format_32_vec4_sint:             return false;
        case Format::format_32_vec4_float:            return false;
        case Format::format_packed1010102_vec4_unorm: return true;
        case Format::format_packed1010102_vec4_snorm: return true;
        case Format::format_packed1010102_vec4_uint:  return false;
        case Format::format_packed1010102_vec4_sint:  return false;

        default: {
            ERHE_FATAL("Bad format");
            return false;
        }
    }
}

auto get_attribute_name(const erhe::dataformat::Vertex_attribute& attribute) -> std::string
{
    using namespace erhe::dataformat;
    switch (attribute.usage_type) {
        case Vertex_attribute_usage::position: return "a_position";
        case Vertex_attribute_usage::normal: {
            return attribute.usage_index == 0 ? "a_normal" : fmt::format("a_normal_{}",  attribute.usage_index);
        }
        case Vertex_attribute_usage::tangent:       return "a_tangent";
        case Vertex_attribute_usage::bitangent:     return "a_bitangent";
        case Vertex_attribute_usage::color:         return fmt::format("a_color_{}",         attribute.usage_index);
        case Vertex_attribute_usage::tex_coord:     return fmt::format("a_texcoord_{}",      attribute.usage_index);
        case Vertex_attribute_usage::joint_indices: return fmt::format("a_joint_indices_{}", attribute.usage_index);
        case Vertex_attribute_usage::joint_weights: return fmt::format("a_joint_weights_{}", attribute.usage_index);
        case Vertex_attribute_usage::custom:        return fmt::format("a_custom_{}",        attribute.usage_index);
        default: {
            return {};
        }
    }
}

auto Vertex_input_state_data::make(const erhe::dataformat::Vertex_format& vertex_format) -> Vertex_input_state_data
{
    Vertex_input_state_data result;
    GLuint layout_location = 0;
    for (const erhe::dataformat::Vertex_stream& stream : vertex_format.streams) {
        result.bindings.push_back(
            Vertex_input_binding{
                .binding = static_cast<GLuint >(stream.binding),
                .stride  = static_cast<GLsizei>(stream.stride),
                .divisor = get_vertex_divisor(stream.step)
            }
        );
        for (const erhe::dataformat::Vertex_attribute& attribute : stream.attributes) {
            Gl_vertex_input_attribute input_attribute;
            input_attribute.layout_location      = layout_location++;
            input_attribute.binding              = static_cast<GLuint >(stream.binding);
            input_attribute.stride               = static_cast<GLsizei>(stream.stride);
            input_attribute.dimension            = static_cast<GLint  >(erhe::dataformat::get_component_count(attribute.format));
            input_attribute.gl_attribute_type    = get_gl_attribute_type(attribute.format);
            input_attribute.gl_vertex_atrib_type = get_gl_vertex_attrib_type(attribute.format);
            input_attribute.normalized           = get_gl_normalized(attribute.format);
            input_attribute.offset               = static_cast<GLuint>(attribute.offset);
            input_attribute.name                 = get_attribute_name(attribute);
            result.attributes.push_back(input_attribute);
        }
    }
    return result;
}

Vertex_input_state::Vertex_input_state(Instance& instance)
    : m_instance{instance}
{
    const std::lock_guard lock{s_mutex};

    s_all_vertex_input_states.push_back(this);

    create();
}

Vertex_input_state::Vertex_input_state(Instance& instance, Vertex_input_state_data&& create_info)
    : m_instance{instance}
    , m_data    {std::move(create_info)}
{
    const std::lock_guard lock{s_mutex};

    s_all_vertex_input_states.push_back(this);

    create();
}

Vertex_input_state::~Vertex_input_state() noexcept
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

void Vertex_input_state::set(const Vertex_input_state_data& data)
{
    m_data = data;
    update();
}

void Vertex_input_state::reset()
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

void Vertex_input_state::create()
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
    m_gl_vertex_array.emplace(Gl_vertex_array{m_instance});

    update();

    ERHE_VERIFY(m_gl_vertex_array.has_value());
}

void Vertex_input_state::update()
{
    ERHE_VERIFY(m_owner_thread == std::this_thread::get_id());
    ERHE_VERIFY(m_gl_vertex_array.has_value());
    ERHE_VERIFY(gl_name() > 0);

    //// const unsigned int max_attribute_count = std::min(
    ////     MAX_ATTRIBUTE_COUNT,
    ////     erhe::graphics::g_instance->limits.max_vertex_attribs
    //// );

    for (const auto& attribute : m_data.attributes) {
        switch (attribute.gl_attribute_type) {
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
                    attribute.dimension,
                    static_cast<gl::Vertex_attrib_i_type>(attribute.gl_vertex_atrib_type),
                    intptr_t{attribute.offset}
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
                    attribute.dimension,
                    attribute.gl_vertex_atrib_type,
                    attribute.normalized ? GL_TRUE : GL_FALSE,
                    intptr_t{attribute.offset}
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
                    attribute.dimension,
                    static_cast<gl::Vertex_attrib_l_type>(attribute.gl_vertex_atrib_type),
                    0
                );
                break;
            }

            default: {
                ERHE_FATAL("Bad vertex attrib pointer type");
            }
        }

        gl::vertex_array_attrib_binding(gl_name(), attribute.layout_location, attribute.binding);
        gl::enable_vertex_array_attrib(gl_name(), attribute.layout_location);
    }

    for (const Vertex_input_binding& binding : m_data.bindings) {
        gl::vertex_array_binding_divisor(gl_name(), binding.binding, binding.divisor);
    }
}

auto Vertex_input_state::gl_name() const -> unsigned int
{
    ERHE_VERIFY(m_owner_thread == std::this_thread::get_id());

    return m_gl_vertex_array.has_value()
        ? m_gl_vertex_array.value().gl_name()
        : 0;
}

auto Vertex_input_state::get_data() const -> const Vertex_input_state_data&
{
    return m_data;
}

void Vertex_input_state_tracker::reset()
{
    gl::bind_vertex_array(0);
    m_last = 0;
}

void Vertex_input_state_tracker::execute(const Vertex_input_state* const state)
{
    const unsigned int name = (state != nullptr) ? state->gl_name() : 0;
    if (m_last == name) {
        return;
    }
    gl::bind_vertex_array(name);
    m_last = name;

    // For set_vertex_buffer()
    if (state != nullptr) {
        m_bindings = state->get_data().bindings;
    } else {
        m_bindings.clear();
    }
}

void Vertex_input_state_tracker::set_index_buffer(erhe::graphics::Buffer* buffer) const
{
    ERHE_VERIFY(m_last != 0); // Must have VAO bound
    gl::vertex_array_element_buffer(m_last, (buffer != nullptr) ? buffer->gl_name() : 0);
}

void Vertex_input_state_tracker::set_vertex_buffer(const uint32_t binding_index, const erhe::graphics::Buffer* const buffer, const std::size_t offset)
{
    ERHE_VERIFY(m_last != 0); // Must have VAO bound
    ERHE_VERIFY(buffer != nullptr);
    for (const Vertex_input_binding& binding : m_bindings) {
        if (binding.binding == binding_index) {
            gl::vertex_array_vertex_buffer(
                m_last,
                binding_index,
                (buffer != nullptr) ? buffer->gl_name() : 0,
                offset,
                binding.stride
            );
            break;
        }
    }

}

} // namespace erhe::graphics
