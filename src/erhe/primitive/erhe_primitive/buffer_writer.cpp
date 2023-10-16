#include "erhe_primitive/buffer_writer.hpp"
#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_primitive/primitive_builder.hpp"
#include "erhe_primitive/geometry_mesh.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>
#include <gsl/span>

namespace erhe::primitive
{

namespace
{

inline void write_low(
    const gsl::span<std::uint8_t> destination,
    const gl::Draw_elements_type  type,
    const std::size_t             value
)
{
    switch (type) {
        //using enum gl::Draw_elements_type;
        case gl::Draw_elements_type::unsigned_byte: {
            auto* const ptr = reinterpret_cast<uint8_t*>(destination.data());
            Expects(value <= 0xffU);
            ptr[0] = value & 0xffU;
            break;
        }

        case gl::Draw_elements_type::unsigned_short: {
            auto* const ptr = reinterpret_cast<uint16_t*>(destination.data());
            Expects(value <= 0xffffU);
            ptr[0] = value & 0xffffU;
            break;
        }

        case gl::Draw_elements_type::unsigned_int: {
            auto* const ptr = reinterpret_cast<uint32_t*>(destination.data());
            ptr[0] = value & 0xffffffffu;
            break;
        }

        default: {
            ERHE_FATAL("bad index type");
        }
    }
}

inline void write_low(
    const gsl::span<std::uint8_t> destination,
    const igl::VertexAttributeFormat  type,
    const unsigned int            value
)
{
    switch (type) {
        //using enum igl::VertexAttributeFormat;
        case igl::VertexAttributeFormat::unsigned_byte: {
            auto* const ptr = reinterpret_cast<uint8_t*>(destination.data());
            Expects(value <= 0xffU);
            ptr[0] = value & 0xffU;
            break;
        }

        case igl::VertexAttributeFormat::unsigned_short: {
            auto* const ptr = reinterpret_cast<uint16_t*>(destination.data());
            Expects(value <= 0xffffU);
            ptr[0] = value & 0xffffU;
            break;
        }

        case igl::VertexAttributeFormat::unsigned_int: {
            auto* ptr = reinterpret_cast<uint32_t*>(destination.data());
            ptr[0] = value;
            break;
        }

        default: {
            ERHE_FATAL("bad index type");
        }
    }
}

inline void write_low(
    const gsl::span<std::uint8_t> destination,
    const igl::VertexAttributeFormat  type,
    const glm::vec2               value
)
{
    switch (type) {
        case igl::VertexAttributeFormat::float_: {
            auto* const ptr = reinterpret_cast<float*>(destination.data());
            ptr[0] = value.x;
            ptr[1] = value.y;
            break;
        }
        case igl::VertexAttributeFormat::half_float: {
            // TODO(tksuoran@gmail.com): Would this be safe even if we are not aligned?
            // uint* ptr = reinterpret_cast<uint*>(data_ptr);
            // *ptr = glm::packHalf2x16(value);
            auto* const ptr = reinterpret_cast<glm::uint16*>(destination.data());
            ptr[0] = glm::packHalf1x16(value.x);
            ptr[1] = glm::packHalf1x16(value.y);
            break;
        }
        case igl::VertexAttributeFormat::unsigned_byte: {
            auto* const ptr = reinterpret_cast<uint8_t*>(destination.data());
            float scaled_x = std::max(0.0f, std::min(value.x * 255.0f, 255.0f));
            float scaled_y = std::max(0.0f, std::min(value.y * 255.0f, 255.0f));
            Expects(scaled_x <= 0xffU);
            Expects(scaled_y <= 0xffU);
            ptr[0] = static_cast<uint8_t>(scaled_x) & 0xffU;
            ptr[1] = static_cast<uint8_t>(scaled_y) & 0xffU;
            break;
        }
        default: {
            ERHE_FATAL("unsupported attribute type");
            break;
        }
    }
}

inline void write_low(
    const gsl::span<std::uint8_t> destination,
    const igl::VertexAttributeFormat  type,
    const glm::vec3               value
)
{
    switch (type) {
        case igl::VertexAttributeFormat::float_: {
            auto* const ptr = reinterpret_cast<float*>(destination.data());
            ptr[0] = value.x;
            ptr[1] = value.y;
            ptr[2] = value.z;
            break;
        }
        case igl::VertexAttributeFormat::half_float: {
            auto* const ptr = reinterpret_cast<glm::uint16 *>(destination.data());
            ptr[0] = glm::packHalf1x16(value.x);
            ptr[1] = glm::packHalf1x16(value.y);
            ptr[2] = glm::packHalf1x16(value.z);
            break;
        }
        case igl::VertexAttributeFormat::unsigned_byte: {
            auto* const ptr = reinterpret_cast<uint8_t*>(destination.data());
            float scaled_x = std::max(0.0f, std::min(value.x * 255.0f, 255.0f));
            float scaled_y = std::max(0.0f, std::min(value.y * 255.0f, 255.0f));
            float scaled_z = std::max(0.0f, std::min(value.z * 255.0f, 255.0f));
            Expects(scaled_x <= 0xffU);
            Expects(scaled_y <= 0xffU);
            Expects(scaled_z <= 0xffU);
            ptr[0] = static_cast<uint8_t>(scaled_x) & 0xffU;
            ptr[1] = static_cast<uint8_t>(scaled_y) & 0xffU;
            ptr[2] = static_cast<uint8_t>(scaled_z) & 0xffU;
            break;
        }
        default: {
            ERHE_FATAL("unsupported attribute type");
            break;
        }
    }
}

inline void write_low(
    const gsl::span<std::uint8_t> destination,
    const igl::VertexAttributeFormat  type,
    const glm::vec4               value
)
{
    switch (type) {
        case igl::VertexAttributeFormat::float_: {
            auto* const ptr = reinterpret_cast<float*>(destination.data());
            ptr[0] = value.x;
            ptr[1] = value.y;
            ptr[2] = value.z;
            ptr[3] = value.w;
            break;
        }
        case igl::VertexAttributeFormat::half_float: { 
            auto* const ptr = reinterpret_cast<glm::uint16*>(destination.data());
            // TODO(tksuoran@gmail.com): glm::packHalf4x16() - but what if we are not aligned?
            ptr[0] = glm::packHalf1x16(value.x);
            ptr[1] = glm::packHalf1x16(value.y);
            ptr[2] = glm::packHalf1x16(value.z);
            ptr[3] = glm::packHalf1x16(value.w);
            break;
        }
        case igl::VertexAttributeFormat::unsigned_byte: {
            auto* const ptr = reinterpret_cast<uint8_t*>(destination.data());
            float scaled_x = std::max(0.0f, std::min(value.x * 255.0f, 255.0f));
            float scaled_y = std::max(0.0f, std::min(value.y * 255.0f, 255.0f));
            float scaled_z = std::max(0.0f, std::min(value.z * 255.0f, 255.0f));
            float scaled_w = std::max(0.0f, std::min(value.w * 255.0f, 255.0f));
            Expects(scaled_x <= 0xffU);
            Expects(scaled_y <= 0xffU);
            Expects(scaled_z <= 0xffU);
            Expects(scaled_w <= 0xffU);
            ptr[0] = static_cast<uint8_t>(scaled_x) & 0xffU;
            ptr[1] = static_cast<uint8_t>(scaled_y) & 0xffU;
            ptr[2] = static_cast<uint8_t>(scaled_z) & 0xffU;
            ptr[3] = static_cast<uint8_t>(scaled_w) & 0xffU;
            break;
        }
        default: {
            ERHE_FATAL("unsupported attribute type");
            break;
        }
    }
}

inline void write_low(
    const gsl::span<std::uint8_t> destination,
    const igl::VertexAttributeFormat  type,
    const glm::uvec4              value
)
{
    switch (type) {
        //using enum igl::VertexAttributeFormat;
        case igl::VertexAttributeFormat::unsigned_byte: {
            auto* const ptr = reinterpret_cast<uint8_t*>(destination.data());
            Expects(value.x <= 0xffU);
            Expects(value.y <= 0xffU);
            Expects(value.z <= 0xffU);
            Expects(value.w <= 0xffU);
            ptr[0] = value.x & 0xffU;
            ptr[1] = value.y & 0xffU;
            ptr[2] = value.z & 0xffU;
            ptr[3] = value.w & 0xffU;
            break;
        }

        case igl::VertexAttributeFormat::unsigned_short: {
            auto* const ptr = reinterpret_cast<uint16_t*>(destination.data());
            Expects(value.x <= 0xffffU);
            Expects(value.y <= 0xffffU);
            Expects(value.z <= 0xffffU);
            Expects(value.w <= 0xffffU);
            ptr[0] = value.x & 0xffffU;
            ptr[1] = value.y & 0xffffU;
            ptr[2] = value.z & 0xffffU;
            ptr[3] = value.w & 0xffffU;
            break;
        }

        case igl::VertexAttributeFormat::unsigned_int: {
            auto* ptr = reinterpret_cast<uint32_t*>(destination.data());
            ptr[0] = value.x;
            ptr[1] = value.y;
            ptr[2] = value.z;
            ptr[3] = value.w;
            break;
        }

        default: {
            ERHE_FATAL("bad index type");
        }
    }
}

} // namespace

Vertex_buffer_writer::Vertex_buffer_writer(
    Build_context& build_context,
    Buffer_sink&   buffer_sink
)
    : build_context{build_context}
    , buffer_sink  {buffer_sink}
{
    Expects(build_context.root.geometry_mesh != nullptr);
    const auto& vertex_buffer_range = build_context.root.geometry_mesh->vertex_buffer_range;
    vertex_data.resize(vertex_buffer_range.count * vertex_buffer_range.element_size);
    vertex_data_span = gsl::make_span(vertex_data);
}

Vertex_buffer_writer::~Vertex_buffer_writer() noexcept
{
    buffer_sink.buffer_ready(*this);
}

auto Vertex_buffer_writer::start_offset() -> std::size_t
{
    return build_context.root.geometry_mesh->vertex_buffer_range.byte_offset;
}

Index_buffer_writer::Index_buffer_writer(
    Build_context& build_context,
    Buffer_sink&   buffer_sink
)
    : build_context  {build_context}
    , buffer_sink    {buffer_sink}
    , index_type     {build_context.root.build_info.buffer_info.index_type}
    , index_type_size{build_context.root.geometry_mesh->index_buffer_range.element_size}
{
    Expects(build_context.root.geometry_mesh != nullptr);
    const auto& geometry_mesh = *build_context.root.geometry_mesh;
    const auto& index_buffer_range = geometry_mesh.index_buffer_range;
    const auto& mesh_info          = build_context.root.mesh_info;
    index_data.resize(index_buffer_range.count * index_type_size);
    index_data_span = gsl::make_span(index_data);

    const auto& primitive_types = build_context.root.build_info.primitive_types;

    if (primitive_types.corner_points) {
        corner_point_index_data_span = index_data_span.subspan(
            geometry_mesh.corner_point_indices.first_index * index_type_size,
            mesh_info.index_count_corner_points * index_type_size
        );
    }
    if (primitive_types.fill_triangles) {
        triangle_fill_index_data_span = index_data_span.subspan(
            geometry_mesh.triangle_fill_indices.first_index * index_type_size,
            mesh_info.index_count_fill_triangles * index_type_size
        );
    }
    if (primitive_types.edge_lines) {
        edge_line_index_data_span = index_data_span.subspan(
            geometry_mesh.edge_line_indices.first_index * index_type_size,
            mesh_info.index_count_edge_lines * index_type_size
        );
    }
    if (primitive_types.centroid_points) {
        polygon_centroid_index_data_span = index_data_span.subspan(
            geometry_mesh.polygon_centroid_indices.first_index * index_type_size,
            mesh_info.polygon_count * index_type_size
        );
    }
}

Index_buffer_writer::~Index_buffer_writer() noexcept
{
    buffer_sink.buffer_ready(*this);
}

auto Index_buffer_writer::start_offset() -> std::size_t
{
    return build_context.root.geometry_mesh->index_buffer_range.byte_offset;
}

void Vertex_buffer_writer::write(
    const Vertex_attribute_info& attribute,
    const glm::vec2              value
)
{
    write_low(
        vertex_data_span.subspan(
            vertex_write_offset + attribute.offset,
            attribute.size
        ),
        attribute.data_type,
        value
    );
}

void Vertex_buffer_writer::write(
    const Vertex_attribute_info& attribute,
    const glm::vec3              value
)
{
    write_low(
        vertex_data_span.subspan(
            vertex_write_offset + attribute.offset,
            attribute.size
        ),
        attribute.data_type,
        value
    );
}

void Vertex_buffer_writer::write(
    const Vertex_attribute_info& attribute,
    const glm::vec4              value
)
{
    write_low(
        vertex_data_span.subspan(
            vertex_write_offset + attribute.offset,
            attribute.size
        ),
        attribute.data_type,
        value
    );
}

void Vertex_buffer_writer::write(
    const Vertex_attribute_info& attribute,
    const uint32_t               value
)
{
    write_low(
        vertex_data_span.subspan(
            vertex_write_offset + attribute.offset,
            attribute.size
        ),
        attribute.data_type,
        value
    );
}

void Vertex_buffer_writer::write(
    const Vertex_attribute_info& attribute,
    const glm::uvec4             value
)
{
    write_low(
        vertex_data_span.subspan(
            vertex_write_offset + attribute.offset,
            attribute.size
        ),
        attribute.data_type,
        value
    );
}

void Vertex_buffer_writer::move(const std::size_t relative_offset)
{
    vertex_write_offset += relative_offset;
}

void Index_buffer_writer::write_corner(const uint32_t v0)
{
    //trace_fmt(log_primitive_builder, "point {}\n", v0);
    write_low(corner_point_index_data_span.subspan(corner_point_indices_written * index_type_size, index_type_size), index_type, v0);
    ++corner_point_indices_written;
}

void Index_buffer_writer::write_triangle(const uint32_t v0, const uint32_t v1, const uint32_t v2)
{
    //trace_fmt(log_primitive_builder, "triangle {}, {}, {}\n", v0, v1, v2);
    write_low(triangle_fill_index_data_span.subspan((triangle_indices_written + 0) * index_type_size, index_type_size), index_type, v0);
    write_low(triangle_fill_index_data_span.subspan((triangle_indices_written + 1) * index_type_size, index_type_size), index_type, v1);
    write_low(triangle_fill_index_data_span.subspan((triangle_indices_written + 2) * index_type_size, index_type_size), index_type, v2);
    triangle_indices_written += 3;
}

void Index_buffer_writer::write_edge(const uint32_t v0, const uint32_t v1)
{
    //trace_fmt(log_primitive_builder, "edge {}, {}\n", v0, v1);
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
