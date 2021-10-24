#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/primitive/buffer_sink.hpp"
#include "erhe/primitive/buffer_writer.hpp"
#include "erhe/primitive/index_range.hpp"
#include "erhe/primitive/log.hpp"
#include "erhe/primitive/primitive_geometry.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/geometry/property_map.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/configuration.hpp"
#include "erhe/graphics/vertex_attribute.hpp"
#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/graphics/state/vertex_input_state.hpp"
#include "erhe/log/log_glm.hpp"
#include "erhe/toolkit/math_util.hpp"
#include "erhe/toolkit/verify.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_precision.hpp>

#include <gsl/span>
#include <map>
#include <stdexcept>
#include <cassert>

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
using Mesh_info         = erhe::geometry::Mesh_info;
using erhe::graphics::Vertex_attribute;
using gl::size_of_type;
using erhe::log::Log;

using glm::vec2;
using glm::vec3;
using glm::vec4;
using glm::mat4;


Primitive_builder::Primitive_builder(
    const erhe::geometry::Geometry& geometry,
    Build_info&                     build_info,
    const Normal_style              normal_style
)
    : m_geometry    {geometry}
    , m_build_info  {build_info}
    , m_normal_style{normal_style}
{
}

Primitive_builder::~Primitive_builder() = default;


void Primitive_builder::prepare_vertex_format(Build_info& build_info)
{
    ZoneScoped;

    auto vf = build_info.buffer.vertex_format;
    if (vf)
    {
        return;
    }

    vf = std::make_shared<erhe::graphics::Vertex_format>();

    build_info.buffer.vertex_format = vf;

    const auto& format_info = build_info.format;
    const auto& features    = format_info.features;

    if (features.position)
    {
        vf->make_attribute(
            {Vertex_attribute::Usage_type::position, 0},
            gl::Attribute_type::float_vec3,
            {format_info.position_type, false, 3}
        );
    }

    if (features.normal)
    {
        vf->make_attribute(
            {Vertex_attribute::Usage_type::normal, 0},
            gl::Attribute_type::float_vec3,
            {format_info.normal_type, false, 3}
        );
    }

    if (features.normal_flat)
    {
        vf->make_attribute(
            {Vertex_attribute::Usage_type::normal, 1},
            gl::Attribute_type::float_vec3,
            {format_info.normal_flat_type, false, 3}
        );
    }

    if (features.normal_smooth)
    {
        vf->make_attribute(
            {Vertex_attribute::Usage_type::normal, 2},
            gl::Attribute_type::float_vec3,
            {format_info.normal_smooth_type, false, 3}
        );
    }

    if (features.tangent)
    {
        vf->make_attribute(
            {Vertex_attribute::Usage_type::tangent, 0},
            gl::Attribute_type::float_vec4,
            {format_info.tangent_type, false, 4}
        );
    }

    if (features.bitangent)
    {
        vf->make_attribute(
            {Vertex_attribute::Usage_type::bitangent, 0},
            gl::Attribute_type::float_vec4,
            {format_info.bitangent_type, false, 4}
        );
    }

    if (features.color)
    {
        vf->make_attribute(
            {Vertex_attribute::Usage_type::color, 0},
            gl::Attribute_type::float_vec4,
            {format_info.color_type, false, 4}
        );
    }

    if (features.id)
    {
        vf->make_attribute(
            {Vertex_attribute::Usage_type::id, 0},
            gl::Attribute_type::float_vec3,
            {format_info.id_vec3_type, false, 3}
        );

        if (erhe::graphics::Instance::info.use_integer_polygon_ids)
        {
            vf->make_attribute(
                {Vertex_attribute::Usage_type::id, 0},
                gl::Attribute_type::unsigned_int,
                {gl::Vertex_attrib_type::unsigned_int, false, 1}
            );
        }
    }

    if (features.texcoord)
    {
        vf->make_attribute(
            {Vertex_attribute::Usage_type::tex_coord, 0},
            gl::Attribute_type::float_vec2,
            {gl::Vertex_attrib_type::float_, false, 2}
        );
    }
}

Build_context_root::Build_context_root(
    const erhe::geometry::Geometry& geometry,
    Build_info&                     build_info,
    Primitive_geometry*             primitive_geometry
)
    : geometry          {geometry}
    , build_info        {build_info}
    , primitive_geometry{primitive_geometry}
    , vertex_format     {build_info.buffer.vertex_format.get()}
    , vertex_stride     {vertex_format->stride()}
{
    ZoneScoped;

    get_mesh_info         ();
    get_vertex_attributes ();
    allocate_vertex_buffer();
    allocate_index_buffer ();
}

void Build_context_root::get_mesh_info()
{
    Mesh_info& mi = mesh_info;

    geometry.info(mi);
    mi.trace(log_primitive_builder);

    const auto& features = build_info.format.features;

    // Count vertices
    total_vertex_count += mi.vertex_count_corners;
    if (features.centroid_points)
    {
        total_vertex_count += mi.vertex_count_centroids;
    }

    // Count indices
    if (features.fill_triangles)
    {
        total_index_count += mi.index_count_fill_triangles;
        log_primitive_builder.trace("{} triangle fill indices\n", mi.index_count_fill_triangles);
        allocate_index_range(
            gl::Primitive_type::triangles,
            mi.index_count_fill_triangles,
            primitive_geometry->triangle_fill_indices
        );
    }

    if (features.edge_lines)
    {
        total_index_count += mi.index_count_edge_lines;
        log_primitive_builder.trace("{} edge line indices\n", mi.index_count_edge_lines);
        allocate_index_range(
            gl::Primitive_type::lines,
            mi.index_count_edge_lines,
            primitive_geometry->edge_line_indices
        );
    }

    if (features.corner_points)
    {
        total_index_count += mi.index_count_corner_points;
        log_primitive_builder.trace("{} corner point indices\n", mi.index_count_corner_points);
        allocate_index_range(
            gl::Primitive_type::points,
            mi.index_count_corner_points,
            primitive_geometry->corner_point_indices
        );
    }

    if (features.centroid_points)
    {
        total_index_count += mi.index_count_centroid_points;
        log_primitive_builder.trace("{} centroid point indices\n", mi.index_count_centroid_points);
        allocate_index_range(
            gl::Primitive_type::points,
            mi.polygon_count,
            primitive_geometry->polygon_centroid_indices
        );
    }

    log_primitive_builder.trace("Total {} vertices\n", total_vertex_count);
}

void Build_context_root::get_vertex_attributes()
{
    ZoneScoped;

    const auto& format_info = build_info.format;
    attributes.position      = Vertex_attribute_info(vertex_format, format_info.position_type,      3, Vertex_attribute::Usage_type::position,  0);
    attributes.normal        = Vertex_attribute_info(vertex_format, format_info.normal_type,        3, Vertex_attribute::Usage_type::normal,    0); // content normals
    attributes.normal_flat   = Vertex_attribute_info(vertex_format, format_info.normal_flat_type,   3, Vertex_attribute::Usage_type::normal,    1); // flat normals
    attributes.normal_smooth = Vertex_attribute_info(vertex_format, format_info.normal_smooth_type, 3, Vertex_attribute::Usage_type::normal,    2); // smooth normals
    attributes.tangent       = Vertex_attribute_info(vertex_format, format_info.tangent_type,       4, Vertex_attribute::Usage_type::tangent,   0);
    attributes.bitangent     = Vertex_attribute_info(vertex_format, format_info.bitangent_type,     4, Vertex_attribute::Usage_type::bitangent, 0);
    attributes.color         = Vertex_attribute_info(vertex_format, format_info.color_type,         4, Vertex_attribute::Usage_type::color,     0);
    attributes.texcoord      = Vertex_attribute_info(vertex_format, format_info.texcoord_type,      2, Vertex_attribute::Usage_type::tex_coord, 0);
    attributes.id_vec3       = Vertex_attribute_info(vertex_format, format_info.id_vec3_type,       3, Vertex_attribute::Usage_type::id,        0);
    if (erhe::graphics::Instance::info.use_integer_polygon_ids)
    {
        attributes.attribute_id_uint = Vertex_attribute_info(vertex_format, format_info.id_uint_type, 1, Vertex_attribute::Usage_type::id, 0);
    }

}

void Build_context_root::allocate_vertex_buffer()
{
    Expects(total_vertex_count > 0);

    primitive_geometry->vertex_buffer_range = build_info.buffer.buffer_sink->allocate_vertex_buffer(total_vertex_count, vertex_stride);
}

void Build_context_root::allocate_index_buffer()
{
    ZoneScoped;

    Expects(total_index_count > 0);

    const gl::Draw_elements_type index_type = build_info.buffer.index_type;
    const size_t index_type_size{size_of_type(index_type)};

    log_primitive_builder.trace(
        "allocating index buffer "
        "total_index_count = {}, index type size = {}\n",
        total_index_count,
        index_type_size
    );

    primitive_geometry->index_buffer_range = build_info.buffer.buffer_sink->allocate_index_buffer(total_index_count, index_type_size);
}

void Build_context_root::calculate_bounding_box(
    erhe::geometry::Property_map<Point_id, vec3>* point_locations
)
{
    ZoneScoped;

    Expects(primitive_geometry != nullptr);

    primitive_geometry->bounding_box_min = vec3(std::numeric_limits<float>::max());
    primitive_geometry->bounding_box_max = vec3(std::numeric_limits<float>::lowest());
    if ((geometry.point_count() == 0) || (point_locations == nullptr))
    {
        primitive_geometry->bounding_box_min = vec3{0.0f};
        primitive_geometry->bounding_box_max = vec3{0.0f};
    }
    else
    {
        for (Point_id point_id = 0, end = geometry.point_count(); point_id < end; ++point_id)
        {
            if (point_locations->has(point_id))
            {
                const vec3 position = point_locations->get(point_id);
                primitive_geometry->bounding_box_min = glm::min(primitive_geometry->bounding_box_min, position);
                primitive_geometry->bounding_box_max = glm::max(primitive_geometry->bounding_box_max, position);
            }
        }
    }

    log_primitive_builder.trace(
        "calculated bounding box "
        "min = {}, max = {}\n",
        primitive_geometry->bounding_box_min,
        primitive_geometry->bounding_box_max
    );
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

    Expects(primitive_geometry != nullptr);

    //m_primitive_geometry = primitive_geometry;
    log_primitive_builder.trace(
        "Primitive_builder::build(usage = {}, normal_style = {}) geometry = {}\n",
        gl::c_str(m_build_info.buffer.usage),
        c_str(m_normal_style),
        m_geometry.name
    );
    erhe::log::Indenter indenter;

    Build_context build_context{
        m_geometry,
        m_build_info,
        m_normal_style,
        primitive_geometry
    };

    const auto &features = m_build_info.format.features;

    if (features.fill_triangles)
    {
        build_context.build_polygon_fill();
    }

    if (features.edge_lines)
    {
        build_context.build_edge_lines();
    }

    if (features.centroid_points)
    {
        build_context.build_centroid_points();
    }
}

Build_context::Build_context(
    const erhe::geometry::Geometry& geometry,
    Build_info&                     build_info,
    const Normal_style              normal_style,
    Primitive_geometry*             primitive_geometry
)
    : root         {geometry, build_info, primitive_geometry}
    , normal_style {normal_style}
    , vertex_writer{*this, build_info.buffer.buffer_sink}
    , index_writer {*this, build_info.buffer.buffer_sink}
    , property_maps{geometry, build_info.format}
{
    Expects(property_maps.point_locations != nullptr);

    root.calculate_bounding_box(property_maps.point_locations);
}

Build_context::~Build_context()
{
    VERIFY(vertex_index == root.total_vertex_count);
}

void Build_context::build_polygon_id()
{
    if (!root.build_info.format.features.id)
    {
        return;
    }

    if (erhe::graphics::Instance::info.use_integer_polygon_ids && root.attributes.attribute_id_uint.is_valid())
    {
        vertex_writer.write(root.attributes.attribute_id_uint, polygon_index);
    }

    if (root.attributes.id_vec3.is_valid())
    {
        const vec3 v = erhe::toolkit::vec3_from_uint(polygon_index);
        vertex_writer.write(root.attributes.id_vec3, v);
    }
}

auto Build_context::get_polygon_normal() -> glm::vec3
{
    vec3 polygon_normal{0.0f, 1.0f, 0.0f};
    if (property_maps.polygon_normals != nullptr)
    {
        property_maps.polygon_normals->maybe_get(polygon_id, polygon_normal);
    }

    return polygon_normal;
}

void Build_context::build_vertex_position()
{
    if (!root.build_info.format.features.position || !root.attributes.position.is_valid())
    {
        return;
    }

    Expects(property_maps.point_locations != nullptr);
    const vec3 position = property_maps.point_locations->get(point_id);
    vertex_writer.write(root.attributes.position, position);

    log_primitive_builder.trace(
        "polygon {} point {} corner {} vertex {} location {}\n",
        polygon_index, corner_id, point_id, vertex_index, position
    );
}

void Build_context::build_vertex_normal()
{
    const vec3 polygon_normal = get_polygon_normal();
    vec3 normal{0.0f, 1.0f, 0.0f};

    const auto& features = root.build_info.format.features;

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

    if (features.normal && root.attributes.normal.is_valid())
    {
        switch (normal_style)
        {
            case Normal_style::none:
            {
                // NOTE Was fallthrough to corner_normals
                break;
            }

            case Normal_style::corner_normals:
            {
                vertex_writer.write(root.attributes.normal, normal);
                log_primitive_builder.trace("point {} corner {} normal {}\n", point_id, corner_id, normal);
                break;
            }

            case Normal_style::point_normals:
            {
                vertex_writer.write(root.attributes.normal, point_normal);
                log_primitive_builder.trace("point {} corner {} point normal {}\n", point_id, corner_id, point_normal);
                break;
            }

            case Normal_style::polygon_normals:
            {
                vertex_writer.write(root.attributes.normal, polygon_normal);
                log_primitive_builder.trace("point {} corner {} polygon normal {}\n", point_id, corner_id, polygon_normal);
                break;
            }

            default:
            {
                FATAL("bad normal style\n");
            }
        }
    }

    if (features.normal_flat && root.attributes.normal_flat.is_valid())
    {
        vertex_writer.write(root.attributes.normal_flat, polygon_normal);
        log_primitive_builder.trace("point {} corner {} flat polygon normal {}\n", point_id, corner_id, polygon_normal);
    }

    if (features.normal_smooth && root.attributes.normal_smooth.is_valid())
    {
        vec3 smooth_point_normal{0.0f, 1.0f, 0.0f};

        if ((property_maps.point_normals_smooth != nullptr) && property_maps.point_normals_smooth->has(point_id))
        {
            smooth_point_normal = property_maps.point_normals_smooth->get(point_id);
            log_primitive_builder.trace("point {} corner {} smooth point normal {}\n", point_id, corner_id, smooth_point_normal);
        }
        else
        {
            //log_primitive_builder.warn("point {} corner {} smooth unit y normal\n", point_id, corner_id);
            used_fallback_smooth_normal = true;
        }

        vertex_writer.write(root.attributes.normal_smooth, smooth_point_normal);
    }
}

void Build_context::build_vertex_tangent()
{
    if (!root.build_info.format.features.tangent || !root.attributes.tangent.is_valid())
    {
        return;
    }

    vec4 tangent{1.0f, 0.0f, 0.0, 1.0f};
    if ((property_maps.corner_tangents != nullptr) && property_maps.corner_tangents->has(corner_id))
    {
        tangent = property_maps.corner_tangents->get(corner_id);
        log_primitive_builder.trace("point {} corner {} tangent {}\n", point_id, corner_id, tangent);
    }
    else if ((property_maps.point_tangents != nullptr) && property_maps.point_tangents->has(point_id))
    {
        tangent = property_maps.point_tangents->get(point_id);
        log_primitive_builder.trace("point {} corner {} point tangent {}\n", point_id, corner_id, tangent);
    }
    else
    {
        //log_primitive_builder.warn("point_id {} corner {} unit x tangent\n", point_id, corner_id);
        used_fallback_tangent = true;
    }

    vertex_writer.write(root.attributes.tangent, tangent);
}

void Build_context::build_vertex_bitangent()
{
    if (!root.build_info.format.features.bitangent || !root.attributes.bitangent.is_valid())
    {
        return;
    }

    vec4 bitangent{0.0f, 0.0f, 1.0, 1.0f};
    if ((property_maps.corner_bitangents != nullptr) && property_maps.corner_bitangents->has(corner_id))
    {
        bitangent = property_maps.corner_bitangents->get(corner_id);
        log_primitive_builder.trace("point {} corner {} bitangent {}\n", point_id, corner_id, bitangent);
    }
    else if ((property_maps.point_bitangents != nullptr) && property_maps.point_bitangents->has(point_id))
    {
        bitangent = property_maps.point_bitangents->get(point_id);
        log_primitive_builder.trace("point {} corner {} point bitangent {}\n", point_id, corner_id, bitangent);
    }
    else
    {
        //log_primitive_builder.warn("point {} corner {} unit z bitangent\n", point_id, corner_id);
        used_fallback_bitangent = true;
    }

    vertex_writer.write(root.attributes.bitangent, bitangent);
}

void Build_context::build_vertex_texcoord()
{
    if (!root.build_info.format.features.texcoord || !root.attributes.texcoord.is_valid())
    {
        return;
    }

    vec2 texcoord{0.0f, 0.0f};
    if ((property_maps.corner_texcoords != nullptr) && property_maps.corner_texcoords->has(corner_id))
    {
        texcoord = property_maps.corner_texcoords->get(corner_id);
        log_primitive_builder.trace("point {} corner {} texcoord {}\n", point_id, corner_id, texcoord);
    }
    else if ((property_maps.point_texcoords != nullptr) && property_maps.point_texcoords->has(point_id))
    {
        texcoord = property_maps.point_texcoords->get(point_id);
        log_primitive_builder.trace("point {} corner {} point texcoord {}\n", point_id, corner_id, texcoord);
    }
    else
    {
        //log_primitive_builder.warn("point {} corner {} origo texcoord\n", point_id, corner_id);
        used_fallback_texcoord = true;
    }

    vertex_writer.write(root.attributes.texcoord, texcoord);
}

void Build_context::build_vertex_color()
{
    if (!root.build_info.format.features.color || !root.attributes.color.is_valid())
    {
        return;
    }

    vec4 color;
    if ((property_maps.corner_colors != nullptr) && property_maps.corner_colors->has(corner_id))
    {
        color = property_maps.corner_colors->get(corner_id);
        log_primitive_builder.trace("point {} corner {} corner color {}\n", point_id, corner_id, color);
    }
    else if ((property_maps.point_colors != nullptr) && property_maps.point_colors->has(point_id))
    {
        color = property_maps.point_colors->get(point_id);
        log_primitive_builder.trace("point {} corner {} point color {}\n", point_id, corner_id, color);
    }
    else
    {
        color = root.build_info.format.constant_color;
        log_primitive_builder.trace("point {} corner {} constant color {}\n", point_id, corner_id, color);
    }

    vertex_writer.write(root.attributes.color, color);
}

void Build_context::build_centroid_position()
{
    if (!root.build_info.format.features.centroid_points || !root.attributes.position.is_valid())
    {
        return;
    }

    vec3 position{0.0f, 0.0f, 0.0f};
    if ((property_maps.point_locations != nullptr) && property_maps.point_locations->has(polygon_id))
    {
        position = property_maps.point_locations->get(polygon_id);
    }

    vertex_writer.write(root.attributes.position, position);
}

void Build_context::build_centroid_normal()
{
    const auto& features = root.build_info.format.features;

    if (!features.centroid_points)
    {
        return;
    }

    vec3 normal{0.0f, 1.0f, 0.0f};
    if ((property_maps.polygon_normals != nullptr) && property_maps.polygon_normals->has(polygon_id))
    {
        normal = property_maps.polygon_normals->get(polygon_id);
    }

    if (features.normal && root.attributes.normal.is_valid())
    {
        vertex_writer.write(root.attributes.normal, normal);
    }

    if (features.normal_flat && root.attributes.normal_flat.is_valid())
    {
        vertex_writer.write(root.attributes.normal_flat, normal);
    }
}

void Build_context::build_corner_point_index()
{
    if (root.build_info.format.features.corner_points)
    {
        index_writer.write_corner(vertex_index);
    }
}

void Build_context::build_triangle_fill_index()
{
    if (root.build_info.format.features.fill_triangles)
    {
        if (previous_index != first_index)
        {
            index_writer.write_triangle(first_index, vertex_index, previous_index);
        }
    }

    previous_index = vertex_index;
}

void Build_context::build_polygon_fill()
{
    // TODO property_maps.corner_indices needs to be setup
    //      also if edge lines are wanted.
    //

    property_maps.corner_indices->clear();

    vertex_index  = 0;
    polygon_index = 0;

    const Polygon_id polygon_id_end = root.geometry.polygon_count();
    for (polygon_id = 0; polygon_id < polygon_id_end; ++polygon_id)
    {
        const Polygon& polygon = root.geometry.polygons[polygon_id];
        first_index    = vertex_index;
        previous_index = first_index;

        if (property_maps.polygon_ids_uint32 != nullptr)
        {
            property_maps.polygon_ids_uint32->put(polygon_id, polygon_index);
        }

        if (property_maps.polygon_ids_vector3 != nullptr)
        {
            property_maps.polygon_ids_vector3->put(polygon_id, erhe::toolkit::vec3_from_uint(polygon_index));
        }

        const Polygon_corner_id polyon_corner_id_end = polygon.first_polygon_corner_id + polygon.corner_count;
        for (polygon_corner_id = polygon.first_polygon_corner_id;
             polygon_corner_id < polyon_corner_id_end;
             ++polygon_corner_id)
        {
            corner_id            = root.geometry.polygon_corners[polygon_corner_id];
            const Corner& corner = root.geometry.corners[corner_id];
            point_id             = corner.point_id;

            build_polygon_id      ();
            build_vertex_position ();
            build_vertex_normal   ();
            build_vertex_tangent  ();
            build_vertex_bitangent();
            build_vertex_texcoord ();
            build_vertex_color    ();

            // Indices
            property_maps.corner_indices->put(corner_id, vertex_index);

            build_corner_point_index();
            build_triangle_fill_index();

            vertex_writer.move(root.vertex_stride);
            ++vertex_index;
        }

        ++polygon_index;
    }

    if (used_fallback_smooth_normal)
    {
        log_primitive_builder.warn("Warning: Used fallback smooth normal\n");
    }
    if (used_fallback_tangent)
    {
        log_primitive_builder.warn("Warning: Used fallback tangent\n");
    }
    if (used_fallback_bitangent)
    {
        log_primitive_builder.warn("Warning: Used fallback bitangent\n");
    }
    if (used_fallback_texcoord)
    {
        log_primitive_builder.warn("Warning: Used fallback texcoord\n");
    }
}

void Build_context::build_edge_lines()
{
    if (!root.build_info.format.features.edge_lines)
    {
        return;
    }

    for (Edge_id edge_id = 0, end = root.geometry.edge_count(); edge_id < end; ++edge_id)
    {
        const Edge&           edge              = root.geometry.edges[edge_id];
        const Point&          point_a           = root.geometry.points[edge.a];
        const Point&          point_b           = root.geometry.points[edge.b];
        const Point_corner_id point_corner_id_a = point_a.first_point_corner_id;
        const Point_corner_id point_corner_id_b = point_b.first_point_corner_id;
        const Corner_id       corner_id_a       = root.geometry.point_corners[point_corner_id_a];
        const Corner_id       corner_id_b       = root.geometry.point_corners[point_corner_id_b];

        VERIFY(edge.a != edge.b);

        if (property_maps.corner_indices->has(corner_id_a) &&
            property_maps.corner_indices->has(corner_id_b))
        {
            const auto v0 = property_maps.corner_indices->get(corner_id_a);
            const auto v1 = property_maps.corner_indices->get(corner_id_b);
            log_primitive_builder.trace(
                "edge {} point {} corner {} vertex {} - point {} corner {} vertex {}\n",
                edge_id,
                edge.a, corner_id_a, v0,
                edge.b, corner_id_b, v1
            );
            index_writer.write_edge(v0, v1);
        }
    }
}

void Build_context::build_centroid_points()
{
    if (!root.build_info.format.features.centroid_points)
    {
        return;
    }

    const Polygon_id polygon_id_end = root.geometry.polygon_count();
    for (polygon_id = 0;
         polygon_id < polygon_id_end;
         ++polygon_id)
    {
        build_centroid_position();
        build_centroid_normal();

        index_writer.write_centroid(vertex_index);
        vertex_writer.move(root.vertex_stride);
        ++vertex_index;
    }
}

void Build_context_root::allocate_index_range(
    const gl::Primitive_type primitive_type,
    const size_t             index_count,
    Index_range&             range
)
{
    range.primitive_type = primitive_type;
    range.first_index    = next_index_range_start;
    range.index_count    = index_count;
    next_index_range_start += index_count;

    // If index buffer has not yet been allocated, no check for enough room for index range
    VERIFY(
        (primitive_geometry->index_buffer_range.count == 0) ||
        (next_index_range_start <= primitive_geometry->index_buffer_range.count));
}


auto make_primitive(
    const erhe::geometry::Geometry& geometry,
    Build_info&                     build_info,
    const Normal_style              normal_style)
-> Primitive_geometry
{
    //Primitive_builder::prepare_vertex_format(context.format_info, context.buffer_info);
    Primitive_builder builder{geometry, build_info, normal_style};
    return builder.build();
}

auto make_primitive_shared(
    const erhe::geometry::Geometry& geometry,
    Build_info&                     build_info,
    const Normal_style              normal_style
)
-> std::shared_ptr<Primitive_geometry>
{
    auto result = std::make_shared<Primitive_geometry>();
    Primitive_builder builder{geometry, build_info, normal_style};
    builder.build(result.get());
    result->source_normal_style = build_info.format.normal_style;
    return result;
}

} // namespace erhe::primitive
