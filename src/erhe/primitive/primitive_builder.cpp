#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/primitive/index_range.hpp"
#include "erhe/primitive/log.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/geometry/property_map.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/configuration.hpp"
#include "erhe/graphics/vertex_attribute.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/graphics/state/vertex_input_state.hpp"
#include "erhe/toolkit/math_util.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/packing.hpp>
#include <glm/gtc/type_precision.hpp>

#include <gsl/span>
#include <map>
#include <stdexcept>
#include <cassert>

// For debugging
#define GENERATE_POLYGONS 1
#define GENERATE_CORNERS  1

namespace erhe::primitive
{

using Corner_id         = erhe::geometry::Corner_id;
using Point_id          = erhe::geometry::Point_id;
using Polygon_id        = erhe::geometry::Polygon_id;
using Edge_id           = erhe::geometry::Edge_id;
using Point_corner_id   = erhe::geometry::Point_corner_id;
using Polygon_corner_id = erhe::geometry::Polygon_corner_id;
using Edge_polygon_id   = erhe::geometry::Edge_polygon_id;
using Corner            = erhe::geometry::Corner;
using Point             = erhe::geometry::Point;
using Polygon           = erhe::geometry::Polygon;
using Edge              = erhe::geometry::Edge;
using erhe::geometry::Geometry;
using erhe::graphics::Configuration;
using erhe::graphics::Vertex_attribute;
using gl::size_of_type;
using erhe::log::Log;

using glm::vec2;
using glm::vec3;
using glm::vec4;
using glm::mat4;

namespace
{

inline void write_low(gsl::span<std::byte>   destination,
                      gl::Draw_elements_type type,
                      size_t                 value)
{
    switch (type)
    {
        case gl::Draw_elements_type::unsigned_byte:
        {
            auto* ptr = reinterpret_cast<uint8_t*>(destination.data());
            Expects(value <= 0xffU);
            ptr[0] = value & 0xffU;
            break;
        }

        case gl::Draw_elements_type::unsigned_short:
        {
            auto* ptr = reinterpret_cast<uint16_t*>(destination.data());
            Expects(value <= 0xffffU);
            ptr[0] = value & 0xffffU;
            break;
        }

        case gl::Draw_elements_type::unsigned_int:
        {
            auto* ptr = reinterpret_cast<uint32_t*>(destination.data());
            ptr[0] = value & 0xffffffffu;
            break;
        }

        default:
        {
            FATAL("bad index type");
        }
    }
}

inline void write_low(gsl::span<std::byte>   destination,
                      gl::Vertex_attrib_type type,
                      unsigned int           value)
{
    switch (type)
    {
        case gl::Vertex_attrib_type::unsigned_byte:
        {
            auto* ptr = reinterpret_cast<uint8_t*>(destination.data());
            Expects(value <= 0xffU);
            ptr[0] = value & 0xffU;
            break;
        }

        case gl::Vertex_attrib_type::unsigned_short:
        {
            auto* ptr = reinterpret_cast<uint16_t*>(destination.data());
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

inline void write_low(gsl::span<std::byte>   destination,
                      gl::Vertex_attrib_type type,
                      vec2                   value)
{
    if (type == gl::Vertex_attrib_type::float_)
    {
        auto* ptr = reinterpret_cast<float*>(destination.data());
        ptr[0] = value.x;
        ptr[1] = value.y;
    }
    else if (type == gl::Vertex_attrib_type::half_float)
    {
        // TODO(tksuoran@gmail.com): Would this be safe even if we are not aligned?
        // uint* ptr = reinterpret_cast<uint*>(data_ptr);
        // *ptr = glm::packHalf2x16(value);
        auto* ptr = reinterpret_cast<glm::uint16*>(destination.data());
        ptr[0] = glm::packHalf1x16(value.x);
        ptr[1] = glm::packHalf1x16(value.y);
    }
    else
    {
        FATAL("unsupported attribute type");
    }
}

inline void write_low(gsl::span<std::byte>   destination,
                      gl::Vertex_attrib_type type,
                      vec3                   value)
{
    if (type == gl::Vertex_attrib_type::float_)
    {
        auto* ptr = reinterpret_cast<float*>(destination.data());
        ptr[0] = value.x;
        ptr[1] = value.y;
        ptr[2] = value.z;
    }
    else if (type == gl::Vertex_attrib_type::half_float)
    {
        auto* ptr = reinterpret_cast<glm::uint16 *>(destination.data());
        ptr[0] = glm::packHalf1x16(value.x);
        ptr[1] = glm::packHalf1x16(value.y);
        ptr[2] = glm::packHalf1x16(value.z);
    }
    else
    {
        FATAL("unsupported attribute type");
    }
}

inline void write_low(gsl::span<std::byte>   destination,
                      gl::Vertex_attrib_type type,
                      vec4                   value)
{
    if (type == gl::Vertex_attrib_type::float_)
    {
        auto* ptr = reinterpret_cast<float*>(destination.data());
        ptr[0] = value.x;
        ptr[1] = value.y;
        ptr[2] = value.z;
        ptr[3] = value.w;
    }
    else if (type == gl::Vertex_attrib_type::half_float)
    {
        auto* ptr = reinterpret_cast<glm::uint16*>(destination.data());
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

Primitive_builder::Primitive_builder(const Geometry&    geometry,
                                     const Format_info& format_info,
                                     Buffer_info&       buffer_info)
    : m_geometry   {geometry}
    , m_format_info{format_info}
    , m_buffer_info{buffer_info}
{
}

Primitive_builder::Property_maps::Property_maps(const Geometry&    geometry,
                                                const Format_info& format_info)
{
    ZoneScoped;

    log_primitive_builder.trace("Property_maps::Property_maps() for geometry = {}\n", geometry.name());
    Log::Indenter indenter;

    polygon_normals      = geometry.polygon_attributes().find<vec3>(erhe::geometry::c_polygon_normals     );
    polygon_centroids    = geometry.polygon_attributes().find<vec3>(erhe::geometry::c_polygon_centroids   );
    corner_normals       = geometry.corner_attributes ().find<vec3>(erhe::geometry::c_corner_normals      );
    corner_tangents      = geometry.corner_attributes ().find<vec4>(erhe::geometry::c_corner_tangents     );
    corner_bitangents    = geometry.corner_attributes ().find<vec4>(erhe::geometry::c_corner_bitangents   );
    corner_texcoords     = geometry.corner_attributes ().find<vec2>(erhe::geometry::c_corner_texcoords    );
    corner_colors        = geometry.corner_attributes ().find<vec4>(erhe::geometry::c_corner_colors       );
    point_locations      = geometry.point_attributes  ().find<vec3>(erhe::geometry::c_point_locations     );
    point_normals        = geometry.point_attributes  ().find<vec3>(erhe::geometry::c_point_normals       );
    point_normals_smooth = geometry.point_attributes  ().find<vec3>(erhe::geometry::c_point_normals_smooth);
    point_tangents       = geometry.point_attributes  ().find<vec4>(erhe::geometry::c_point_tangents      );
    point_bitangents     = geometry.point_attributes  ().find<vec4>(erhe::geometry::c_point_bitangents    );
    point_texcoords      = geometry.point_attributes  ().find<vec2>(erhe::geometry::c_point_texcoords     );
    point_colors         = geometry.point_attributes  ().find<vec4>(erhe::geometry::c_point_colors        );

    if (format_info.want_id)
    {
        polygon_ids_vector3 = polygon_attributes.create<vec3>(erhe::geometry::c_polygon_ids_vec3);
        log_primitive_builder.trace("-created polygon_ids_vec3\n");

        if (Configuration::info.use_integer_polygon_ids)
        {
            polygon_ids_uint32 = polygon_attributes.create<unsigned int>(erhe::geometry::c_polygon_ids_uint);
            log_primitive_builder.trace("-created polygon_ids_uint\n");
        }
    }

    if (format_info.want_normal)
    {
        if (point_locations != nullptr)
        {
            polygon_normals = polygon_attributes.find_or_create<vec3>(erhe::geometry::c_polygon_normals);
            for (Polygon_id polygon_id = 0, end = geometry.polygon_count();
                 polygon_id < end;
                 ++polygon_id)
            {
                if (!polygon_normals->has(polygon_id))
                {
                    const Polygon& polygon = geometry.polygons[polygon_id];
                    polygon.compute_normal(polygon_id, geometry, *polygon_normals, *point_locations);
                }
            }
        }
        if ((corner_normals == nullptr) && (point_normals == nullptr) && (point_normals_smooth == nullptr))
        {
            corner_normals = corner_attributes.create<vec3>(erhe::geometry::c_corner_normals);
            geometry.smooth_normalize(*corner_normals, *polygon_normals, *polygon_normals, 0.0f);
        }
    }

    if (format_info.want_normal_smooth && (point_normals_smooth == nullptr))
    {
        log_primitive_builder.trace("-computing point_normals_smooth\n");
        point_normals_smooth = point_attributes.create<vec3>(erhe::geometry::c_point_normals_smooth);
        for (Point_id point_id = 0, end = geometry.point_count();
             point_id < end;
             ++point_id)
        {
            const Point& point = geometry.points[point_id];
            vec3 normal_sum{0.0f, 0.0f, 0.0f};
            for (Point_corner_id point_corner_id = point.first_point_corner_id,
                 end = point.first_point_corner_id + point.corner_count;
                 point_corner_id < end;
                 ++point_corner_id)
            {
                Corner_id     corner_id  = geometry.point_corners[point_corner_id];
                const Corner& corner     = geometry.corners[corner_id];
                Polygon_id    polygon_id = corner.polygon_id;
                if (polygon_normals->has(polygon_id))
                {
                    normal_sum += polygon_normals->get(polygon_id);
                }
                else
                {
                    //log_primitive_builder.warn("{} - smooth normals have been requested, but polygon normals have missing polygons\n", __func__);
                    const Polygon& polygon = geometry.polygons[polygon_id];
                    glm::vec3 normal = polygon.compute_normal(geometry, *point_locations);
                    normal_sum += normal;
                }
            }
            point_normals_smooth->put(point_id, normalize(normal_sum));
        }
    }

    if (format_info.want_centroid_points && (point_locations != nullptr))
    {
        polygon_centroids = polygon_attributes.find_or_create<vec3>(erhe::geometry::c_polygon_centroids);
        for (Polygon_id polygon_id = 0, end = geometry.polygon_count();
             polygon_id < end;
             ++polygon_id)
        {
            if (!polygon_centroids->has(polygon_id))
            {
                const Polygon& polygon = geometry.polygons[polygon_id];
                polygon.compute_centroid(polygon_id, geometry, *polygon_centroids, *point_locations);
            }
        }
    }

    corner_indices = corner_attributes.create<unsigned int>(erhe::geometry::c_corner_indices);
}

void Primitive_builder::prepare_vertex_format(const Format_info& format_info,
                                              Buffer_info&       buffer_info)
{
    ZoneScoped;

    auto vf = buffer_info.vertex_format;
    if (vf)
    {
        return;
    }

    vf = std::make_shared<erhe::graphics::Vertex_format>();

    buffer_info.vertex_format = vf;

    if (format_info.want_position)
    {
        vf->make_attribute({Vertex_attribute::Usage_type::position, 0},
                            gl::Attribute_type::float_vec3,
                            {format_info.position_type, false, 3});
    }

    if (format_info.want_normal)
    {
        vf->make_attribute({Vertex_attribute::Usage_type::normal, 0},
                            gl::Attribute_type::float_vec3,
                            {format_info.normal_type, false, 3});
    }

    if (format_info.want_normal_flat)
    {
        vf->make_attribute({Vertex_attribute::Usage_type::normal, 1},
                            gl::Attribute_type::float_vec3,
                            {format_info.normal_flat_type, false, 3});
    }

    if (format_info.want_normal_smooth)
    {
        vf->make_attribute({Vertex_attribute::Usage_type::normal, 2},
                            gl::Attribute_type::float_vec3,
                            {format_info.normal_smooth_type, false, 3});
    }

    if (format_info.want_tangent)
    {
        vf->make_attribute({Vertex_attribute::Usage_type::tangent, 0},
                            gl::Attribute_type::float_vec4,
                            {format_info.tangent_type, false, 4});
    }

    if (format_info.want_bitangent)
    {
        vf->make_attribute({Vertex_attribute::Usage_type::bitangent, 0},
                            gl::Attribute_type::float_vec4,
                            {format_info.bitangent_type, false, 4});
    }

    if (format_info.want_color)
    {
        vf->make_attribute({Vertex_attribute::Usage_type::color, 0},
                            gl::Attribute_type::float_vec4,
                            {format_info.color_type, false, 4});
    }

    if (format_info.want_id)
    {
        vf->make_attribute({Vertex_attribute::Usage_type::id, 0},
                            gl::Attribute_type::float_vec3,
                            {format_info.id_vec3_type, false, 3});

        if (Configuration::info.use_integer_polygon_ids)
        {
            vf->make_attribute({Vertex_attribute::Usage_type::id, 0},
                                gl::Attribute_type::unsigned_int,
                                {gl::Vertex_attrib_type::unsigned_int, false, 1});
        }
    }

    if (format_info.want_texcoord)
    {
        vf->make_attribute({Vertex_attribute::Usage_type::tex_coord, 0},
                            gl::Attribute_type::float_vec2,
                            {gl::Vertex_attrib_type::float_, false, 2});
    }
}

void Primitive_builder::get_vertex_attributes()
{
    ZoneScoped;

    m_attributes.position      = Vertex_attribute_info(m_vertex_format, m_format_info.position_type,      3, Vertex_attribute::Usage_type::position,  0);
    m_attributes.normal        = Vertex_attribute_info(m_vertex_format, m_format_info.normal_type,        3, Vertex_attribute::Usage_type::normal,    0); // content normals
    m_attributes.normal_flat   = Vertex_attribute_info(m_vertex_format, m_format_info.normal_flat_type,   3, Vertex_attribute::Usage_type::normal,    1); // flat normals
    m_attributes.normal_smooth = Vertex_attribute_info(m_vertex_format, m_format_info.normal_smooth_type, 3, Vertex_attribute::Usage_type::normal,    2); // smooth normals
    m_attributes.tangent       = Vertex_attribute_info(m_vertex_format, m_format_info.tangent_type,       4, Vertex_attribute::Usage_type::tangent,   0);
    m_attributes.bitangent     = Vertex_attribute_info(m_vertex_format, m_format_info.bitangent_type,     4, Vertex_attribute::Usage_type::bitangent, 0);
    m_attributes.color         = Vertex_attribute_info(m_vertex_format, m_format_info.color_type,         4, Vertex_attribute::Usage_type::color,     0);
    m_attributes.texcoord      = Vertex_attribute_info(m_vertex_format, m_format_info.texcoord_type,      2, Vertex_attribute::Usage_type::tex_coord, 0);
    m_attributes.id_vec3       = Vertex_attribute_info(m_vertex_format, m_format_info.id_vec3_type,       3, Vertex_attribute::Usage_type::id,        0);
    if (Configuration::info.use_integer_polygon_ids)
    {
        m_attributes.attribute_id_uint = Vertex_attribute_info(m_vertex_format, m_format_info.id_uint_type, 1, Vertex_attribute::Usage_type::id, 0);
    }

}

void Primitive_builder::Vertex_buffer_writer::write(Vertex_attribute_info& attribute, glm::vec2 value)
{
    write_low(vertex_data.subspan(vertex_write_offset + attribute.offset,
                                  attribute.size),
              attribute.data_type,
              value);
}

void Primitive_builder::Vertex_buffer_writer::write(Vertex_attribute_info& attribute, glm::vec3 value)
{
    write_low(vertex_data.subspan(vertex_write_offset + attribute.offset,
                                  attribute.size),
              attribute.data_type,
              value);
}

void Primitive_builder::Vertex_buffer_writer::write(Vertex_attribute_info& attribute, glm::vec4 value)
{
    write_low(vertex_data.subspan(vertex_write_offset + attribute.offset,
                                  attribute.size),
              attribute.data_type,
              value);
}

void Primitive_builder::Vertex_buffer_writer::write(Vertex_attribute_info& attribute, uint32_t value)
{
    write_low(vertex_data.subspan(vertex_write_offset + attribute.offset,
                                  attribute.size),
              attribute.data_type,
              value);
}

void Primitive_builder::Vertex_buffer_writer::move(size_t relative_offset)
{
    vertex_write_offset += relative_offset;
}

void Primitive_builder::Index_buffer_writer::write_corner(uint32_t v0)
{
    //log_primitive_builder.trace("point {}\n", v0);
    write_low(corner_point_index_data.subspan(corner_point_indices_written * index_type_size, index_type_size), index_type, v0);
    ++corner_point_indices_written;
}

void Primitive_builder::Index_buffer_writer::write_triangle(uint32_t v0, uint32_t v1, uint32_t v2)
{
    //log_primitive_builder.trace("triangle {}, {}, {}\n", v0, v1, v2);
    write_low(fill_index_data.subspan((triangle_indices_written + 0) * index_type_size, index_type_size), index_type, v0);
    write_low(fill_index_data.subspan((triangle_indices_written + 1) * index_type_size, index_type_size), index_type, v1);
    write_low(fill_index_data.subspan((triangle_indices_written + 2) * index_type_size, index_type_size), index_type, v2);
    triangle_indices_written += 3;
}

void Primitive_builder::Index_buffer_writer::write_edge(uint32_t v0, uint32_t v1)
{
    //log_primitive_builder.trace("edge {}, {}\n", v0, v1);
    write_low(edge_line_index_data.subspan((edge_line_indices_written + 0) * index_type_size, index_type_size), index_type, v0);
    write_low(edge_line_index_data.subspan((edge_line_indices_written + 1) * index_type_size, index_type_size), index_type, v1);
    edge_line_indices_written += 2;
}

void Primitive_builder::Index_buffer_writer::write_centroid(uint32_t v0)
{
    //log_primitive_builder.trace("centroid {}\n", v0);
    write_low(polygon_centroid_index_data.subspan(polygon_centroid_indices_written * index_type_size, index_type_size), index_type, v0);
    ++polygon_centroid_indices_written;
}

void Primitive_builder::get_geometry_mesh_info()
{
    ZoneScoped;

    erhe::geometry::Geometry::Mesh_info& mi = m_mesh_info;

    m_geometry.info(mi);
    mi.trace(log_primitive_builder);
    m_total_vertex_count += mi.vertex_count_corners;
    if (m_format_info.want_centroid_points)
    {
        m_total_vertex_count += mi.vertex_count_centroids;
    }

    if (m_format_info.want_fill_triangles)
    {
        m_total_index_count += mi.index_count_fill_triangles;
        allocate_index_range(gl::Primitive_type::triangles,
                             mi.index_count_fill_triangles,
                             m_primitive_geometry->fill_indices);
    }

    if (m_format_info.want_edge_lines)
    {
        m_total_index_count += mi.index_count_edge_lines;
        allocate_index_range(gl::Primitive_type::lines,
                             mi.index_count_edge_lines,
                             m_primitive_geometry->edge_line_indices);
    }

    if (m_format_info.want_corner_points)
    {
        m_total_index_count += mi.index_count_corner_points;
        allocate_index_range(gl::Primitive_type::points,
                             mi.index_count_corner_points,
                             m_primitive_geometry->corner_point_indices);
    }

    if (m_format_info.want_centroid_points)
    {
        m_total_index_count += mi.index_count_centroid_points;
        allocate_index_range(gl::Primitive_type::points,
                             mi.polygon_count,
                             m_primitive_geometry->polygon_centroid_indices);
    }

    log_primitive_builder.trace("Total {} vertices, stride {}\n", m_total_vertex_count, m_vertex_stride);
}

void Primitive_builder::allocate_vertex_buffer()
{
    ZoneScoped;

    if (m_buffer_info.vertex_buffer)
    {
        // Shared vertex buffer, allocate space from that
        // TODO(tksuoran@gmail.com): If there is not enough space in the shared VBO,
        //                           allocate individual VBO as a fallback?
        m_primitive_geometry->allocate_vertex_buffer(m_buffer_info.vertex_buffer, m_total_vertex_count, m_vertex_stride);
    }
    else
    {
        // No shared vertex buffer, allocate private vertex buffer
        m_primitive_geometry->allocate_vertex_buffer(m_total_vertex_count, m_vertex_stride);
    }
    m_primitive_geometry->vertex_count = m_total_vertex_count;
}

Primitive_builder::Vertex_buffer_writer::Vertex_buffer_writer(Primitive_geometry& primitive_geometry)
    : vertex_mapping{*primitive_geometry.vertex_buffer.get(),
                     primitive_geometry.vertex_byte_offset,
                     primitive_geometry.vertex_count * primitive_geometry.vertex_element_size,
                     gl::Map_buffer_access_mask::map_write_bit |
                     gl::Map_buffer_access_mask::map_invalidate_range_bit}
    , vertex_data{vertex_mapping.span()}
{
}

void Primitive_builder::allocate_index_buffer()
{
    ZoneScoped;

    gl::Draw_elements_type index_type = m_buffer_info.index_type;
    size_t index_type_size{size_of_type(index_type)};
    bool index_range_allocated{false};

    log_primitive_builder.trace("Total {} indices, index type size {}\n", m_total_index_count, index_type_size);
    if (m_buffer_info.index_buffer)
    {
        // Shared index buffer given, allocate range from that
        m_primitive_geometry->allocate_index_buffer(m_buffer_info.index_buffer, m_total_index_count, index_type_size);
        index_range_allocated = true;
    }

    if (!index_range_allocated)
    {
        // No shared IBO given, or no room in it, allocate individual IBO
        m_primitive_geometry->allocate_index_buffer(m_total_index_count, index_type_size);
    }
}

Primitive_builder::Index_buffer_writer::Index_buffer_writer(Buffer_info&                         buffer_info,
                                                            const Format_info&                   format_info,
                                                            erhe::geometry::Geometry::Mesh_info& mesh_info,
                                                            Primitive_geometry&                  primitive_geometry)
    : index_type     {buffer_info.index_type}
    , index_type_size{size_of_type(index_type)}
    , index_mapping  {*primitive_geometry.index_buffer.get(),
                      primitive_geometry.index_byte_offset,
                      primitive_geometry.index_count * index_type_size,
                      gl::Map_buffer_access_mask::map_write_bit | gl::Map_buffer_access_mask::map_invalidate_range_bit}
    , index_data     {index_mapping.span()}
{
    ZoneScoped;

    if (format_info.want_corner_points)
    {
        corner_point_index_data = index_data.subspan(primitive_geometry.corner_point_indices.first_index * index_type_size,
                                                     mesh_info.index_count_corner_points * index_type_size);
    }
    if (format_info.want_fill_triangles)
    {
        fill_index_data = index_data.subspan(primitive_geometry.fill_indices.first_index * index_type_size,
                                             mesh_info.index_count_fill_triangles * index_type_size);
    }
    if (format_info.want_edge_lines)
    {
        edge_line_index_data = index_data.subspan(primitive_geometry.edge_line_indices.first_index * index_type_size,
                                                  mesh_info.index_count_edge_lines * index_type_size);
    }
    if (format_info.want_centroid_points)
    {
        polygon_centroid_index_data = index_data.subspan(primitive_geometry.polygon_centroid_indices.first_index * index_type_size,
                                                         mesh_info.polygon_count * index_type_size);
    }
}

void Primitive_builder::calculate_bounding_box(erhe::geometry::Property_map<erhe::geometry::Point_id, glm::vec3>* point_locations)
{
    ZoneScoped;

    m_primitive_geometry->bounding_box_min = vec3(std::numeric_limits<float>::max());
    m_primitive_geometry->bounding_box_max = vec3(std::numeric_limits<float>::lowest());
    if (m_geometry.point_count() == 0 || (point_locations == nullptr))
    {
        m_primitive_geometry->bounding_box_min = vec3{0.0f};
        m_primitive_geometry->bounding_box_max = vec3{0.0f};
        return;
    }

    for (Point_id point_id = 0, end = m_geometry.point_count(); point_id < end; ++point_id)
    {
        if (point_locations->has(point_id))
        {
            vec3 position = point_locations->get(point_id);
            m_primitive_geometry->bounding_box_min = glm::min(m_primitive_geometry->bounding_box_min, position);
            m_primitive_geometry->bounding_box_max = glm::max(m_primitive_geometry->bounding_box_max, position);
        }
    }
}

Primitive_geometry Primitive_builder::build()
{
    Primitive_geometry primitive_geometry;
    build(&primitive_geometry);
    return primitive_geometry;
}

void Primitive_builder::build(Primitive_geometry* primitive_geometry)
{
    ZoneScoped;

    m_primitive_geometry = primitive_geometry;
    log_primitive_builder.trace("Primitive_builder::build_mesh_from_geometry(usage = {}, normal_style = {}) geometry = {}\n",
                                gl::c_str(m_buffer_info.usage),
                                Primitive_geometry::desc(m_format_info.normal_style),
                                m_geometry.name());
    Log::Indenter indenter;

    m_vertex_format = m_buffer_info.vertex_format.get();
    m_vertex_stride = m_vertex_format->stride();

    Property_maps property_maps(m_geometry, m_format_info);

    get_vertex_attributes();
    get_geometry_mesh_info();
    calculate_bounding_box(property_maps.point_locations);
    allocate_vertex_buffer();
    allocate_index_buffer();
    Vertex_buffer_writer vertex_writer(*m_primitive_geometry);
    Index_buffer_writer index_writer(m_buffer_info, m_format_info, m_mesh_info, *m_primitive_geometry);

#if GENERATE_POLYGONS // polygons
    property_maps.corner_indices->clear();
    vec3 unit_y{0.0f, 1.0f, 0.0f};
    uint32_t vertex_index{0};
    uint32_t polygon_index{0};
    for (Polygon_id polygon_id = 0, end = m_geometry.polygon_count();
         polygon_id < end;
         ++polygon_id)
    {
        const Polygon& polygon = m_geometry.polygons[polygon_id];
        if (m_format_info.want_id)
        {
            if (property_maps.polygon_ids_uint32 != nullptr)
            {
                property_maps.polygon_ids_uint32->put(polygon_id, polygon_index);
            }
            property_maps.polygon_ids_vector3->put(polygon_id, erhe::toolkit::vec3_from_uint(polygon_index));
        }

        vec3 polygon_normal{0.0f, 1.0f, 0.0f};
        if (property_maps.polygon_normals != nullptr)
        {
            property_maps.polygon_normals->maybe_get(polygon_id, polygon_normal);
        }

        auto first_index    = vertex_index;
        auto previous_index = first_index;

#    if GENERATE_CORNERS // corners
        for (Polygon_corner_id polygon_corner_id = polygon.first_polygon_corner_id,
             end = polygon.first_polygon_corner_id + polygon.corner_count;
             polygon_corner_id < end;
             ++polygon_corner_id)
        {
            Corner_id     corner_id = m_geometry.polygon_corners[polygon_corner_id];
            const Corner& corner    = m_geometry.corners[corner_id];
            Point_id      point_id  = corner.point_id;

            // Position
            if (m_format_info.want_position && m_attributes.position.is_valid())
            {
                Expects(property_maps.point_locations != nullptr);
                vec3 position = property_maps.point_locations->get(point_id);
                vertex_writer.write(m_attributes.position, position);
                // log_primitive_builder.trace("polygon {} corner {} point {} vertex {} location {}, {}, {}\n",
                //                             polygon_index, corner_id, point_id, vertex_index,
                //                             position.x, position.y, position.z);
            }

            // Normal
            vec3 normal{0.0f, 1.0f, 0.0f};
            if ((property_maps.corner_normals != nullptr) && property_maps.corner_normals->has(corner_id))
            {
                normal = property_maps.corner_normals->get(corner_id);
            }
            else if ((property_maps.point_normals != nullptr) && property_maps.point_normals->has(point_id))
            {
                normal = property_maps.point_normals->get(point_id);
            }
            else if ((property_maps.point_normals_smooth != nullptr) && property_maps.point_normals_smooth->has(point_id))
            {
                normal = property_maps.point_normals_smooth->get(point_id);
            }

            vec3 point_normal{0.0f, 1.0f, 0.0f};
            if ((property_maps.point_normals != nullptr) && property_maps.point_normals->has(point_id))
            {
                point_normal = property_maps.point_normals->get(point_id);
            }
            else if ((property_maps.point_normals_smooth != nullptr) && property_maps.point_normals_smooth->has(point_id))
            {
                point_normal = property_maps.point_normals_smooth->get(point_id);
            }

            if (m_format_info.want_normal && m_attributes.normal.is_valid())
            {
                switch (m_format_info.normal_style)
                {
                    case Primitive_geometry::Normal_style::none:
                    {
                        // NOTE Was fallthrough to corner_normals
                        break;
                    }

                    case Primitive_geometry::Normal_style::corner_normals:
                    {
                        vertex_writer.write(m_attributes.normal, normal);
                        break;
                    }

                    case Primitive_geometry::Normal_style::point_normals:
                    {
                        vertex_writer.write(m_attributes.normal, point_normal);
                        break;
                    }

                    case Primitive_geometry::Normal_style::polygon_normals:
                    {
                        vertex_writer.write(m_attributes.normal, polygon_normal);
                        break;
                    }
                }
            }

            if (m_format_info.want_normal_flat && m_attributes.normal_flat.is_valid())
            {
                vertex_writer.write(m_attributes.normal_flat, polygon_normal);
            }

            if (m_format_info.want_normal_smooth && m_attributes.normal_smooth.is_valid())
            {
                vec3 smooth_point_normal{0.0f, 1.0f, 0.0f};
                if ((property_maps.point_normals_smooth != nullptr) && property_maps.point_normals_smooth->has(point_id))
                {
                    smooth_point_normal = property_maps.point_normals_smooth->get(point_id);
                }
                vertex_writer.write(m_attributes.normal_smooth, smooth_point_normal);
            }

            // Tangent
            if (m_format_info.want_tangent && m_attributes.tangent.is_valid())
            {
                vec4 tangent{1.0f, 0.0f, 0.0, 1.0f};
                if ((property_maps.corner_tangents != nullptr) && property_maps.corner_tangents->has(corner_id))
                {
                    tangent = property_maps.corner_tangents->get(corner_id);
                    //log_primitive_builder.trace("corner {} tangent {}, {}, {} for point {}\n",
                    //                            corner_id, tangent.x, tangent.y, tangent.z,
                    //                            point_id);
                }
                else if ((property_maps.point_tangents != nullptr) && property_maps.point_tangents->has(point_id))
                {
                    tangent = property_maps.point_tangents->get(point_id);
                    //log_primitive_builder.trace("point tangent {}, {}, {} for point {}\n",
                    //                            tangent.x, tangent.y, tangent.z,
                    //                            point_id);
                }
                vertex_writer.write(m_attributes.tangent, tangent);
            }

            // Bitangent
            if (m_format_info.want_bitangent && m_attributes.bitangent.is_valid())
            {
                vec4 bitangent{0.0f, 0.0f, 1.0, 1.0f};
                if ((property_maps.corner_bitangents != nullptr) && property_maps.corner_bitangents->has(corner_id))
                {
                    bitangent = property_maps.corner_bitangents->get(corner_id);
                    //log_primitive_builder.trace("corner {} bitangent {}, {}, {} for point {}\n",
                    //                            corner_id, bitangent.x, bitangent.y, bitangent.z,
                    //                            point_id);
                }
                else if ((property_maps.point_bitangents != nullptr) && property_maps.point_bitangents->has(point_id))
                {
                    bitangent = property_maps.point_bitangents->get(point_id);
                    //log_primitive_builder.trace("point bitangent {}, {}, {} for point {}\n",
                    //                            bitangent.x, bitangent.y, bitangent.z,
                    //                            point_id);
                }
                vertex_writer.write(m_attributes.bitangent, bitangent);
            }

            // Texcoord
            if (m_format_info.want_texcoord && m_attributes.texcoord.is_valid())
            {
                vec2 texcoord{0.0f, 0.0f};
                if ((property_maps.corner_texcoords != nullptr) && property_maps.corner_texcoords->has(corner_id))
                {
                    texcoord = property_maps.corner_texcoords->get(corner_id);
                }
                else if ((property_maps.point_texcoords != nullptr) && property_maps.point_texcoords->has(point_id))
                {
                    texcoord = property_maps.point_texcoords->get(point_id);
                }
                vertex_writer.write(m_attributes.texcoord, texcoord);
            }

            // Vertex Color
            if (m_format_info.want_color && m_attributes.color.is_valid())
            {
                glm::vec4 color;
                if ((property_maps.corner_colors != nullptr) && property_maps.corner_colors->has(corner_id))
                {
                    color = property_maps.corner_colors->get(corner_id);
                }
                else if ((property_maps.point_colors != nullptr) && property_maps.point_colors->has(point_id))
                {
                    color = property_maps.point_colors->get(point_id);
                }
                else
                {
                    color = m_format_info.constant_color;
                }
                vertex_writer.write(m_attributes.color, color);
            }

            // PolygonId
            if (m_format_info.want_id)
            {
                if (Configuration::info.use_integer_polygon_ids && m_attributes.attribute_id_uint.is_valid())
                {
                    vertex_writer.write(m_attributes.attribute_id_uint, polygon_index);
                }

                if (m_attributes.id_vec3.is_valid())
                {
                    vec3 v = erhe::toolkit::vec3_from_uint(polygon_index);
                    vertex_writer.write(m_attributes.id_vec3, v);
                }
            }

            // Indices
            if (m_format_info.want_corner_points)
            {
                index_writer.write_corner(vertex_index);
            }

            property_maps.corner_indices->put(corner_id, vertex_index);

            if (m_format_info.want_fill_triangles)
            {
                if (previous_index != first_index)
                {
                    index_writer.write_triangle(first_index, vertex_index, previous_index);
                }
            }

            previous_index = vertex_index;
            vertex_writer.move(m_vertex_stride);
            ++vertex_index;
        }
#    endif // GENERATE_CORNERS

        ++polygon_index;
    }
#endif // GENERATE_POLYGONS

    if (m_format_info.want_edge_lines)
    {
        for (Edge_id edge_id = 0, end = m_geometry.edge_count(); edge_id < end; ++edge_id)
        {
            const Edge&     edge              = m_geometry.edges[edge_id];
            const Point&    point_a           = m_geometry.points[edge.a];
            const Point&    point_b           = m_geometry.points[edge.b];
            Point_corner_id point_corner_id_a = point_a.first_point_corner_id;
            Point_corner_id point_corner_id_b = point_b.first_point_corner_id;
            Corner_id       corner_id_a       = m_geometry.point_corners[point_corner_id_a];
            Corner_id       corner_id_b       = m_geometry.point_corners[point_corner_id_b];

            VERIFY(edge.a != edge.b);

            if (property_maps.corner_indices->has(corner_id_a) &&
                property_maps.corner_indices->has(corner_id_b))
            {
                auto v0 = property_maps.corner_indices->get(corner_id_a);
                auto v1 = property_maps.corner_indices->get(corner_id_b);
                // log_primitive_builder.trace("edge {}: point {} corner {} vertex {} - point {} corner {} vertex {}\n",
                //                             edge_id,
                //                             edge.a, corner_id_a, v0,
                //                             edge.b, corner_id_b, v1);
                index_writer.write_edge(v0, v1);
            }
        }
    }

    if (m_format_info.want_centroid_points)
    {
        for (Polygon_id polygon_id = 0, end = m_geometry.polygon_count();
             polygon_id < end;
             ++polygon_id)
        {
            vec3 position{0.0f, 0.0f, 0.0f};
            vec3 normal = unit_y;
            if ((property_maps.point_locations != nullptr) && property_maps.point_locations->has(polygon_id))
            {
                position = property_maps.point_locations->get(polygon_id);
            }
            if ((property_maps.polygon_normals != nullptr) && property_maps.polygon_normals->has(polygon_id))
            {
                normal = property_maps.polygon_normals->get(polygon_id);
            }

            if (m_format_info.want_position && m_attributes.position.is_valid())
            {
                vertex_writer.write(m_attributes.position, position);
            }

            if (m_format_info.want_normal && m_attributes.normal.is_valid())
            {
                vertex_writer.write(m_attributes.normal, normal);
            }

            if (m_format_info.want_normal_flat && m_attributes.normal_flat.is_valid())
            {
                vertex_writer.write(m_attributes.normal_flat, normal);
            }

            index_writer.write_centroid(vertex_index);

            vertex_writer.move(m_vertex_stride);
            ++vertex_index;
        }
    }

    VERIFY(vertex_index == m_total_vertex_count);

    m_primitive_geometry = nullptr;
}

using erhe::graphics::Buffer;
using erhe::log::Log;
using std::shared_ptr;


void Primitive_builder::allocate_index_range(gl::Primitive_type primitive_type,
                                             size_t             index_count,
                                             Index_range&       range)
{
    range.primitive_type = primitive_type;
    range.first_index    = m_next_index_range_start;
    range.index_count    = index_count;
    m_next_index_range_start += index_count;

    // If index buffer has not yet been allocated, no check for enough room for index range
    VERIFY((m_primitive_geometry->index_count == 0) || (m_next_index_range_start <= m_primitive_geometry->index_count));
}


auto make_primitive(const erhe::geometry::Geometry&       geometry,
                    const Primitive_builder::Format_info& format_info,
                    Primitive_builder::Buffer_info&       buffer_info)
    -> Primitive_geometry
{
    Primitive_builder::prepare_vertex_format(format_info, buffer_info);
    Primitive_builder builder(geometry, format_info, buffer_info);
    return builder.build();
}

auto make_primitive_shared(const erhe::geometry::Geometry&       geometry,
                           const Primitive_builder::Format_info& format_info,
                           Primitive_builder::Buffer_info&       buffer_info)
    -> std::shared_ptr<Primitive_geometry>
{
    auto result = std::make_shared<Primitive_geometry>();
    Primitive_builder::prepare_vertex_format(format_info, buffer_info);
    Primitive_builder builder(geometry, format_info, buffer_info);
    builder.build(result.get());
    return result;
}

} // namespace erhe::primitive
