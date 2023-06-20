#pragma once

#include "erhe/primitive/buffer_info.hpp"
#include "erhe/primitive/format_info.hpp"

namespace erhe::primitive
{

class Primitive_types
{
public:
    bool fill_triangles {false};
    bool edge_lines     {false};
    bool corner_points  {false};
    bool centroid_points{false};
};

class Build_info
{
public:
    Primitive_types                            primitive_types;
    Buffer_info                                buffer_info;
    glm::vec4                                  constant_color           {1.0f};
    bool                                       keep_geometry            {false};
    Normal_style                               normal_style             {Normal_style::corner_normals};
    erhe::graphics::Vertex_attribute_mappings* vertex_attribute_mappings{nullptr};
    bool                                       autocolor                {false};

};

} // namespace erhe::primitive
