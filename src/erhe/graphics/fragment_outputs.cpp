#include "erhe/graphics/fragment_outputs.hpp"
#include "erhe/log/log.hpp"
#include "erhe/toolkit/verify.hpp"

#include <sstream>

namespace erhe::graphics
{

static auto gl_fragment_output_type_name(
    const gl::Fragment_shader_output_type type
) -> char const *
{
    switch (type)
    {
        using enum gl::Fragment_shader_output_type;
        case int_:              return "int    ";
        case int_vec2:          return "ivec2  ";
        case int_vec3:          return "ivec3  ";
        case int_vec4:          return "ivec4  ";
        case unsigned_int:      return "uint   ";
        case unsigned_int_vec2: return "uvec2  ";
        case unsigned_int_vec3: return "uvec3  ";
        case unsigned_int_vec4: return "uvec4  ";
        case float_:            return "float  ";
        case float_vec2:        return "vec2   ";
        case float_vec3:        return "vec3   ";
        case float_vec4:        return "vec4   ";
        case double_:           return "double ";
        case double_vec2:       return "dvec2  ";
        case double_vec3:       return "dvec3  ";
        case double_vec4:       return "dvec4  ";
        default:
        {
            ERHE_FATAL("Bad fragment outout type\n");
        }
    }
}

void Fragment_outputs::clear()
{
    m_outputs.clear();
}

void Fragment_outputs::add(
    const std::string&              name,
    gl::Fragment_shader_output_type type,
    unsigned int                    location
)
{
    m_outputs.emplace_back(name, type, location);
}

auto Fragment_outputs::source() const -> std::string
{
    std::stringstream ss;

    for (const auto& output : m_outputs)
    {
        ss << "layout(location = " << output.location() << ") ";
        ss << "out ";
        ss << gl_fragment_output_type_name(output.type()) << " ";
        ss << output.name() << ";\n";
    }

    return ss.str();
}

} // namespace erhe::graphics
