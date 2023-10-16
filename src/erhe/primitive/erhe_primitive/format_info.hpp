#pragma once

#include "erhe_gl/wrapper_enums.hpp"
#include "erhe_graphics/vertex_format.hpp"

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
    igl::VertexAttributeFormat position     {igl::VertexAttributeFormat::float_};
    igl::VertexAttributeFormat normal       {igl::VertexAttributeFormat::float_};
    igl::VertexAttributeFormat normal_flat  {igl::VertexAttributeFormat::float_};
    igl::VertexAttributeFormat normal_smooth{igl::VertexAttributeFormat::float_};
    igl::VertexAttributeFormat tangent      {igl::VertexAttributeFormat::float_};
    igl::VertexAttributeFormat bitangent    {igl::VertexAttributeFormat::float_};
    igl::VertexAttributeFormat color        {igl::VertexAttributeFormat::float_};
    igl::VertexAttributeFormat texcoord     {igl::VertexAttributeFormat::float_};
    igl::VertexAttributeFormat id_vec3      {igl::VertexAttributeFormat::float_};
    igl::VertexAttributeFormat id_uint      {igl::VertexAttributeFormat::float_};
    igl::VertexAttributeFormat joint_indices{igl::VertexAttributeFormat::unsigned_byte};
    igl::VertexAttributeFormat joint_weights{igl::VertexAttributeFormat::float_};
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
