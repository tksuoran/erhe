#pragma once

#include "erhe/gl/strong_gl_enums.hpp"

#include <glm/glm.hpp>

namespace erhe::graphics
{
    class Vertex_attribute_mappings;
}

namespace erhe::primitive
{

struct Format_info
{
    bool want_fill_triangles {false};
    bool want_edge_lines     {false};
    bool want_corner_points  {false};
    bool want_centroid_points{false};
    bool want_position       {false};
    bool want_normal         {false};
    bool want_normal_flat    {false};
    bool want_normal_smooth  {false};
    bool want_tangent        {false};
    bool want_bitangent      {false};
    bool want_color          {false};
    bool want_texcoord       {false};
    bool want_id             {false};

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

    glm::vec4                                  constant_color{1.0f};
    bool                                       keep_geometry {false};
    Normal_style                               normal_style  {Normal_style::corner_normals};
    erhe::graphics::Vertex_attribute_mappings* vertex_attribute_mappings{nullptr};
};

}
