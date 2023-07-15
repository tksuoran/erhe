#pragma once

#include "erhe/gl/wrapper_enums.hpp"
#include "erhe/graphics/vertex_format.hpp"

namespace erhe::graphics
{
    class Vertex_attribute_mappings;
}

namespace erhe::primitive
{


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
    gl::Vertex_attrib_type position     {gl::Vertex_attrib_type::float_};
    gl::Vertex_attrib_type normal       {gl::Vertex_attrib_type::float_};
    gl::Vertex_attrib_type normal_flat  {gl::Vertex_attrib_type::float_};
    gl::Vertex_attrib_type normal_smooth{gl::Vertex_attrib_type::float_};
    gl::Vertex_attrib_type tangent      {gl::Vertex_attrib_type::float_};
    gl::Vertex_attrib_type bitangent    {gl::Vertex_attrib_type::float_};
    gl::Vertex_attrib_type color        {gl::Vertex_attrib_type::float_};
    gl::Vertex_attrib_type texcoord     {gl::Vertex_attrib_type::float_};
    gl::Vertex_attrib_type id_vec3      {gl::Vertex_attrib_type::float_};
    gl::Vertex_attrib_type id_uint      {gl::Vertex_attrib_type::float_};
    gl::Vertex_attrib_type joint_indices{gl::Vertex_attrib_type::unsigned_byte};
    gl::Vertex_attrib_type joint_weights{gl::Vertex_attrib_type::float_};
};

[[nodiscard]] auto prepare_vertex_format(
    const Attributes&      attributes,
    const Attribute_types& attribute_types
) -> erhe::graphics::Vertex_format;

//class Format_info
//{
//public:
//    glm::vec4                                  constant_color           {1.0f};
//    bool                                       keep_geometry            {false};
//    Normal_style                               normal_style             {Normal_style::corner_normals};
//    erhe::graphics::Vertex_attribute_mappings* vertex_attribute_mappings{nullptr};
//    bool                                       autocolor                {false};
//};

}
