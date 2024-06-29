#pragma once

#include "erhe_primitive/buffer_range.hpp"
#include "erhe_primitive/vertex_attribute_info.hpp"
#include "erhe_dataformat/dataformat.hpp"

#include <glm/glm.hpp>

#include <span>
#include <vector>

namespace erhe::geometry
{
    class Geometry;
    class Mesh_info;
}

namespace erhe::primitive
{

class Build_context;
class Buffer_sink;
class Renderable_mesh;

/// Writes vertex attribute values to byte buffer/memory.
///
/// Vertex_buffer_writer is target API agnostic.
class Vertex_buffer_writer
{
public:
    Vertex_buffer_writer(
        Build_context& build_context,
        Buffer_sink&   buffer_sink
    );
    virtual ~Vertex_buffer_writer() noexcept;

    void write(const Vertex_attribute_info& attribute, const glm::vec2 value);
    void write(const Vertex_attribute_info& attribute, const glm::vec3 value);
    void write(const Vertex_attribute_info& attribute, const glm::vec4 value);
    void write(const Vertex_attribute_info& attribute, const uint32_t value);
    void write(const Vertex_attribute_info& attribute, const glm::uvec4 value);
    void move (const std::size_t relative_offset);

    [[nodiscard]] auto start_offset() -> std::size_t;

    Build_context&            build_context;
    Buffer_sink&              buffer_sink;
    Buffer_range              buffer_range;
    std::vector<std::uint8_t> vertex_data;
    std::span<std::uint8_t>   vertex_data_span;
    std::size_t               vertex_write_offset{0};
};

/// Writes 8/16/32 -bit indices to byte buffer/memory
///
/// Index_buffer_writer is target API agnostic.
class Index_buffer_writer
{
public:
    Index_buffer_writer(
        Build_context& build_context,
        Buffer_sink&   buffer_sink
    );
    virtual ~Index_buffer_writer() noexcept;

    void write_corner  (const uint32_t v0);
    void write_triangle(const uint32_t v0, const uint32_t v1, const uint32_t v2);
    void write_quad    (const uint32_t v0, const uint32_t v1, const uint32_t v2, const uint32_t v3);
    void write_edge    (const uint32_t v0, const uint32_t v1);
    void write_centroid(const uint32_t v0);

    [[nodiscard]] auto start_offset() -> std::size_t;

    Build_context&                 build_context;
    Buffer_sink&                   buffer_sink;
    Buffer_range                   buffer_range;
    const erhe::dataformat::Format index_type;
    const std::size_t              index_type_size{0};
    std::vector<std::uint8_t>      index_data;
    std::span<std::uint8_t>        index_data_span;
    std::span<std::uint8_t>        corner_point_index_data_span;
    std::span<std::uint8_t>        triangle_fill_index_data_span;
    std::span<std::uint8_t>        edge_line_index_data_span;
    std::span<std::uint8_t>        polygon_centroid_index_data_span;

    std::size_t corner_point_indices_written    {0};
    std::size_t triangle_indices_written        {0};
    std::size_t edge_line_indices_written       {0};
    std::size_t polygon_centroid_indices_written{0};
};

} // namespace erhe::primitive
