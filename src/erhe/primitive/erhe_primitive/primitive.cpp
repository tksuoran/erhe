#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_primitive/primitive_builder.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_raytrace/ibuffer.hpp"
#include "erhe_raytrace/igeometry.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

namespace erhe::primitive
{

auto c_str(const Primitive_mode primitive_mode) -> const char*
{
    switch (primitive_mode) {
        //using enum Primitive_mode;
        case Primitive_mode::not_set:           return "not_set";
        case Primitive_mode::polygon_fill:      return "polygon_fill";
        case Primitive_mode::edge_lines:        return "edge_lines";
        case Primitive_mode::corner_points:     return "corner_points";
        case Primitive_mode::corner_normals:    return "corner_normals";
        case Primitive_mode::polygon_centroids: return "polygon_centroids";
        case Primitive_mode::count:             return "count";
        default: {
            ERHE_FATAL("Bad mesh mode");
        }
    }
}

auto c_str(const Normal_style normal_style) -> const char*
{
    switch (normal_style) {
        //using enum Normal_style;
        case Normal_style::none:            return "none";
        case Normal_style::corner_normals:  return "corner_normals";
        case Normal_style::polygon_normals: return "polygon_normals";
        case Normal_style::point_normals:   return "point_normals";
        default: {
            ERHE_FATAL("Bad mesh normal style");
        }
    }
}

#pragma region Primitive_raytrace
Primitive_raytrace::Primitive_raytrace() = default;

Primitive_raytrace::Primitive_raytrace(erhe::geometry::Geometry& geometry)
{
    ERHE_PROFILE_FUNCTION();

    const erhe::graphics::Vertex_format vertex_format{
        erhe::graphics::Vertex_attribute::position_float3()
    };
    const std::size_t vertex_stride = vertex_format.stride();
    const std::size_t index_stride = 4;
    const erhe::geometry::Mesh_info mesh_info = geometry.get_mesh_info();
    m_vertex_buffer = erhe::raytrace::IBuffer::create_shared(
        geometry.name + "_vertex",
        mesh_info.vertex_count_corners * vertex_stride
    );
    m_index_buffer = erhe::raytrace::IBuffer::create_shared(
        geometry.name + "_index",
        mesh_info.index_count_fill_triangles * index_stride
    );
    erhe::primitive::Raytrace_buffer_sink buffer_sink{*m_vertex_buffer.get(), *m_index_buffer.get()};
    const erhe::primitive::Build_info build_info{
        .primitive_types = {
            .fill_triangles = true,
        },
        .buffer_info = {
            .normal_style  = erhe::primitive::Normal_style::corner_normals,
            .index_type    = erhe::dataformat::Format::format_32_scalar_uint,
            .vertex_format = vertex_format,
            .buffer_sink   = buffer_sink
        }
    };

    m_mesh = make_renderable_mesh(
        geometry,
        build_info,
        erhe::primitive::Normal_style::none
    );

    make_geometry(geometry.name);
    m_rt_geometry->set_user_data(nullptr);
}

void Primitive_raytrace::make_geometry(std::string_view debug_label)
{
    m_rt_geometry = erhe::raytrace::IGeometry::create_unique(
        fmt::format("{}_rt_geometry", debug_label),
        erhe::raytrace::Geometry_type::GEOMETRY_TYPE_TRIANGLE
    );

    const auto& vertex_buffer_range   = m_mesh.vertex_buffer_range;
    const auto& index_buffer_range    = m_mesh.index_buffer_range;
    const auto& triangle_fill_indices = m_mesh.triangle_fill_indices;

    m_rt_geometry->set_buffer(
        erhe::raytrace::Buffer_type::BUFFER_TYPE_VERTEX,
        0, // slot
        erhe::raytrace::Format::FORMAT_FLOAT3,
        m_vertex_buffer.get(),
        vertex_buffer_range.byte_offset,
        vertex_buffer_range.element_size,
        vertex_buffer_range.count
    );

    ERHE_VERIFY(index_buffer_range.element_size == 4);
    const auto index_count    = index_buffer_range.count;
    const auto index_size     = index_buffer_range.element_size;
    const auto triangle_count = index_count / 3;
    const auto triangle_size  = index_size * 3;
    m_rt_geometry->set_buffer(
        erhe::raytrace::Buffer_type::BUFFER_TYPE_INDEX,
        0, // slot
        erhe::raytrace::Format::FORMAT_UINT3,
        m_index_buffer.get(),
        index_buffer_range.byte_offset + triangle_fill_indices.first_index * index_buffer_range.element_size,
        triangle_size,
        triangle_count
    );
    //SPDLOG_LOGGER_TRACE(log_raytrace, "{}:", m_source_geometry->name);

    {
        ERHE_PROFILE_SCOPE("geometry commit");
        m_rt_geometry->commit();
    }
}

Primitive_raytrace::Primitive_raytrace(erhe::primitive::Triangle_soup& triangle_soup)
{
    ERHE_PROFILE_FUNCTION();

    const erhe::graphics::Vertex_format vertex_format{
        erhe::graphics::Vertex_attribute::position_float3()
    };
    const std::size_t vertex_stride = vertex_format.stride();
    const std::size_t index_stride = 4;
    const std::size_t vertex_count = triangle_soup.get_vertex_count();
    const std::size_t index_count = triangle_soup.get_index_count();
    m_vertex_buffer = erhe::raytrace::IBuffer::create_shared(
        "triangle_soup_raytrace_vertex",
        vertex_count * vertex_stride
    );
    m_index_buffer = erhe::raytrace::IBuffer::create_shared(
        "triangle_soup_raytrace_index",
        index_count * index_stride
    );
    erhe::primitive::Raytrace_buffer_sink buffer_sink{*m_vertex_buffer.get(), *m_index_buffer.get()};
    const erhe::primitive::Buffer_info buffer_info{
        .normal_style  = erhe::primitive::Normal_style::corner_normals,
        .index_type    = erhe::dataformat::Format::format_32_scalar_uint,
        .vertex_format = vertex_format,
        .buffer_sink   = buffer_sink
    };

    m_mesh = build_renderable_mesh_from_triangle_soup(triangle_soup, buffer_info);
    m_rt_geometry = erhe::raytrace::IGeometry::create_unique(
        "triangle_soup_triangle_geometry",
        erhe::raytrace::Geometry_type::GEOMETRY_TYPE_TRIANGLE
    );
    m_rt_geometry->set_user_data(nullptr); // TODO

    const auto& vertex_buffer_range   = m_mesh.vertex_buffer_range;
    const auto& index_buffer_range    = m_mesh.index_buffer_range;
    const auto& triangle_fill_indices = m_mesh.triangle_fill_indices;

    m_rt_geometry->set_buffer(
        erhe::raytrace::Buffer_type::BUFFER_TYPE_VERTEX,
        0, // slot
        erhe::raytrace::Format::FORMAT_FLOAT3,
        m_vertex_buffer.get(),
        vertex_buffer_range.byte_offset,
        vertex_buffer_range.element_size,
        vertex_buffer_range.count
    );

    ERHE_VERIFY(index_buffer_range.element_size == 4);
    const auto index_size     = index_buffer_range.element_size;
    const auto triangle_count = index_count / 3;
    const auto triangle_size  = index_size * 3;
    m_rt_geometry->set_buffer(
        erhe::raytrace::Buffer_type::BUFFER_TYPE_INDEX,
        0, // slot
        erhe::raytrace::Format::FORMAT_UINT3,
        m_index_buffer.get(),
        index_buffer_range.byte_offset + triangle_fill_indices.first_index * index_buffer_range.element_size,
        triangle_size,
        triangle_count
    );
    //SPDLOG_LOGGER_TRACE(log_raytrace, "{}:", m_source_geometry->name);

    {
        ERHE_PROFILE_SCOPE("geometry commit");
        m_rt_geometry->commit();
    }
}

Primitive_raytrace::Primitive_raytrace(const Primitive_raytrace& other) = default;

Primitive_raytrace& Primitive_raytrace::operator=(const Primitive_raytrace& other) = default;

Primitive_raytrace::Primitive_raytrace(Primitive_raytrace&& old) = default;

Primitive_raytrace& Primitive_raytrace::operator=(Primitive_raytrace&& old) = default;

Primitive_raytrace::~Primitive_raytrace() noexcept = default;
#pragma endregion Primitive_raytrace

Primitive::Primitive()
{
}

Primitive::Primitive(const Primitive& other) noexcept = default;

Primitive::Primitive(Primitive&& old) noexcept = default;

Primitive& Primitive::operator=(const Primitive& other) noexcept = default;

Primitive& Primitive::operator=(Primitive&& old) noexcept = default;

Primitive::Primitive(
    const std::shared_ptr<erhe::geometry::Geometry>& geometry,
    const std::shared_ptr<Material>&                 material
)
    : m_geometry{geometry}
    , m_material{material}
{
    if (geometry->has_point_normals()) {
        m_normal_style = erhe::primitive::Normal_style::point_normals;
    } else if (geometry->has_corner_normals()) {
        m_normal_style = erhe::primitive::Normal_style::corner_normals;
    } else if (geometry->has_polygon_normals()) {
        m_normal_style = erhe::primitive::Normal_style::point_normals;
    } else {
        m_normal_style = erhe::primitive::Normal_style::none;
    }
}

Primitive::Primitive(
    Renderable_mesh&&                renderable_mesh,
    const std::shared_ptr<Material>& material
)
    : m_material       {material}
    , m_normal_style   {erhe::primitive::Normal_style::corner_normals}
    , m_renderable_mesh{renderable_mesh}
{
}

Primitive::Primitive(
    const Renderable_mesh&           renderable_mesh,
    const std::shared_ptr<Material>& material
)
    : m_material       {material}
    , m_normal_style   {erhe::primitive::Normal_style::corner_normals}
    , m_renderable_mesh{renderable_mesh}
{
}

Primitive::Primitive(
    const std::shared_ptr<erhe::geometry::Geometry>& geometry,
    const std::shared_ptr<Material>&                 material,
    const Build_info&                                build_info,
    const Normal_style                               normal_style
)
    : m_geometry       {geometry}
    , m_material       {material}
    , m_normal_style   {normal_style}
    , m_renderable_mesh{erhe::primitive::make_renderable_mesh(*geometry.get(), build_info, normal_style)}
    , m_raytrace       {*geometry.get()}
{
}

Primitive::Primitive(
    const std::shared_ptr<erhe::geometry::Geometry>& render_geometry,
    const std::shared_ptr<erhe::geometry::Geometry>& collision_geometry,
    const std::shared_ptr<Material>&                 material,
    const Build_info&                                build_info,
    const Normal_style                               normal_style
)
    : m_geometry       {render_geometry}
    , m_material       {material}
    , m_normal_style   {normal_style}
    , m_renderable_mesh{erhe::primitive::make_renderable_mesh(*render_geometry.get(), build_info, normal_style)}
    , m_raytrace       {*collision_geometry.get()}
{
}

Primitive::Primitive(
    const Triangle_soup&             triangle_soup,
    const std::shared_ptr<Material>& material,
    const Buffer_info&               buffer_info
)
    : m_material       {material}
    , m_normal_style   {Normal_style::corner_normals}
    , m_renderable_mesh{build_renderable_mesh_from_triangle_soup(triangle_soup, buffer_info)}
{
    // For now, when Primitive is created using Triangle_soup, raytrace is always created.
    // TODO Make this optional? But note that currently make_raytrace() needs to be called
    //      before adding Primitive to Mesh.
    make_raytrace();
}

Primitive::Primitive(
    const std::shared_ptr<Triangle_soup>& triangle_soup,
    const std::shared_ptr<Material>&      material
)
    : m_material     {material}
    , m_normal_style {Normal_style::corner_normals}
    , m_triangle_soup{triangle_soup}
    // m_renderable_mesh needs to be explicitly requested using has_renderable_triangles()
{
    // For now, when Primitive is created using Triangle_soup, raytrace is always created.
    // TODO Make this optional? But note that currently make_raytrace() needs to be called
    //      before adding Primitive to Mesh.
    make_raytrace();
}

Primitive::Primitive(
    const Primitive&                 primitive,
    const std::shared_ptr<Material>& material
)
    : m_geometry       {primitive.get_geometry()}
    , m_material       {material}
    , m_triangle_soup  {primitive.get_triangle_soup()}
    , m_normal_style   {primitive.get_normal_style()}
    , m_renderable_mesh{primitive.get_renderable_mesh()} // copy
    , m_raytrace       {primitive.get_geometry_raytrace()}
{
}

Renderable_mesh build_renderable_mesh_from_triangle_soup(const Triangle_soup& triangle_soup, const Buffer_info& buffer_info)
{
    Renderable_mesh renderable_mesh;

    // TODO Use index_type from buffer_info
    const std::size_t sink_vertex_stride   = buffer_info.vertex_format.stride();
    const std::size_t source_vertex_stride = triangle_soup.vertex_format.stride();
    const std::size_t vertex_count         = triangle_soup.vertex_data.size() / source_vertex_stride;
    const std::size_t index_count          = triangle_soup.index_data.size();

    const Buffer_range index_range  = buffer_info.buffer_sink.allocate_index_buffer(index_count, 4);
    const Buffer_range vertex_range = buffer_info.buffer_sink.allocate_vertex_buffer(vertex_count, sink_vertex_stride);

    renderable_mesh.triangle_fill_indices.primitive_type = gl::Primitive_type::triangles;
    renderable_mesh.triangle_fill_indices.first_index = 0;
    renderable_mesh.triangle_fill_indices.index_count = index_count;
    renderable_mesh.index_buffer_range  = index_range;
    renderable_mesh.vertex_buffer_range = vertex_range;

    // Copy indices to buffer
    std::vector<uint8_t> sink_index_data(index_count * index_range.element_size);
    memcpy(sink_index_data.data(), triangle_soup.index_data.data(), index_count * index_range.element_size);
    buffer_info.buffer_sink.enqueue_index_data(index_range.byte_offset, std::move(sink_index_data));

    // Copy and convert vertices to buffer
    std::vector<uint8_t> sink_vertex_data(vertex_count * vertex_range.element_size);
    const std::vector<erhe::graphics::Vertex_attribute>& attributes = buffer_info.vertex_format.get_attributes();
    uint8_t* sink_vertex_data_base = sink_vertex_data.data();
    const uint8_t* src_vertex_data_base = triangle_soup.vertex_data.data();
    for (std::size_t attribute_index = 0, end = attributes.size(); attribute_index < end; ++attribute_index) {
        const erhe::graphics::Vertex_attribute& sink_attribute = attributes[attribute_index];
        const erhe::graphics::Vertex_attribute* src_attribute = triangle_soup.vertex_format.find_attribute_maybe(
            sink_attribute.usage.type,
            static_cast<unsigned int>(sink_attribute.usage.index)
        );
        uint8_t* sink_attribute_base = sink_vertex_data_base + sink_attribute.offset;
        if (src_attribute != nullptr) {
            const uint8_t* src_attribute_base = src_vertex_data_base + src_attribute->offset;
            for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
                uint8_t* sink = sink_attribute_base + vertex_index * sink_vertex_stride;
                const uint8_t* src = src_attribute_base + vertex_index * source_vertex_stride;
                erhe::dataformat::convert(src, src_attribute->data_type, sink, sink_attribute.data_type, 1.0f);
            }
        } else {
            const uint8_t* src = reinterpret_cast<const uint8_t*>(&sink_attribute.default_value[0]);
            for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
                uint8_t* sink = sink_attribute_base + vertex_index * sink_vertex_stride;
                erhe::dataformat::convert(src, erhe::dataformat::Format::format_32_vec4_float, sink, sink_attribute.data_type, 1.0f);
            }
        }
    }

    buffer_info.buffer_sink.enqueue_vertex_data(vertex_range.byte_offset, std::move(sink_vertex_data));

    const erhe::graphics::Vertex_attribute* position_attribute = buffer_info.vertex_format.find_attribute_maybe(erhe::graphics::Vertex_attribute::Usage_type::position);
    erhe::math::Point_vector_bounding_volume_source positions{vertex_count};
    if (position_attribute != nullptr) {
        for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
            const uint8_t* src = src_vertex_data_base + position_attribute->offset + vertex_index * source_vertex_stride;
            float position[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            erhe::dataformat::convert(src, position_attribute->data_type, &position[0], erhe::dataformat::Format::format_32_vec4_float, 1.0f);
            positions.add(position[0], position[1], position[2]);
        }
    }
    erhe::math::calculate_bounding_volume(
        positions,
        renderable_mesh.bounding_box,
        renderable_mesh.bounding_sphere
    );
    return renderable_mesh;
}

Primitive::~Primitive() noexcept
{
}

auto Primitive::has_renderable_triangles() const -> bool
{
    return m_renderable_mesh.index_range(erhe::primitive::Primitive_mode::polygon_fill).index_count > 0;
}

auto Primitive::has_raytrace_triangles() const -> bool
{
    return
        m_raytrace.m_rt_geometry &&
        m_raytrace.m_index_buffer &&
        m_raytrace.m_vertex_buffer &&
        m_raytrace.m_mesh.index_range(erhe::primitive::Primitive_mode::polygon_fill).index_count > 0;
}

void Primitive::make_renderable_mesh(const Build_info& build_info, Normal_style normal_style)
{
    if (m_geometry) {
        m_renderable_mesh = erhe::primitive::make_renderable_mesh(*m_geometry.get(), build_info, normal_style);
    } else if (m_triangle_soup) {
        m_renderable_mesh = build_renderable_mesh_from_triangle_soup(*m_triangle_soup.get(), build_info.buffer_info);
    }
}

void Primitive::make_raytrace()
{
    if (m_geometry) {
        m_raytrace = Primitive_raytrace{*m_geometry.get()};
    } else if (m_triangle_soup) {
        m_raytrace = Primitive_raytrace{*m_triangle_soup.get()};
    }
}

auto Primitive::get_name() const -> std::string_view
{
    return m_geometry ? m_geometry->name : std::string_view{};
}

} // namespace erhe::primitive
