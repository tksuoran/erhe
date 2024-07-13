#include "erhe_primitive/buffer_writer.hpp"
#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_primitive/primitive_builder.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>

#include <span>

namespace erhe::primitive {

namespace {

inline void write_low(
    const std::span<std::uint8_t>  destination,
    const erhe::dataformat::Format format,
    const std::size_t              value
)
{
    switch (format) {
        case erhe::dataformat::Format::format_8_scalar_uint: {
            auto* const ptr = reinterpret_cast<uint8_t*>(destination.data());
            ERHE_VERIFY(value <= 0xffU);
            ptr[0] = value & 0xffU;
            break;
        }

        case erhe::dataformat::Format::format_16_scalar_uint: {
            auto* const ptr = reinterpret_cast<uint16_t*>(destination.data());
            ERHE_VERIFY(value <= 0xffffU);
            ptr[0] = value & 0xffffU;
            break;
        }

        case erhe::dataformat::Format::format_32_scalar_uint: {
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
    const std::span<std::uint8_t>  destination,
    const erhe::dataformat::Format format,
    const unsigned int             value
)
{
    write_low(destination, format, static_cast<std::size_t>(value));
}

inline void write_low(
    const std::span<std::uint8_t>  destination,
    const erhe::dataformat::Format format,
    const glm::vec2                value
)
{
    switch (format) {
        case erhe::dataformat::Format::format_32_vec2_float: {
            auto* const ptr = reinterpret_cast<float*>(destination.data());
            ptr[0] = value.x;
            ptr[1] = value.y;
            break;
        }
        case erhe::dataformat::Format::format_8_vec2_unorm: {
            auto* const ptr = reinterpret_cast<uint8_t*>(destination.data());
            ptr[0] = erhe::dataformat::float_to_unorm8(value.x);
            ptr[1] = erhe::dataformat::float_to_unorm8(value.y);
            break;
        }
        case erhe::dataformat::Format::format_8_vec2_snorm: {
            auto* const ptr = reinterpret_cast<uint8_t*>(destination.data());
            ptr[0] = erhe::dataformat::float_to_snorm8(value.x);
            ptr[1] = erhe::dataformat::float_to_snorm8(value.y);
            break;
        }
        case erhe::dataformat::Format::format_16_vec2_unorm: {
            auto* const ptr = reinterpret_cast<uint16_t*>(destination.data());
            ptr[0] = erhe::dataformat::float_to_unorm16(value.x);
            ptr[1] = erhe::dataformat::float_to_unorm16(value.y);
            break;
        }
        case erhe::dataformat::Format::format_16_vec2_snorm: {
            auto* const ptr = reinterpret_cast<int16_t*>(destination.data());
            ptr[0] = erhe::dataformat::float_to_snorm16(value.x);
            ptr[1] = erhe::dataformat::float_to_snorm16(value.y);
            break;
        }
        default: {
            ERHE_FATAL("unsupported attribute type");
            break;
        }
    }
}

inline void write_low(
    const std::span<std::uint8_t>  destination,
    const erhe::dataformat::Format format,
    const glm::vec3                value
)
{
    switch (format) {
        case erhe::dataformat::Format::format_32_vec3_float: {
            auto* const ptr = reinterpret_cast<float*>(destination.data());
            ptr[0] = value.x;
            ptr[1] = value.y;
            ptr[2] = value.z;
            break;
        }
        case erhe::dataformat::Format::format_8_vec3_unorm: {
            auto* const ptr = reinterpret_cast<uint8_t*>(destination.data());
            ptr[0] = erhe::dataformat::float_to_unorm8(value.x);
            ptr[1] = erhe::dataformat::float_to_unorm8(value.y);
            ptr[2] = erhe::dataformat::float_to_unorm8(value.z);
            break;
        }
        case erhe::dataformat::Format::format_8_vec3_snorm: {
            auto* const ptr = reinterpret_cast<uint8_t*>(destination.data());
            ptr[0] = erhe::dataformat::float_to_snorm8(value.x);
            ptr[1] = erhe::dataformat::float_to_snorm8(value.y);
            ptr[2] = erhe::dataformat::float_to_snorm8(value.z);
            break;
        }
        case erhe::dataformat::Format::format_16_vec3_unorm: {
            auto* const ptr = reinterpret_cast<uint16_t*>(destination.data());
            ptr[0] = erhe::dataformat::float_to_unorm16(value.x);
            ptr[1] = erhe::dataformat::float_to_unorm16(value.y);
            ptr[2] = erhe::dataformat::float_to_unorm16(value.z);
            break;
        }
        case erhe::dataformat::Format::format_16_vec3_snorm: {
            auto* const ptr = reinterpret_cast<int16_t*>(destination.data());
            ptr[0] = erhe::dataformat::float_to_snorm16(value.x);
            ptr[1] = erhe::dataformat::float_to_snorm16(value.y);
            ptr[2] = erhe::dataformat::float_to_snorm16(value.z);
            break;
        }
        default: {
            ERHE_FATAL("unsupported attribute type");
            break;
        }
    }
}

inline void write_low(
    const std::span<std::uint8_t>  destination,
    const erhe::dataformat::Format format,
    const glm::vec4                value
)
{
    switch (format) {
        case erhe::dataformat::Format::format_32_vec4_float: {
            auto* const ptr = reinterpret_cast<float*>(destination.data());
            ptr[0] = value.x;
            ptr[1] = value.y;
            ptr[2] = value.z;
            ptr[3] = value.w;
            break;
        }
        case erhe::dataformat::Format::format_8_vec4_unorm: {
            auto* const ptr = reinterpret_cast<uint8_t*>(destination.data());
            ptr[0] = erhe::dataformat::float_to_unorm8(value.x);
            ptr[1] = erhe::dataformat::float_to_unorm8(value.y);
            ptr[2] = erhe::dataformat::float_to_unorm8(value.z);
            ptr[3] = erhe::dataformat::float_to_unorm8(value.w);
            break;
        }
        case erhe::dataformat::Format::format_8_vec4_snorm: {
            auto* const ptr = reinterpret_cast<uint8_t*>(destination.data());
            ptr[0] = erhe::dataformat::float_to_snorm8(value.x);
            ptr[1] = erhe::dataformat::float_to_snorm8(value.y);
            ptr[2] = erhe::dataformat::float_to_snorm8(value.z);
            ptr[3] = erhe::dataformat::float_to_snorm8(value.w);
            break;
        }
        case erhe::dataformat::Format::format_16_vec4_unorm: {
            auto* const ptr = reinterpret_cast<uint16_t*>(destination.data());
            ptr[0] = erhe::dataformat::float_to_unorm16(value.x);
            ptr[1] = erhe::dataformat::float_to_unorm16(value.y);
            ptr[2] = erhe::dataformat::float_to_unorm16(value.z);
            ptr[3] = erhe::dataformat::float_to_unorm16(value.w);
            break;
        }
        case erhe::dataformat::Format::format_16_vec4_snorm: {
            auto* const ptr = reinterpret_cast<int16_t*>(destination.data());
            ptr[0] = erhe::dataformat::float_to_snorm16(value.x);
            ptr[1] = erhe::dataformat::float_to_snorm16(value.y);
            ptr[2] = erhe::dataformat::float_to_snorm16(value.z);
            ptr[3] = erhe::dataformat::float_to_snorm16(value.w);
            break;
        }
        default: {
            ERHE_FATAL("unsupported attribute type");
            break;
        }
    }
}

inline void write_low(
    const std::span<std::uint8_t>  destination,
    const erhe::dataformat::Format format,
    const glm::uvec4               value
)
{
    switch (format) {
        case erhe::dataformat::Format::format_8_vec4_uint: {
            auto* const ptr = reinterpret_cast<uint8_t*>(destination.data());
            ERHE_VERIFY(value.x <= std::numeric_limits<uint8_t>::max());
            ERHE_VERIFY(value.y <= std::numeric_limits<uint8_t>::max());
            ERHE_VERIFY(value.z <= std::numeric_limits<uint8_t>::max());
            ERHE_VERIFY(value.w <= std::numeric_limits<uint8_t>::max());
            ptr[0] = static_cast<uint8_t>(value.x & 0xffu);
            ptr[1] = static_cast<uint8_t>(value.y & 0xffu);
            ptr[2] = static_cast<uint8_t>(value.z & 0xffu);
            ptr[3] = static_cast<uint8_t>(value.w & 0xffu);
            break;
        }
        case erhe::dataformat::Format::format_16_vec4_uint: {
            auto* const ptr = reinterpret_cast<uint16_t*>(destination.data());
            ERHE_VERIFY(value.x <= std::numeric_limits<uint16_t>::max());
            ERHE_VERIFY(value.y <= std::numeric_limits<uint16_t>::max());
            ERHE_VERIFY(value.z <= std::numeric_limits<uint16_t>::max());
            ERHE_VERIFY(value.w <= std::numeric_limits<uint16_t>::max());
            ptr[0] = static_cast<uint16_t>(value.x & 0xffffu);
            ptr[1] = static_cast<uint16_t>(value.y & 0xffffu);
            ptr[2] = static_cast<uint16_t>(value.z & 0xffffu);
            ptr[3] = static_cast<uint16_t>(value.w & 0xffffu);
            break;
        }
        case erhe::dataformat::Format::format_32_vec4_uint: {
            auto* const ptr = reinterpret_cast<uint32_t*>(destination.data());
            ptr[0] = value.x;
            ptr[1] = value.y;
            ptr[2] = value.z;
            ptr[3] = value.w;
            break;
        }
        default: {
            ERHE_FATAL("unsupported attribute type");
            break;
        }
    }
}

} // namespace

Vertex_buffer_writer::Vertex_buffer_writer(Build_context& build_context, Buffer_sink& buffer_sink)
    : build_context{build_context}
    , buffer_sink  {buffer_sink}
{
    ERHE_VERIFY(build_context.root.buffer_mesh != nullptr);
    const auto& vertex_buffer_range = build_context.root.buffer_mesh->vertex_buffer_range;
    vertex_data.resize(vertex_buffer_range.count * vertex_buffer_range.element_size);
    vertex_data_span = vertex_data;
}

Vertex_buffer_writer::~Vertex_buffer_writer() noexcept
{
    buffer_sink.buffer_ready(*this);
}

auto Vertex_buffer_writer::start_offset() -> std::size_t
{
    return build_context.root.buffer_mesh->vertex_buffer_range.byte_offset;
}

Index_buffer_writer::Index_buffer_writer(Build_context& build_context, Buffer_sink& buffer_sink)
    : build_context  {build_context}
    , buffer_sink    {buffer_sink}
    , index_type     {build_context.root.build_info.buffer_info.index_type}
    , index_type_size{build_context.root.buffer_mesh->index_buffer_range.element_size}
{
    ERHE_VERIFY(build_context.root.buffer_mesh != nullptr);
    const auto& buffer_mesh        = *build_context.root.buffer_mesh;
    const auto& index_buffer_range = buffer_mesh.index_buffer_range;
    const auto& mesh_info          = build_context.root.mesh_info;
    index_data.resize(index_buffer_range.count * index_type_size);
    index_data_span = index_data;

    const auto& primitive_types = build_context.root.build_info.primitive_types;

    if (primitive_types.corner_points) {
        corner_point_index_data_span = index_data_span.subspan(
            buffer_mesh.corner_point_indices.first_index * index_type_size,
            mesh_info.index_count_corner_points * index_type_size
        );
    }
    if (primitive_types.fill_triangles) {
        triangle_fill_index_data_span = index_data_span.subspan(
            buffer_mesh.triangle_fill_indices.first_index * index_type_size,
            mesh_info.index_count_fill_triangles * index_type_size
        );
    }
    if (primitive_types.edge_lines) {
        edge_line_index_data_span = index_data_span.subspan(
            buffer_mesh.edge_line_indices.first_index * index_type_size,
            mesh_info.index_count_edge_lines * index_type_size
        );
    }
    if (primitive_types.centroid_points) {
        polygon_centroid_index_data_span = index_data_span.subspan(
            buffer_mesh.polygon_centroid_indices.first_index * index_type_size,
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
    return build_context.root.buffer_mesh->index_buffer_range.byte_offset;
}

void Vertex_buffer_writer::write(const Vertex_attribute_info& attribute, const glm::vec2 value)
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

void Vertex_buffer_writer::write(const Vertex_attribute_info& attribute, const glm::vec3 value)
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

void Vertex_buffer_writer::write(const Vertex_attribute_info& attribute, const glm::vec4 value
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

void Vertex_buffer_writer::write(const Vertex_attribute_info& attribute, const uint32_t value)
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

void Vertex_buffer_writer::write(const Vertex_attribute_info& attribute, const glm::uvec4 value)
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
