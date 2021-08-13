#pragma once

#include "erhe/primitive/vertex_attribute_info.hpp"

#include <glm/glm.hpp>

#include <gsl/span>

namespace erhe::geometry
{
    class Geometry;
    class Mesh_info;
}

namespace erhe::primitive
{

class Primitive_geometry;
class Geometry_uploader;

/// Writes vertex attribute values to byte buffer/memory.
/// Memory is passed to Geometry_uploader when writer is destructed.
/// 
/// Vertex_buffer_writer is target API agnostic.
class Vertex_buffer_writer
{
public:
    explicit Vertex_buffer_writer(const Primitive_geometry& primitive_geometry,
                                  const Geometry_uploader&  geometry_uploader);
    virtual ~Vertex_buffer_writer();

    void write(const Vertex_attribute_info& attribute, const glm::vec2 value);
    void write(const Vertex_attribute_info& attribute, const glm::vec3 value);
    void write(const Vertex_attribute_info& attribute, const glm::vec4 value);
    void write(const Vertex_attribute_info& attribute, const uint32_t value);
    void move (const size_t relative_offset);

    const Primitive_geometry& primitive_geometry;
    const Geometry_uploader&  uploader;
    std::vector<std::uint8_t> vertex_data;
    gsl::span<std::uint8_t>   vertex_data_span;
    size_t                    vertex_write_offset{0};
};

/// Writes 8/16/32 -bit indices to byte buffer/memory
///
/// Index_buffer_writer is target API agnostic.
class Index_buffer_writer
{
public:
    Index_buffer_writer(const Primitive_geometry&  primitive_geometry,
                        const Geometry_uploader&   geometry_uploader,
                        erhe::geometry::Mesh_info& mesh_info);
    virtual ~Index_buffer_writer();

    void write_corner  (const uint32_t v0);
    void write_triangle(const uint32_t v0, const uint32_t v1, const uint32_t v2);
    void write_quad    (const uint32_t v0, const uint32_t v1, const uint32_t v2, const uint32_t v3);
    void write_edge    (const uint32_t v0, const uint32_t v1);
    void write_centroid(const uint32_t v0);

    const Primitive_geometry&    primitive_geometry;
    const Geometry_uploader&     uploader;
    const gl::Draw_elements_type index_type;
    const size_t                 index_type_size{0};
    std::vector<std::uint8_t>    index_data;
    gsl::span<std::uint8_t>      index_data_span;
    gsl::span<std::uint8_t>      corner_point_index_data_span;
    gsl::span<std::uint8_t>      triangle_fill_index_data_span;
    gsl::span<std::uint8_t>      edge_line_index_data_span;
    gsl::span<std::uint8_t>      polygon_centroid_index_data_span;

    size_t corner_point_indices_written    {0};
    size_t triangle_indices_written        {0};
    size_t edge_line_indices_written       {0};
    size_t polygon_centroid_indices_written{0};
};

} // namespace erhe::primitive
