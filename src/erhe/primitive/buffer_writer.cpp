#include "erhe/primitive/buffer_writer.hpp"
#include "erhe/primitive/log.hpp"
#include "erhe/primitive/primitive_geometry.hpp"
#include "erhe/primitive/geometry_uploader.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/gl/strong_gl_enums.hpp"
#include "erhe/toolkit/verify.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>

#include <gsl/span>

namespace erhe::primitive
{

namespace
{

inline void write_low(gsl::span<std::uint8_t> destination,
                      gl::Draw_elements_type  type,
                      size_t                  value)
{
    switch (type)
    {
        case gl::Draw_elements_type::unsigned_byte:
        {
            auto* const ptr = reinterpret_cast<uint8_t*>(destination.data());
            Expects(value <= 0xffU);
            ptr[0] = value & 0xffU;
            break;
        }

        case gl::Draw_elements_type::unsigned_short:
        {
            auto* const ptr = reinterpret_cast<uint16_t*>(destination.data());
            Expects(value <= 0xffffU);
            ptr[0] = value & 0xffffU;
            break;
        }

        case gl::Draw_elements_type::unsigned_int:
        {
            auto* const ptr = reinterpret_cast<uint32_t*>(destination.data());
            ptr[0] = value & 0xffffffffu;
            break;
        }

        default:
        {
            FATAL("bad index type");
        }
    }
}

inline void write_low(gsl::span<std::uint8_t> destination,
                      gl::Vertex_attrib_type  type,
                      unsigned int            value)
{
    switch (type)
    {
        case gl::Vertex_attrib_type::unsigned_byte:
        {
            auto* const ptr = reinterpret_cast<uint8_t*>(destination.data());
            Expects(value <= 0xffU);
            ptr[0] = value & 0xffU;
            break;
        }

        case gl::Vertex_attrib_type::unsigned_short:
        {
            auto* const ptr = reinterpret_cast<uint16_t*>(destination.data());
            Expects(value <= 0xffffU);
            ptr[0] = value & 0xffffU;
            break;
        }

        case gl::Vertex_attrib_type::unsigned_int:
        {
            auto* ptr = reinterpret_cast<uint32_t*>(destination.data());
            ptr[0] = value;
            break;
        }

        default:
        {
            FATAL("bad index type");
        }
    }
}

inline void write_low(gsl::span<std::uint8_t> destination,
                      gl::Vertex_attrib_type  type,
                      glm::vec2               value)
{
    if (type == gl::Vertex_attrib_type::float_)
    {
        auto* const ptr = reinterpret_cast<float*>(destination.data());
        ptr[0] = value.x;
        ptr[1] = value.y;
    }
    else if (type == gl::Vertex_attrib_type::half_float)
    {
        // TODO(tksuoran@gmail.com): Would this be safe even if we are not aligned?
        // uint* ptr = reinterpret_cast<uint*>(data_ptr);
        // *ptr = glm::packHalf2x16(value);
        auto* const ptr = reinterpret_cast<glm::uint16*>(destination.data());
        ptr[0] = glm::packHalf1x16(value.x);
        ptr[1] = glm::packHalf1x16(value.y);
    }
    else
    {
        FATAL("unsupported attribute type");
    }
}

inline void write_low(gsl::span<std::uint8_t> destination,
                      gl::Vertex_attrib_type  type,
                      glm::vec3               value)
{
    if (type == gl::Vertex_attrib_type::float_)
    {
        auto* const ptr = reinterpret_cast<float*>(destination.data());
        ptr[0] = value.x;
        ptr[1] = value.y;
        ptr[2] = value.z;
    }
    else if (type == gl::Vertex_attrib_type::half_float)
    {
        auto* const ptr = reinterpret_cast<glm::uint16 *>(destination.data());
        ptr[0] = glm::packHalf1x16(value.x);
        ptr[1] = glm::packHalf1x16(value.y);
        ptr[2] = glm::packHalf1x16(value.z);
    }
    else
    {
        FATAL("unsupported attribute type");
    }
}

inline void write_low(gsl::span<std::uint8_t> destination,
                      gl::Vertex_attrib_type  type,
                      glm::vec4               value)
{
    if (type == gl::Vertex_attrib_type::float_)
    {
        auto* const ptr = reinterpret_cast<float*>(destination.data());
        ptr[0] = value.x;
        ptr[1] = value.y;
        ptr[2] = value.z;
        ptr[3] = value.w;
    }
    else if (type == gl::Vertex_attrib_type::half_float)
    {
        auto* const ptr = reinterpret_cast<glm::uint16*>(destination.data());
        // TODO(tksuoran@gmail.com): glm::packHalf4x16() - but what if we are not aligned?
        ptr[0] = glm::packHalf1x16(value.x);
        ptr[1] = glm::packHalf1x16(value.y);
        ptr[2] = glm::packHalf1x16(value.z);
        ptr[3] = glm::packHalf1x16(value.w);
    }
    else
    {
        FATAL("unsupported attribute type");
    }
}

} // namespace

Vertex_buffer_writer::Vertex_buffer_writer(const Primitive_geometry& primitive_geometry,
                                           const Geometry_uploader&  uploader)
    : primitive_geometry{primitive_geometry}
    , uploader          {uploader}
{
    vertex_data.resize(primitive_geometry.vertex_count * primitive_geometry.vertex_element_size);
    vertex_data_span = gsl::make_span(vertex_data);
}

Vertex_buffer_writer::~Vertex_buffer_writer()
{
    uploader.end(*this);
}

Index_buffer_writer::Index_buffer_writer(const Primitive_geometry&  primitive_geometry,
                                         const Geometry_uploader&   uploader,
                                         erhe::geometry::Mesh_info& mesh_info)
    : primitive_geometry{primitive_geometry}
    , uploader          {uploader}
    , index_type        {uploader.buffer_info.index_type}
    , index_type_size   {size_of_type(index_type)}
{
    index_data.resize(primitive_geometry.index_count * index_type_size);
    index_data_span = gsl::make_span(index_data);

    if (uploader.format_info.want_corner_points)
    {
        corner_point_index_data_span = index_data_span.subspan(primitive_geometry.corner_point_indices.first_index * index_type_size,
                                                               mesh_info.index_count_corner_points * index_type_size);
    }
    if (uploader.format_info.want_fill_triangles)
    {
        triangle_fill_index_data_span = index_data_span.subspan(primitive_geometry.triangle_fill_indices.first_index * index_type_size,
                                                                mesh_info.index_count_fill_triangles * index_type_size);
    }
    if (uploader.format_info.want_edge_lines)
    {
        edge_line_index_data_span = index_data_span.subspan(primitive_geometry.edge_line_indices.first_index * index_type_size,
                                                            mesh_info.index_count_edge_lines * index_type_size);
    }
    if (uploader.format_info.want_centroid_points)
    {
        polygon_centroid_index_data_span = index_data_span.subspan(primitive_geometry.polygon_centroid_indices.first_index * index_type_size,
                                                                   mesh_info.polygon_count * index_type_size);
    }
}

Index_buffer_writer::~Index_buffer_writer()
{
    uploader.end(*this);
}

void Vertex_buffer_writer::write(const Vertex_attribute_info& attribute, const glm::vec2 value)
{
    write_low(vertex_data_span.subspan(vertex_write_offset + attribute.offset,
                                       attribute.size),
              attribute.data_type,
              value);
}

void Vertex_buffer_writer::write(const Vertex_attribute_info& attribute, const glm::vec3 value)
{
    write_low(vertex_data_span.subspan(vertex_write_offset + attribute.offset,
                                       attribute.size),
              attribute.data_type,
              value);
}

void Vertex_buffer_writer::write(const Vertex_attribute_info& attribute, const glm::vec4 value)
{
    write_low(vertex_data_span.subspan(vertex_write_offset + attribute.offset,
                                       attribute.size),
              attribute.data_type,
              value);
}

void Vertex_buffer_writer::write(const Vertex_attribute_info& attribute, const uint32_t value)
{
    write_low(vertex_data_span.subspan(vertex_write_offset + attribute.offset,
                                       attribute.size),
              attribute.data_type,
              value);
}

void Vertex_buffer_writer::move(const size_t relative_offset)
{
    vertex_write_offset += relative_offset;
}

void Index_buffer_writer::write_corner(const uint32_t v0)
{
    log_primitive_builder.trace("point {}\n", v0);
    write_low(corner_point_index_data_span.subspan(corner_point_indices_written * index_type_size, index_type_size), index_type, v0);
    ++corner_point_indices_written;
}

void Index_buffer_writer::write_triangle(const uint32_t v0, const uint32_t v1, const uint32_t v2)
{
    log_primitive_builder.trace("triangle {}, {}, {}\n", v0, v1, v2);
    write_low(triangle_fill_index_data_span.subspan((triangle_indices_written + 0) * index_type_size, index_type_size), index_type, v0);
    write_low(triangle_fill_index_data_span.subspan((triangle_indices_written + 1) * index_type_size, index_type_size), index_type, v1);
    write_low(triangle_fill_index_data_span.subspan((triangle_indices_written + 2) * index_type_size, index_type_size), index_type, v2);
    triangle_indices_written += 3;
}

void Index_buffer_writer::write_edge(const uint32_t v0, const uint32_t v1)
{
    log_primitive_builder.trace("edge {}, {}\n", v0, v1);
    write_low(edge_line_index_data_span.subspan((edge_line_indices_written + 0) * index_type_size, index_type_size), index_type, v0);
    write_low(edge_line_index_data_span.subspan((edge_line_indices_written + 1) * index_type_size, index_type_size), index_type, v1);
    edge_line_indices_written += 2;
}

void Index_buffer_writer::write_centroid(const uint32_t v0)
{
    //log_primitive_builder.trace("centroid {}\n", v0);
    write_low(polygon_centroid_index_data_span.subspan(polygon_centroid_indices_written * index_type_size, index_type_size), index_type, v0);
    ++polygon_centroid_indices_written;
}

}
