#pragma once

#include "erhe_dataformat/vertex_format.hpp"

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

#if 0
class Attribute_types
{
public:
    erhe::dataformat::Format position     {erhe::dataformat::Format::format_32_vec3_float};
    erhe::dataformat::Format normal       {erhe::dataformat::Format::format_32_vec3_float};
    erhe::dataformat::Format tangent      {erhe::dataformat::Format::format_32_vec3_float};
    erhe::dataformat::Format bitangent    {erhe::dataformat::Format::format_32_vec3_float};
    erhe::dataformat::Format color        {erhe::dataformat::Format::format_32_vec4_float};
    erhe::dataformat::Format texcoord     {erhe::dataformat::Format::format_32_vec2_float};
    erhe::dataformat::Format id_vec3      {erhe::dataformat::Format::format_32_vec3_float};
    erhe::dataformat::Format joint_indices{erhe::dataformat::Format::format_8_vec4_uint};
    erhe::dataformat::Format joint_weights{erhe::dataformat::Format::format_32_vec4_float};
};
#endif

} // namespace erhe::primitive
