#pragma once

#include "erhe_primitive/buffer_info.hpp"

#include <geogram/mesh/mesh.h>

#include <cstdint>
#include <vector>

namespace erhe::primitive {

class Primitive_types
{
public:
    bool fill_triangles         {false};
    // Expanded (un-shared, 3 sequential vertices per triangle) copy of the fill
    // triangles, carrying a per-vertex packed wireframe attribute (corner index +
    // real-edge mask). Drawn by the solid-wireframe shader variant so the
    // wireframe shares the fill's exact depth (no z-fight). Independent of
    // fill_triangles: a build may produce either, or both.
    bool fill_triangles_expanded{false};
    bool edge_lines             {false};
    bool corner_points          {false};
    bool centroid_points        {false};
};

class Build_info
{
public:
    Primitive_types primitive_types;
    Buffer_info     buffer_info;
    GEO::vec4f      constant_color{1.0f, 1.0f, 1.0f, 1.0f};
    bool            keep_geometry {false};
    Normal_style    normal_style  {Normal_style::corner_normals};
    bool            vertex_id_vec3{false};
    bool            autocolor     {false};
};

class Element_mappings
{
public:
    std::vector<uint32_t> triangle_to_mesh_facet;
    std::vector<uint32_t> mesh_corner_to_vertex_buffer_index;
    std::vector<uint32_t> mesh_vertex_to_vertex_buffer_index;
};

} // namespace erhe::primitive
