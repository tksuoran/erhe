#pragma once

#include "erhe_primitive/buffer_info.hpp"

#include <geogram/mesh/mesh.h>

#include <cstdint>
#include <vector>

namespace erhe::graphics {
    class Vertex_attribute_mappings;
}

namespace erhe::primitive {

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
    GEO::vec4f                                 constant_color           {1.0f, 1.0f, 1.0f, 1.0f};
    bool                                       keep_geometry            {false};
    Normal_style                               normal_style             {Normal_style::corner_normals};
    erhe::graphics::Vertex_attribute_mappings* vertex_attribute_mappings{nullptr};
    bool                                       vertex_id_vec3           {false};
    bool                                       autocolor                {false};
};

class Element_mappings
{
public:
    std::vector<uint32_t> triangle_to_mesh_facet;
    std::vector<uint32_t> mesh_corner_to_vertex_buffer_index;
    std::vector<uint32_t> mesh_vertex_to_vertex_buffer_index;
};

} // namespace erhe::primitive
