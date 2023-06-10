#pragma once

#include "erhe/primitive/enums.hpp"
#include "erhe/gl/wrapper_enums.hpp"

#include <glm/glm.hpp>

namespace erhe::graphics
{
    class Vertex_attribute_mappings;
}

namespace erhe::primitive
{

class Requested_features
{
public:
    bool fill_triangles {false};
    bool edge_lines     {false};
    bool corner_points  {false};
    bool centroid_points{false};
    bool position       {false};
    bool normal         {false};
    bool normal_flat    {false};
    bool normal_smooth  {false};
    bool tangent        {false};
    bool bitangent      {false};
    bool color          {false};
    bool texcoord       {false};
    bool id             {false};
    bool joint_indices  {false};
    bool joint_weights  {false};
};

class Format_info
{
public:
    Requested_features features;
    //Requested_features embree_features;

    gl::Vertex_attrib_type position_type     {gl::Vertex_attrib_type::float_};
    gl::Vertex_attrib_type normal_type       {gl::Vertex_attrib_type::float_};
    gl::Vertex_attrib_type normal_flat_type  {gl::Vertex_attrib_type::float_};
    gl::Vertex_attrib_type normal_smooth_type{gl::Vertex_attrib_type::float_};
    gl::Vertex_attrib_type tangent_type      {gl::Vertex_attrib_type::float_};
    gl::Vertex_attrib_type bitangent_type    {gl::Vertex_attrib_type::float_};
    gl::Vertex_attrib_type color_type        {gl::Vertex_attrib_type::float_};
    gl::Vertex_attrib_type texcoord_type     {gl::Vertex_attrib_type::float_};
    gl::Vertex_attrib_type id_vec3_type      {gl::Vertex_attrib_type::float_};
    gl::Vertex_attrib_type id_uint_type      {gl::Vertex_attrib_type::float_};
    gl::Vertex_attrib_type joint_indices_type{gl::Vertex_attrib_type::unsigned_byte};
    gl::Vertex_attrib_type joint_weights_type{gl::Vertex_attrib_type::float_};

    glm::vec4                                  constant_color           {1.0f};
    bool                                       keep_geometry            {false};
    Normal_style                               normal_style             {Normal_style::corner_normals};
    erhe::graphics::Vertex_attribute_mappings* vertex_attribute_mappings{nullptr};
    bool                                       autocolor                {false};
};

}
