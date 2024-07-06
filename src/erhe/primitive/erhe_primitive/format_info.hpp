#pragma once

#include "erhe_gl/wrapper_enums.hpp"
#include "erhe_graphics/vertex_format.hpp"

namespace erhe::graphics {
    class Vertex_attribute_mappings;
}

namespace erhe::primitive {

class Attributes
{
public:
    bool position     {false};
    bool normal       {false};
    bool normal_flat  {false};
    bool normal_smooth{false};
    bool tangent      {false};
    bool bitangent    {false};
    bool color        {false};
    bool texcoord     {false};
    bool id           {false};
    bool joint_indices{false};
    bool joint_weights{false};
};

class Attribute_types
{
public:
    erhe::dataformat::Format position     {erhe::dataformat::Format::format_32_vec3_float};
    erhe::dataformat::Format normal       {erhe::dataformat::Format::format_32_vec3_float};
    erhe::dataformat::Format normal_flat  {erhe::dataformat::Format::format_32_vec3_float};
    erhe::dataformat::Format normal_smooth{erhe::dataformat::Format::format_32_vec3_float};
    erhe::dataformat::Format tangent      {erhe::dataformat::Format::format_32_vec3_float};
    erhe::dataformat::Format bitangent    {erhe::dataformat::Format::format_32_vec3_float};
    erhe::dataformat::Format color        {erhe::dataformat::Format::format_32_vec4_float};
    erhe::dataformat::Format texcoord     {erhe::dataformat::Format::format_32_vec2_float};
    erhe::dataformat::Format id_vec3      {erhe::dataformat::Format::format_32_vec3_float};
    erhe::dataformat::Format id_uint      {erhe::dataformat::Format::format_32_scalar_uint};
    erhe::dataformat::Format joint_indices{erhe::dataformat::Format::format_8_vec4_uint};
    erhe::dataformat::Format joint_weights{erhe::dataformat::Format::format_32_vec4_float};
};

[[nodiscard]] auto prepare_vertex_format(const Attributes& attributes, const Attribute_types& attribute_types) -> erhe::graphics::Vertex_format;

} // namespace erhe::primitive
