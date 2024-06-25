#include "erhe_graphics/vertex_attribute_mappings.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/instance.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_graphics/vertex_format.hpp"
#include "erhe_gl/enum_string_functions.hpp"

namespace erhe::graphics
{

using std::string;
using std::string_view;

[[nodiscard]] auto get_gl_attribute_type(Glsl_type glsl_type) -> gl::Attribute_type
{
    switch (glsl_type) {
        case Glsl_type::float_:            return gl::Attribute_type::float_;
        case Glsl_type::float_vec2:        return gl::Attribute_type::float_vec2;
        case Glsl_type::float_vec3:        return gl::Attribute_type::float_vec3;
        case Glsl_type::float_vec4:        return gl::Attribute_type::float_vec4;
        case Glsl_type::bool_:             return gl::Attribute_type::bool_;
        case Glsl_type::int_:              return gl::Attribute_type::int_;
        case Glsl_type::int_vec2:          return gl::Attribute_type::int_vec2;
        case Glsl_type::int_vec3:          return gl::Attribute_type::int_vec3;
        case Glsl_type::int_vec4:          return gl::Attribute_type::int_vec4;
        case Glsl_type::unsigned_int:      return gl::Attribute_type::unsigned_int;
        case Glsl_type::unsigned_int_vec2: return gl::Attribute_type::unsigned_int_vec2;
        case Glsl_type::unsigned_int_vec3: return gl::Attribute_type::unsigned_int_vec3;
        case Glsl_type::unsigned_int_vec4: return gl::Attribute_type::unsigned_int_vec4;
        case Glsl_type::float_mat_2x2:     return gl::Attribute_type::float_mat2;
        case Glsl_type::float_mat_3x3:     return gl::Attribute_type::float_mat3;
        case Glsl_type::float_mat_4x4:     return gl::Attribute_type::float_mat4;
        default: {
            ERHE_FATAL("Unsupported attribute type");
        }
    }
}

[[nodiscard]] void format_to_gl_attribute(erhe::dataformat::Format format, gl::Vertex_attrib_type& type, GLint& dimension, bool& normalized)
{
    dimension = static_cast<GLint>(erhe::dataformat::get_component_count(format));
    switch (format) {
        case erhe::dataformat::Format::format_8_scalar_unorm:           type = gl::Vertex_attrib_type::unsigned_byte;  normalized = true;  break;
        case erhe::dataformat::Format::format_8_scalar_snorm:           type = gl::Vertex_attrib_type::byte;           normalized = true;  break;
        case erhe::dataformat::Format::format_8_scalar_uscaled:         type = gl::Vertex_attrib_type::unsigned_byte;  normalized = false; break;
        case erhe::dataformat::Format::format_8_scalar_sscaled:         type = gl::Vertex_attrib_type::byte;           normalized = false; break;
        case erhe::dataformat::Format::format_8_scalar_uint:            type = gl::Vertex_attrib_type::unsigned_byte;  normalized = false; break;
        case erhe::dataformat::Format::format_8_scalar_sint:            type = gl::Vertex_attrib_type::byte;           normalized = false; break;
        case erhe::dataformat::Format::format_8_vec2_unorm:             type = gl::Vertex_attrib_type::unsigned_byte;  normalized = true;  break;
        case erhe::dataformat::Format::format_8_vec2_snorm:             type = gl::Vertex_attrib_type::byte;           normalized = true;  break;
        case erhe::dataformat::Format::format_8_vec2_uscaled:           type = gl::Vertex_attrib_type::unsigned_byte;  normalized = false; break;
        case erhe::dataformat::Format::format_8_vec2_sscaled:           type = gl::Vertex_attrib_type::byte;           normalized = false; break;
        case erhe::dataformat::Format::format_8_vec2_uint:              type = gl::Vertex_attrib_type::unsigned_byte;  normalized = false; break;
        case erhe::dataformat::Format::format_8_vec2_sint:              type = gl::Vertex_attrib_type::byte;           normalized = false; break;
        case erhe::dataformat::Format::format_8_vec3_unorm:             type = gl::Vertex_attrib_type::unsigned_byte;  normalized = true;  break;
        case erhe::dataformat::Format::format_8_vec3_snorm:             type = gl::Vertex_attrib_type::byte;           normalized = true;  break;
        case erhe::dataformat::Format::format_8_vec3_uscaled:           type = gl::Vertex_attrib_type::unsigned_byte;  normalized = false; break;
        case erhe::dataformat::Format::format_8_vec3_sscaled:           type = gl::Vertex_attrib_type::byte;           normalized = false; break;
        case erhe::dataformat::Format::format_8_vec3_uint:              type = gl::Vertex_attrib_type::unsigned_byte;  normalized = false; break;
        case erhe::dataformat::Format::format_8_vec3_sint:              type = gl::Vertex_attrib_type::byte;           normalized = false; break;
        case erhe::dataformat::Format::format_8_vec4_unorm:             type = gl::Vertex_attrib_type::unsigned_byte;  normalized = true;  break;
        case erhe::dataformat::Format::format_8_vec4_snorm:             type = gl::Vertex_attrib_type::byte;           normalized = true;  break;
        case erhe::dataformat::Format::format_8_vec4_uscaled:           type = gl::Vertex_attrib_type::unsigned_byte;  normalized = false; break;
        case erhe::dataformat::Format::format_8_vec4_sscaled:           type = gl::Vertex_attrib_type::byte;           normalized = false; break;
        case erhe::dataformat::Format::format_8_vec4_uint:              type = gl::Vertex_attrib_type::unsigned_byte;  normalized = false; break;
        case erhe::dataformat::Format::format_8_vec4_sint:              type = gl::Vertex_attrib_type::byte;           normalized = false; break;
        case erhe::dataformat::Format::format_16_scalar_unorm:          type = gl::Vertex_attrib_type::unsigned_short; normalized = true;  break;
        case erhe::dataformat::Format::format_16_scalar_snorm:          type = gl::Vertex_attrib_type::short_;         normalized = true;  break;
        case erhe::dataformat::Format::format_16_scalar_uscaled:        type = gl::Vertex_attrib_type::unsigned_short; normalized = false; break;
        case erhe::dataformat::Format::format_16_scalar_sscaled:        type = gl::Vertex_attrib_type::short_;         normalized = false; break;
        case erhe::dataformat::Format::format_16_scalar_uint:           type = gl::Vertex_attrib_type::unsigned_short; normalized = false; break;
        case erhe::dataformat::Format::format_16_scalar_sint:           type = gl::Vertex_attrib_type::short_;         normalized = false; break;
        case erhe::dataformat::Format::format_16_vec2_unorm:            type = gl::Vertex_attrib_type::unsigned_short; normalized = true;  break;
        case erhe::dataformat::Format::format_16_vec2_snorm:            type = gl::Vertex_attrib_type::short_;         normalized = true;  break;
        case erhe::dataformat::Format::format_16_vec2_uscaled:          type = gl::Vertex_attrib_type::unsigned_short; normalized = false; break;
        case erhe::dataformat::Format::format_16_vec2_sscaled:          type = gl::Vertex_attrib_type::short_;         normalized = false; break;
        case erhe::dataformat::Format::format_16_vec2_uint:             type = gl::Vertex_attrib_type::unsigned_short; normalized = false; break;
        case erhe::dataformat::Format::format_16_vec2_sint:             type = gl::Vertex_attrib_type::short_;         normalized = false; break;
        case erhe::dataformat::Format::format_16_vec3_unorm:            type = gl::Vertex_attrib_type::unsigned_short; normalized = true;  break;
        case erhe::dataformat::Format::format_16_vec3_snorm:            type = gl::Vertex_attrib_type::short_;         normalized = true;  break;
        case erhe::dataformat::Format::format_16_vec3_uscaled:          type = gl::Vertex_attrib_type::unsigned_short; normalized = false; break;
        case erhe::dataformat::Format::format_16_vec3_sscaled:          type = gl::Vertex_attrib_type::short_;         normalized = false; break;
        case erhe::dataformat::Format::format_16_vec3_uint:             type = gl::Vertex_attrib_type::unsigned_short; normalized = false; break;
        case erhe::dataformat::Format::format_16_vec3_sint:             type = gl::Vertex_attrib_type::short_;         normalized = false; break;
        case erhe::dataformat::Format::format_16_vec4_unorm:            type = gl::Vertex_attrib_type::unsigned_short; normalized = true;  break;
        case erhe::dataformat::Format::format_16_vec4_snorm:            type = gl::Vertex_attrib_type::short_;         normalized = true;  break;
        case erhe::dataformat::Format::format_16_vec4_uscaled:          type = gl::Vertex_attrib_type::unsigned_short; normalized = false; break;
        case erhe::dataformat::Format::format_16_vec4_sscaled:          type = gl::Vertex_attrib_type::short_;         normalized = false; break;
        case erhe::dataformat::Format::format_16_vec4_uint:             type = gl::Vertex_attrib_type::unsigned_short; normalized = false; break;
        case erhe::dataformat::Format::format_16_vec4_sint:             type = gl::Vertex_attrib_type::short_;         normalized = false; break;
        case erhe::dataformat::Format::format_32_scalar_unorm:          type = gl::Vertex_attrib_type::unsigned_int;   normalized = true;  break;
        case erhe::dataformat::Format::format_32_scalar_snorm:          type = gl::Vertex_attrib_type::int_;           normalized = true;  break;
        case erhe::dataformat::Format::format_32_scalar_uscaled:        type = gl::Vertex_attrib_type::unsigned_int;   normalized = false; break;
        case erhe::dataformat::Format::format_32_scalar_sscaled:        type = gl::Vertex_attrib_type::int_;           normalized = false; break;
        case erhe::dataformat::Format::format_32_scalar_uint:           type = gl::Vertex_attrib_type::unsigned_int;   normalized = false; break;
        case erhe::dataformat::Format::format_32_scalar_sint:           type = gl::Vertex_attrib_type::int_;           normalized = false; break;
        case erhe::dataformat::Format::format_32_scalar_float:          type = gl::Vertex_attrib_type::float_;         normalized = false; break;
        case erhe::dataformat::Format::format_32_vec2_unorm:            type = gl::Vertex_attrib_type::unsigned_int;   normalized = true;  break;
        case erhe::dataformat::Format::format_32_vec2_snorm:            type = gl::Vertex_attrib_type::int_;           normalized = true;  break;
        case erhe::dataformat::Format::format_32_vec2_uscaled:          type = gl::Vertex_attrib_type::unsigned_int;   normalized = false; break;
        case erhe::dataformat::Format::format_32_vec2_sscaled:          type = gl::Vertex_attrib_type::int_;           normalized = false; break;
        case erhe::dataformat::Format::format_32_vec2_uint:             type = gl::Vertex_attrib_type::unsigned_int;   normalized = false; break;
        case erhe::dataformat::Format::format_32_vec2_sint:             type = gl::Vertex_attrib_type::int_;           normalized = false; break;
        case erhe::dataformat::Format::format_32_vec2_float:            type = gl::Vertex_attrib_type::float_;         normalized = false; break;
        case erhe::dataformat::Format::format_32_vec3_unorm:            type = gl::Vertex_attrib_type::unsigned_int;   normalized = true;  break;
        case erhe::dataformat::Format::format_32_vec3_snorm:            type = gl::Vertex_attrib_type::int_;           normalized = true;  break;
        case erhe::dataformat::Format::format_32_vec3_uscaled:          type = gl::Vertex_attrib_type::unsigned_int;   normalized = false; break;
        case erhe::dataformat::Format::format_32_vec3_sscaled:          type = gl::Vertex_attrib_type::int_;           normalized = false; break;
        case erhe::dataformat::Format::format_32_vec3_uint:             type = gl::Vertex_attrib_type::unsigned_int;   normalized = false; break;
        case erhe::dataformat::Format::format_32_vec3_sint:             type = gl::Vertex_attrib_type::int_;           normalized = false; break;
        case erhe::dataformat::Format::format_32_vec3_float:            type = gl::Vertex_attrib_type::float_;         normalized = false; break;
        case erhe::dataformat::Format::format_32_vec4_unorm:            type = gl::Vertex_attrib_type::unsigned_int;   normalized = true;  break;
        case erhe::dataformat::Format::format_32_vec4_snorm:            type = gl::Vertex_attrib_type::int_;           normalized = true;  break;
        case erhe::dataformat::Format::format_32_vec4_uscaled:          type = gl::Vertex_attrib_type::unsigned_int;   normalized = false; break;
        case erhe::dataformat::Format::format_32_vec4_sscaled:          type = gl::Vertex_attrib_type::int_;           normalized = false; break;
        case erhe::dataformat::Format::format_32_vec4_uint:             type = gl::Vertex_attrib_type::unsigned_int;   normalized = false; break;
        case erhe::dataformat::Format::format_32_vec4_sint:             type = gl::Vertex_attrib_type::int_;           normalized = false; break;
        case erhe::dataformat::Format::format_32_vec4_float:            type = gl::Vertex_attrib_type::float_;         normalized = false; break;
        //case erhe::dataformat::Format::format_packed1010102_vec4_unorm: type = gl::Vertex_attrib_type::float_;         normalized = false; break;
        //case erhe::dataformat::Format::format_packed1010102_vec4_snorm: type = gl::Vertex_attrib_type::float_;         normalized = false; break;
        case erhe::dataformat::Format::format_packed1010102_vec4_uint:  type = gl::Vertex_attrib_type::unsigned_int_2_10_10_10_rev; normalized = false; break;
        case erhe::dataformat::Format::format_packed1010102_vec4_sint:  type = gl::Vertex_attrib_type::int_2_10_10_10_rev;          normalized = false; break;
        default: {
            ERHE_FATAL("Unsupported attribute format");
        }
    }
}

Vertex_attribute_mappings::Vertex_attribute_mappings(
    erhe::graphics::Instance& instance
)
    : m_instance{instance}
{
}

Vertex_attribute_mappings::Vertex_attribute_mappings(
    erhe::graphics::Instance&                       instance,
    std::initializer_list<Vertex_attribute_mapping> mappings
)
    : mappings  {mappings}
    , m_instance{instance}
{
}

void Vertex_attribute_mappings::collect_attributes(
    std::vector<Vertex_input_attribute>& attributes,
    const Buffer*                        vertex_buffer,
    const Vertex_format&                 vertex_format
) const
{
    const unsigned int max_attribute_count = std::min(
        MAX_ATTRIBUTE_COUNT,
        m_instance.limits.max_vertex_attribs
    );

    if (vertex_buffer == nullptr) {
        log_vertex_attribute_mappings->error("error: vertex buffer == nullptr");
        return;
    }

    for (const auto& mapping : mappings) {
        if (
            vertex_format.has_attribute(
                mapping.src_usage.type,
                static_cast<unsigned int>(mapping.src_usage.index)
            )
        ) {
            const auto& attribute = vertex_format.find_attribute(
                mapping.src_usage.type,
                static_cast<unsigned int>(mapping.src_usage.index)
            );
            log_vertex_attribute_mappings->trace(
                "vertex attribute: shader type = {}, name = {}, usage type = {}, usage index = {}, data_type = {}",
                c_str(mapping.shader_type),
                mapping.name,
                Vertex_attribute::desc(attribute->usage.type),
                attribute->usage.index,
                erhe::dataformat::c_str(attribute->data_type)
            );

            if (attribute == nullptr) {
                log_vertex_attribute_mappings->error("bad vertex input state: attribute == nullptr");
                continue;
            }
            if (mapping.layout_location >= max_attribute_count) {
                log_vertex_attribute_mappings->error("bad vertex input state: layout location >= max attribute count");
                continue;
            }

            // GLuint                 layout_location{0};
            // const Buffer*          vertex_buffer;
            // GLsizei                stride;
            // GLint                  dimension;
            // gl::Attribute_type     shader_type;
            // gl::Vertex_attrib_type data_type;
            // bool                   normalized;
            // GLuint                 offset;
            // GLuint                 divisor;
            gl::Attribute_type shader_type = get_gl_attribute_type(attribute->shader_type);
            GLint dimension = 0;
            gl::Vertex_attrib_type type;
            bool normalized = false;
            format_to_gl_attribute(attribute->data_type, type, dimension, normalized);

            attributes.push_back(
                {
                    .layout_location = static_cast<GLuint>(mapping.layout_location),  // layout_location
                    .vertex_buffer   = vertex_buffer,                                 // vertex buffer
                    .stride          = static_cast<GLsizei>(vertex_format.stride()),  // stride
                    .dimension       = dimension,                                     // dimension
                    .shader_type     = shader_type,                                   // shader type
                    .data_type       = type,                                          // data type
                    .normalized      = normalized,                                    // normalized
                    .offset          = static_cast<GLuint>(attribute->offset),        // offset
                    .divisor         = attribute->divisor                             // divisor
                }
            );
        }
    }
}

} // namespace erhe::graphics
