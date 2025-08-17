#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_primitive/primitive_builder.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_buffer/ibuffer.hpp"
#include "erhe_raytrace/igeometry.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

namespace erhe::primitive {

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

Primitive_raytrace::Primitive_raytrace(const GEO::Mesh& mesh, Element_mappings* element_mappings)
{
    ERHE_PROFILE_FUNCTION();

    const erhe::dataformat::Vertex_format vertex_format{
        {
            0,
            {{erhe::dataformat::Format::format_32_vec3_float, erhe::dataformat::Vertex_attribute_usage::position}}
        }
    };
    const erhe::dataformat::Vertex_stream& vertex_stream = vertex_format.streams.front();
    const std::size_t vertex_stride = vertex_stream.stride;
    const std::size_t index_stride = 4;
    const Mesh_info mesh_info = get_mesh_info(mesh);
    m_rt_vertex_buffer = std::make_shared<erhe::buffer::Cpu_buffer>("raytrace_vertex", mesh_info.vertex_count_corners * vertex_stride);
    m_rt_index_buffer  = std::make_shared<erhe::buffer::Cpu_buffer>("raytrace_index",  mesh_info.index_count_fill_triangles * index_stride);
    Cpu_buffer_sink buffer_sink{{m_rt_vertex_buffer.get()}, *m_rt_index_buffer.get()};
    Build_info build_info{
        .primitive_types = {
            .fill_triangles = true,
        },
        .buffer_info = {
            .normal_style  = Normal_style::corner_normals,
            .index_type    = erhe::dataformat::Format::format_32_scalar_uint,
            .vertex_format = vertex_format,
            .buffer_sink   = buffer_sink
        }
    };

    Element_mappings dummy_mappings;

    build_buffer_mesh(
        m_rt_mesh,
        mesh, 
        build_info, 
        (element_mappings != nullptr) ? *element_mappings : dummy_mappings, 
        Normal_style::none
    );
    
    make_raytrace_geometry();
    m_rt_geometry->set_user_data(nullptr);
}

auto Primitive_raytrace::get_raytrace_mesh() const -> const Buffer_mesh&
{
    return m_rt_mesh;
}

auto Primitive_raytrace::get_raytrace_geometry() const -> const std::shared_ptr<erhe::raytrace::IGeometry>&
{
    return m_rt_geometry;
}

auto Primitive_raytrace::has_raytrace_triangles() const -> bool
{
    return
        m_rt_geometry &&
        m_rt_index_buffer &&
        m_rt_vertex_buffer &&
        m_rt_mesh.index_range(Primitive_mode::polygon_fill).index_count > 0;
}

void Primitive_raytrace::make_raytrace_geometry()
{
    m_rt_geometry = erhe::raytrace::IGeometry::create_unique("rt_geometry", erhe::raytrace::Geometry_type::GEOMETRY_TYPE_TRIANGLE);

    ERHE_VERIFY(m_rt_mesh.vertex_buffer_ranges.size() == 1);
    const auto& vertex_buffer_range   = m_rt_mesh.vertex_buffer_ranges.front();
    const auto& index_buffer_range    = m_rt_mesh.index_buffer_range;
    const auto& triangle_fill_indices = m_rt_mesh.triangle_fill_indices;

    m_rt_geometry->set_buffer(
        erhe::raytrace::Buffer_type::BUFFER_TYPE_VERTEX,
        0, // slot
        erhe::dataformat::Format::format_32_vec3_float,
        m_rt_vertex_buffer.get(),
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
        erhe::dataformat::Format::format_32_vec3_uint,
        m_rt_index_buffer.get(),
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

Primitive_raytrace::Primitive_raytrace(Triangle_soup& triangle_soup)
{
    ERHE_PROFILE_FUNCTION();

    const erhe::dataformat::Vertex_format vertex_format{
        {
            0,
            {{erhe::dataformat::Format::format_32_vec3_float, erhe::dataformat::Vertex_attribute_usage::position}}
        }
    };
    const erhe::dataformat::Vertex_stream& vertex_stream = vertex_format.streams.front();
    const std::size_t vertex_stride = vertex_stream.stride;
    const std::size_t index_stride = 4;
    const std::size_t vertex_count = triangle_soup.get_vertex_count();
    const std::size_t index_count = triangle_soup.get_index_count();
    m_rt_vertex_buffer = std::make_shared<erhe::buffer::Cpu_buffer>("triangle_soup_raytrace_vertex", vertex_count * vertex_stride);
    m_rt_index_buffer = std::make_shared<erhe::buffer::Cpu_buffer>("triangle_soup_raytrace_index", index_count * index_stride);
    Cpu_buffer_sink buffer_sink{{m_rt_vertex_buffer.get()}, *m_rt_index_buffer.get()};
    const Buffer_info buffer_info{
        .normal_style  = Normal_style::corner_normals,
        .index_type    = erhe::dataformat::Format::format_32_scalar_uint,
        .vertex_format = vertex_format,
        .buffer_sink   = buffer_sink
    };

    std::optional<Buffer_mesh> buffer_mesh_opt = build_buffer_mesh_from_triangle_soup(triangle_soup, buffer_info);
    if (!buffer_mesh_opt.has_value()) {
        return; // TODO
    }
    m_rt_mesh = buffer_mesh_opt.value();
    m_rt_geometry = erhe::raytrace::IGeometry::create_unique(
        "triangle_soup_triangle_geometry",
        erhe::raytrace::Geometry_type::GEOMETRY_TYPE_TRIANGLE
    );
    m_rt_geometry->set_user_data(nullptr); // TODO

    ERHE_VERIFY(m_rt_mesh.vertex_buffer_ranges.size() == 1);

    const auto& vertex_buffer_range   = m_rt_mesh.vertex_buffer_ranges.front();
    const auto& index_buffer_range    = m_rt_mesh.index_buffer_range;
    const auto& triangle_fill_indices = m_rt_mesh.triangle_fill_indices;

    m_rt_geometry->set_buffer(
        erhe::raytrace::Buffer_type::BUFFER_TYPE_VERTEX,
        0, // slot
        erhe::dataformat::Format::format_32_vec3_float,
        m_rt_vertex_buffer.get(),
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
        erhe::dataformat::Format::format_32_vec3_uint,
        m_rt_index_buffer.get(),
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

#pragma region Primitive_shape
Primitive_shape::Primitive_shape()
{
}

Primitive_shape::Primitive_shape(const Primitive_shape& other) noexcept = default;

Primitive_shape::Primitive_shape(Primitive_shape&& old) noexcept = default;

Primitive_shape& Primitive_shape::operator=(const Primitive_shape& other) noexcept = default;

Primitive_shape& Primitive_shape::operator=(Primitive_shape&& old) noexcept = default;

Primitive_shape::Primitive_shape(const std::shared_ptr<erhe::geometry::Geometry>& geometry)
    : m_geometry{geometry}
{
}

Primitive_shape::Primitive_shape(const std::shared_ptr<Triangle_soup>& triangle_soup)
    : m_triangle_soup{triangle_soup}
{
}

Primitive_shape::~Primitive_shape() noexcept
{
}

auto Primitive_shape::make_geometry() -> bool
{
    if (m_geometry) {
        return true;
    }
    if (m_triangle_soup) {
        ERHE_VERIFY(m_element_mappings.triangle_to_mesh_facet.empty());
        ERHE_VERIFY(m_element_mappings.mesh_corner_to_vertex_buffer_index.empty());
        m_geometry = std::make_shared<erhe::geometry::Geometry>();
        GEO::Mesh& mesh = m_geometry->get_mesh();

        mesh_from_triangle_soup(*m_triangle_soup.get(), mesh, m_element_mappings);
        //mesh.vertices.set_double_precision();
        //mesh.facets.connect();
        //mesh.edges.create_edges(
        //if (mesh.edges.nb() == 0) {
        //    static int count_bad = 0;
        //    ++count_bad; // breakpoint placeholder
        //} else {
        //    static int count_good = 0;
        //    ++count_good; // breakpoint placeholder
        //}
        //mesh.vertices.set_single_precision();
        //m_geometry->update_connectivity();
        m_geometry->process(
            erhe::geometry::Geometry::process_flag_connect                       |
            erhe::geometry::Geometry::process_flag_build_edges                   |
            erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals | 
            erhe::geometry::Geometry::process_flag_compute_facet_centroids
        );
        // compute_mesh_tangents();
        return true;
    }
    return false;
}

auto Primitive_shape::get_geometry() -> const std::shared_ptr<erhe::geometry::Geometry>&
{
    if (!m_geometry) {
        make_geometry();
    }
    return m_geometry;
}

auto Primitive_shape::get_geometry_const() const -> const std::shared_ptr<erhe::geometry::Geometry>&
{
    return m_geometry;
}

auto Primitive_shape::get_raytrace() -> Primitive_raytrace&
{
    return m_raytrace;
}

auto Primitive_shape::get_raytrace() const -> const Primitive_raytrace&
{
    return m_raytrace;
}

auto Primitive_shape::get_triangle_soup() const -> const std::shared_ptr<Triangle_soup>&
{
    return m_triangle_soup;
}

auto Primitive_shape::has_raytrace_triangles() const -> bool
{
    return m_raytrace.has_raytrace_triangles();
}

auto Primitive_shape::make_raytrace(const GEO::Mesh& mesh) -> bool
{
    m_raytrace = Primitive_raytrace{mesh, nullptr};
    return m_raytrace.has_raytrace_triangles();
}

auto Primitive_shape::make_raytrace() -> bool
{
    // Ensure geometry and element mappings exists
    if (!m_geometry) {
        if (!make_geometry()) {
            return false;
        }
    }

    //bool has_element_mappings =
    //    !m_element_mappings.triangle_to_mesh_facet.empty() &&
    //    !m_element_mappings.mesh_corner_to_vertex_buffer_index.empty();

    return make_raytrace(m_geometry->get_mesh());

    // TODO Is it possible to make raytrace only / directly from triangle soup?
    //      We would lack element mappings, is that still useful?
    // 
    // if (m_geometry) {
    //     m_raytrace = Primitive_raytrace{*m_geometry.get()};
    // } else if (m_triangle_soup) {
    //     m_raytrace = Primitive_raytrace{*m_triangle_soup.get()};
    // }
}
#pragma endregion Primitive_shape

#pragma region Primitive_render_shape
Primitive_render_shape::Primitive_render_shape(const std::shared_ptr<erhe::geometry::Geometry>& geometry)
    : Primitive_shape{geometry}
{
    // TODO if (geometry->has_corner_normals()) {
        m_normal_style = Normal_style::corner_normals;
    //} else if (geometry->has_point_normals()) {
    //    m_normal_style = Normal_style::point_normals;
    //} else if (geometry->has_polygon_normals()) {
    //    m_normal_style = Normal_style::polygon_normals;
    //} else {
    //    m_normal_style = Normal_style::none;
    //}
}

Primitive_render_shape::Primitive_render_shape(Buffer_mesh&& renderable_mesh)
    : m_normal_style   {Normal_style::corner_normals}
    , m_renderable_mesh{renderable_mesh}
{
}

Primitive_render_shape::Primitive_render_shape(const Buffer_mesh& renderable_mesh)
    : m_normal_style   {Normal_style::corner_normals}
    , m_renderable_mesh{renderable_mesh}
{
}

Primitive_render_shape::Primitive_render_shape(const std::shared_ptr<Triangle_soup>& triangle_soup)
    : Primitive_shape{triangle_soup}
    , m_normal_style {Normal_style::corner_normals}
{
}

auto Primitive_render_shape::has_buffer_mesh_triangles() const -> bool
{
    return m_renderable_mesh.index_range(Primitive_mode::polygon_fill).index_count > 0;
}

auto Primitive_render_shape::make_buffer_mesh(const Build_info& build_info, Normal_style normal_style) -> bool
{
    if (m_geometry) {
        // TODO temp hack
        m_element_mappings.triangle_to_mesh_facet.clear();
        m_element_mappings.mesh_corner_to_vertex_buffer_index.clear();
        m_element_mappings.mesh_vertex_to_vertex_buffer_index.clear();
        return build_buffer_mesh(
            m_renderable_mesh,
            m_geometry->get_mesh(),
            build_info,
            m_element_mappings,
            normal_style
        );
    } else {
        return make_buffer_mesh(build_info.buffer_info);
    }
}

auto Primitive_render_shape::make_buffer_mesh(const Buffer_info& buffer_info) -> bool
{
    if (!m_triangle_soup) {
        return false;
    }
    std::optional<Buffer_mesh> buffer_mesh_opt = build_buffer_mesh_from_triangle_soup(*m_triangle_soup.get(), buffer_info);
    if (buffer_mesh_opt.has_value()) {
        m_renderable_mesh = buffer_mesh_opt.value();
    }
    return buffer_mesh_opt.has_value();
}
#pragma endregion Primitive_render_shape

auto build_buffer_mesh_from_triangle_soup(const Triangle_soup& triangle_soup, const Buffer_info& buffer_info) -> std::optional<Buffer_mesh>
{
    // TODO Use index_type from buffer_info
    //const std::size_t  sink_vertex_stride   = buffer_info.vertex_format.stride();
    const std::size_t  source_vertex_stride = triangle_soup.vertex_format.streams.front().stride;
    const std::size_t  vertex_count         = triangle_soup.vertex_data.size() / source_vertex_stride;
    const std::size_t  index_count          = triangle_soup.index_data.size();
    const Buffer_range index_range          = buffer_info.buffer_sink.allocate_index_buffer(index_count, 4);
    if (index_range.count == 0) {
        return std::optional<Buffer_mesh>{};
    }

    Buffer_mesh buffer_mesh;
    buffer_mesh.triangle_fill_indices.primitive_type = Primitive_type::triangles;
    buffer_mesh.triangle_fill_indices.first_index = 0;
    buffer_mesh.triangle_fill_indices.index_count = index_count;
    buffer_mesh.index_buffer_range  = index_range;

    for (std::size_t i = 0, end = buffer_info.vertex_format.streams.size(); i < end; ++i) {
        const erhe::dataformat::Vertex_stream& stream = buffer_info.vertex_format.streams.at(i);
        Buffer_range vertex_range = buffer_info.buffer_sink.allocate_vertex_buffer(i, vertex_count, stream.stride);
        if (vertex_range.count == 0) {
            // TODO free allocations so far
            return std::optional<Buffer_mesh>{};
        }
        buffer_mesh.vertex_buffer_ranges.emplace_back(std::move(vertex_range));
    }

    // Copy indices to buffer
    {
        std::vector<uint8_t> sink_index_data(index_count * index_range.element_size);
        memcpy(sink_index_data.data(), triangle_soup.index_data.data(), index_count * index_range.element_size);
        buffer_info.buffer_sink.enqueue_index_data(index_range.byte_offset, std::move(sink_index_data));
    }

    // Copy and convert vertices to buffer
    for (size_t stream_index = 0, stream_end = buffer_info.vertex_format.streams.size(); stream_index < stream_end; ++stream_index) {
        const erhe::dataformat::Vertex_stream& sink_stream = buffer_info.vertex_format.streams[stream_index];
        std::vector<uint8_t> sink_vertex_data(vertex_count * sink_stream.stride);
        const std::vector<erhe::dataformat::Vertex_attribute>& attributes = sink_stream.attributes;
        uint8_t* sink_vertex_data_base = sink_vertex_data.data();
        const uint8_t* src_vertex_data_base = triangle_soup.vertex_data.data();
        for (std::size_t attribute_index = 0, attribute_index_end = attributes.size(); attribute_index < attribute_index_end; ++attribute_index) {
            const erhe::dataformat::Vertex_attribute& sink_attribute = attributes[attribute_index];
            const erhe::dataformat::Attribute_stream src = triangle_soup.vertex_format.find_attribute(
                sink_attribute.usage_type,
                static_cast<unsigned int>(sink_attribute.usage_index)
            );
            uint8_t* sink_attribute_base = sink_vertex_data_base + sink_attribute.offset;
            if (src.attribute != nullptr) {
                const uint8_t* src_attribute_base = src_vertex_data_base + src.attribute->offset;
                for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
                    uint8_t* sink = sink_attribute_base + vertex_index * sink_stream.stride;
                    const uint8_t* src_data = src_attribute_base + vertex_index * source_vertex_stride;
                    erhe::dataformat::convert(src_data, src.attribute->format, sink, sink_attribute.format, 1.0f);
                }
            } else {
                const float src_data[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
                    uint8_t* sink = sink_attribute_base + vertex_index * sink_stream.stride;
                    erhe::dataformat::convert(src_data, erhe::dataformat::Format::format_32_vec4_float, sink, sink_attribute.format, 1.0f);
                }
            }
        }
        buffer_info.buffer_sink.enqueue_vertex_data(
            stream_index,
            buffer_mesh.vertex_buffer_ranges[stream_index].byte_offset,
            std::move(sink_vertex_data)
        );
    }

    const erhe::dataformat::Attribute_stream position = triangle_soup.vertex_format.find_attribute(erhe::dataformat::Vertex_attribute_usage::position);
    erhe::math::Point_vector_bounding_volume_source positions{vertex_count};
    if (position.attribute != nullptr) {
        for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
            const uint8_t* src_vertex_data_base = triangle_soup.vertex_data.data();
            const uint8_t* src = src_vertex_data_base + position.attribute->offset + vertex_index * position.stream->stride;
            float pos[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            erhe::dataformat::convert(src, position.attribute->format, &pos[0], erhe::dataformat::Format::format_32_vec4_float, 1.0f);
            positions.add(pos[0], pos[1], pos[2]);
        }
    }
    erhe::math::calculate_bounding_volume(positions, buffer_mesh.bounding_box, buffer_mesh.bounding_sphere);
    return buffer_mesh;
}

auto Primitive_shape::get_mesh_facet_from_triangle(const uint32_t triangle) const -> GEO::index_t
{
    if (m_element_mappings.triangle_to_mesh_facet.empty()) {
        return GEO::NO_INDEX;
    }
    ERHE_VERIFY(triangle < m_element_mappings.triangle_to_mesh_facet.size());
    return m_element_mappings.triangle_to_mesh_facet[triangle];
}

auto Primitive_shape::get_element_mappings() const -> const Element_mappings&
{
    return m_element_mappings;
}

/////////////////////////


Primitive::Primitive()
{
}

Primitive::Primitive(const Primitive&) = default;
Primitive::Primitive(Primitive&&) = default;
Primitive& Primitive::operator=(const Primitive&) = default;
Primitive& Primitive::operator=(Primitive&&) = default;

Primitive::~Primitive()
{
}

Primitive::Primitive(const std::shared_ptr<Triangle_soup>& triangle_soup)
    : render_shape{std::make_shared<Primitive_render_shape>(triangle_soup)}
{
}

Primitive::Primitive(const Buffer_mesh& renderable_mesh)
    : render_shape{std::make_shared<Primitive_render_shape>(renderable_mesh)}
{
}

Primitive::Primitive(const std::shared_ptr<erhe::geometry::Geometry>& geometry)
    : render_shape{std::make_shared<Primitive_render_shape>(geometry)}
{
}

Primitive::Primitive(
    const std::shared_ptr<erhe::geometry::Geometry>& geometry,
    const Build_info&                                build_info,
    const Normal_style                               normal_style
)
    : render_shape{std::make_shared<Primitive_render_shape>(geometry)}
{
    const bool ok = make_renderable_mesh(build_info, normal_style);
    ERHE_VERIFY(ok);
}

Primitive::Primitive(
    const std::shared_ptr<erhe::geometry::Geometry>& render_geometry,
    const std::shared_ptr<erhe::geometry::Geometry>& collision_geometry
)
    : render_shape   {std::make_shared<Primitive_render_shape>(render_geometry)}
    , collision_shape{std::make_shared<Primitive_shape>(collision_geometry)}
{
    ERHE_VERIFY(render_geometry);
    ERHE_VERIFY(collision_geometry);
    ERHE_VERIFY(render_geometry != collision_geometry);
}

auto Primitive::has_renderable_triangles() const -> bool
{
    return render_shape ? render_shape->has_buffer_mesh_triangles() : false;
}

auto Primitive::has_raytrace_triangles() const -> bool
{
    if (render_shape && render_shape->has_raytrace_triangles()) {
        return true;
    }
    if (collision_shape && collision_shape->has_raytrace_triangles()) {
        return true;
    }
    return false;
}

auto Primitive::make_geometry() -> bool
{
    if (render_shape) {
        if (render_shape->make_geometry()) {
            return true;
        }
    }
    if (collision_shape) {
        if (collision_shape->make_geometry()) {
            return true;
        }
    }
    return false;
}

auto Primitive::make_raytrace() -> bool
{
    if (collision_shape) {
        if (collision_shape->has_raytrace_triangles()) {
            return true;
        }
        if (collision_shape->make_raytrace()) {
            return true;
        }
    }
    if (render_shape) {
        if (render_shape->has_raytrace_triangles()) {
            return true;
        }
        if (render_shape->make_raytrace()) {
            return true;
        }
    }
    return false;
}

auto Primitive::make_renderable_mesh(const Build_info& build_info, Normal_style normal_style) -> bool
{
    if (!render_shape) {
        return false;
    }
    if (render_shape->has_buffer_mesh_triangles()) {
        return true;
    }
    return render_shape->make_buffer_mesh(build_info, normal_style);
}

auto Primitive::make_renderable_mesh(const Buffer_info& buffer_info) -> bool
{
    if (!render_shape) {
        return false;
    }
    return render_shape->make_buffer_mesh(buffer_info);
}

auto Primitive::get_renderable_mesh() const -> const Buffer_mesh*
{
    if (!render_shape) {
        return nullptr;
    }
    return &render_shape->get_renderable_mesh();
}

static std::string Primitive__get_name__empty{};

auto Primitive::get_name() const -> std::string_view
{
    const std::shared_ptr<erhe::geometry::Geometry>& render_geometry    = render_shape ? render_shape->get_geometry_const() : std::shared_ptr<erhe::geometry::Geometry>{};
    const std::shared_ptr<erhe::geometry::Geometry>& collision_geometry = collision_shape ? collision_shape->get_geometry_const() : std::shared_ptr<erhe::geometry::Geometry>{};
    return
        render_geometry    ? render_geometry   ->get_name() :
        collision_geometry ? collision_geometry->get_name() : Primitive__get_name__empty;
}

auto Primitive::get_bounding_box() const -> erhe::math::Aabb
{
    erhe::math::Aabb bounding_box{};
    if (render_shape) {
        bounding_box.include(render_shape->get_renderable_mesh().bounding_box);
    }
    if (collision_shape) {
        bounding_box.include(collision_shape->get_raytrace().get_raytrace_mesh().bounding_box);
    }
    return bounding_box;
}

auto Primitive::get_shape_for_raytrace() const -> std::shared_ptr<Primitive_shape>
{
    ERHE_VERIFY(collision_shape || render_shape );
    return collision_shape ? collision_shape : render_shape;

    // TODO Should this version be considered?
    //
    // if (collision_shape && collision_shape->has_raytrace_triangles()) {
    //     return collision_shape;
    // }
    // if (render_shape && render_shape->has_raytrace_triangles()) {
    //     return render_shape;
    // }
}

} // namespace erhe::primitive
