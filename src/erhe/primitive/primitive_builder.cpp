// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/primitive/buffer_sink.hpp"
#include "erhe/primitive/buffer_writer.hpp"
#include "erhe/primitive/index_range.hpp"
#include "erhe/primitive/primitive_log.hpp"
#include "erhe/primitive/primitive_geometry.hpp"
#include "erhe/geometry/geometry.hpp"
#include "erhe/geometry/property_map.hpp"
#include "erhe/gl/enum_string_functions.hpp"
#include "erhe/gl/gl_helpers.hpp"
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

#include <cassert>
#include <map>
#include <stdexcept>

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
using gl_helpers::size_of_type;

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

Primitive_builder::~Primitive_builder() noexcept
{
}

void Primitive_builder::prepare_vertex_format(Build_info& build_info)
{
    ERHE_PROFILE_FUNCTION

    if (build_info.buffer.vertex_format)
    {
        return;
    }

    auto vf = std::make_shared<erhe::graphics::Vertex_format>();

    build_info.buffer.vertex_format = vf;

    const auto& format_info = build_info.format;
    const auto& features    = format_info.features;

    if (features.position)
    {
        vf->add_attribute(
            erhe::graphics::Vertex_attribute{
                .usage       = { Vertex_attribute::Usage_type::position },
                .shader_type = gl::Attribute_type::float_vec3,
                .data_type   = {
                    .type      = format_info.position_type,
                    .dimension = 3
                }
            }
        );
    }

    if (features.normal)
    {
        vf->add_attribute(
            erhe::graphics::Vertex_attribute{
                .usage = {
                    .type      = Vertex_attribute::Usage_type::normal
                },
                .shader_type   = gl::Attribute_type::float_vec3,
                .data_type = {
                    .type      = format_info.normal_type,
                    .dimension = 3
                }
            }
        );
    }

    if (features.normal_flat)
    {
        vf->add_attribute(
            erhe::graphics::Vertex_attribute{
                .usage = {
                    .type      = Vertex_attribute::Usage_type::normal,
                    .index     = 1
                },
                .shader_type   = gl::Attribute_type::float_vec3,
                .data_type = {
                    .type      = format_info.normal_flat_type,
                    .dimension = 3
                }
            }
        );
    }

    if (features.normal_smooth)
    {
        vf->add_attribute(
            erhe::graphics::Vertex_attribute{
                .usage = {
                    .type      = Vertex_attribute::Usage_type::normal,
                    .index     = 2
                },
                .shader_type   = gl::Attribute_type::float_vec3,
                .data_type = {
                    .type      = format_info.normal_smooth_type,
                    .dimension = 3
                }
            }
        );
    }

    if (features.tangent)
    {
        vf->add_attribute(
            erhe::graphics::Vertex_attribute{
                .usage = {
                    .type      = Vertex_attribute::Usage_type::tangent
                },
                .shader_type   = gl::Attribute_type::float_vec4,
                .data_type = {
                    .type      = format_info.tangent_type,
                    .dimension = 4
                }
            }
        );
    }

    if (features.bitangent)
    {
        vf->add_attribute(
            erhe::graphics::Vertex_attribute{
                .usage = {
                    .type      = Vertex_attribute::Usage_type::bitangent
                },
                .shader_type   = gl::Attribute_type::float_vec4,
                .data_type = {
                    .type      = format_info.bitangent_type,
                    .dimension = 4
                }
            }
        );
    }

    if (features.color)
    {
        vf->add_attribute(
            erhe::graphics::Vertex_attribute{
                .usage = {
                    .type      = Vertex_attribute::Usage_type::color
                },
                .shader_type   = gl::Attribute_type::float_vec4,
                .data_type = {
                    .type      = format_info.color_type,
                    .dimension = 4
                }
            }
        );
    }

    if (features.id)
    {
        vf->add_attribute(
            erhe::graphics::Vertex_attribute{
                .usage = {
                    .type      = Vertex_attribute::Usage_type::id
                },
                .shader_type   = gl::Attribute_type::float_vec3,
                .data_type = {
                    .type      = format_info.id_vec3_type,
                    .dimension = 3
                }
            }
        );

        if (erhe::graphics::Instance::info.use_integer_polygon_ids)
        {
            vf->add_attribute(
                erhe::graphics::Vertex_attribute{
                    .usage = {
                        .type      = Vertex_attribute::Usage_type::id
                    },
                    .shader_type   = gl::Attribute_type::unsigned_int,
                    .data_type = {
                        .type      = gl::Vertex_attrib_type::unsigned_int,
                        .dimension = 1
                    }
                }
            );
        }
    }

    if (features.texcoord)
    {
        vf->add_attribute(
            erhe::graphics::Vertex_attribute{
                .usage = {
                    .type      = Vertex_attribute::Usage_type::tex_coord
                },
                .shader_type   = gl::Attribute_type::float_vec2,
                .data_type = {
                    .type      = gl::Vertex_attrib_type::float_,
                    .dimension = 2
                }
            }
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
    ERHE_PROFILE_FUNCTION

    get_mesh_info         ();
    get_vertex_attributes ();
    allocate_vertex_buffer();
    allocate_index_buffer ();
}

void Build_context_root::get_mesh_info()
{
    mesh_info = geometry.get_mesh_info();

    Mesh_info& mi = mesh_info;
    mi.trace(log_primitive_builder);

    const auto& features = build_info.format.features;

    // Count vertices
    total_vertex_count = 0;
    total_vertex_count += mi.vertex_count_corners;
    if (features.centroid_points)
    {
        SPDLOG_LOGGER_INFO(log_primitive_builder, "{} centroid point indices", mi.vertex_count_centroids);
        total_vertex_count += mi.vertex_count_centroids;
    }

    // Count indices
    if (features.fill_triangles)
    {
        SPDLOG_LOGGER_INFO(log_primitive_builder, "{} triangle fill indices", mi.index_count_fill_triangles);
        total_index_count += mi.index_count_fill_triangles;
        allocate_index_range(
            gl::Primitive_type::triangles,
            mi.index_count_fill_triangles,
            primitive_geometry->triangle_fill_indices
        );
        const std::size_t primitive_count = mi.index_count_fill_triangles;
        primitive_geometry->primitive_id_to_polygon_id.resize(primitive_count);
    }

    if (features.edge_lines)
    {
        SPDLOG_LOGGER_INFO(log_primitive_builder, "{} edge line indices", mi.index_count_edge_lines);
        total_index_count += mi.index_count_edge_lines;
        allocate_index_range(
            gl::Primitive_type::lines,
            mi.index_count_edge_lines,
            primitive_geometry->edge_line_indices
        );
    }

    if (features.corner_points)
    {
        SPDLOG_LOGGER_INFO(log_primitive_builder, "{} corner point indices", mi.index_count_corner_points);
        total_index_count += mi.index_count_corner_points;
        allocate_index_range(
            gl::Primitive_type::points,
            mi.index_count_corner_points,
            primitive_geometry->corner_point_indices
        );
    }

    if (features.centroid_points)
    {
        SPDLOG_LOGGER_INFO(log_primitive_builder, "{} centroid point indices", mi.index_count_centroid_points);
        total_index_count += mi.index_count_centroid_points;
        allocate_index_range(
            gl::Primitive_type::points,
            mi.polygon_count,
            primitive_geometry->polygon_centroid_indices
        );
    }

    SPDLOG_LOGGER_INFO(log_primitive_builder, "Total {} vertices", total_vertex_count);
}

void Build_context_root::get_vertex_attributes()
{
    ERHE_PROFILE_FUNCTION

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
    ERHE_PROFILE_FUNCTION

    Expects(total_index_count > 0);

    const gl::Draw_elements_type index_type     {build_info.buffer.index_type};
    const std::size_t            index_type_size{size_of_type(index_type)};

    log_primitive_builder->trace(
        "allocating index buffer "
        "total_index_count = {}, index type size = {}",
        total_index_count,
        index_type_size
    );

    primitive_geometry->index_buffer_range = build_info.buffer.buffer_sink->allocate_index_buffer(
        total_index_count,
        index_type_size
    );
}

class Geometry_point_source
    : public erhe::toolkit::Bounding_volume_source
{
public:
    Geometry_point_source(
        const erhe::geometry::Geometry&               geometry,
        erhe::geometry::Property_map<Point_id, vec3>* point_locations
    )
        : m_geometry       {geometry}
        , m_point_locations{point_locations}
    {
    }

    auto get_element_count() const -> std::size_t override
    {
        if (m_point_locations == nullptr)
        {
            return 0;
        }
        return m_geometry.get_point_count();
    }

    auto get_element_point_count(const std::size_t element_index) const -> std::size_t override
    {
        static_cast<void>(element_index);
        return 1;
    }

    auto get_point(
        const std::size_t element_index,
        const std::size_t point_index
    ) const -> std::optional<glm::vec3> override
    {
        static_cast<void>(point_index);
        const auto point_id = static_cast<erhe::geometry::Point_id>(element_index);
        if (m_point_locations->has(point_id))
        {
            return m_point_locations->get(point_id);
        }
        return {};
    }

private:
    const erhe::geometry::Geometry&               m_geometry;
    erhe::geometry::Property_map<Point_id, vec3>* m_point_locations{nullptr};
};

void Build_context_root::calculate_bounding_volume(
    erhe::geometry::Property_map<Point_id, vec3>* point_locations
)
{
    ERHE_PROFILE_FUNCTION

    Expects(primitive_geometry != nullptr);

    const Geometry_point_source point_source{geometry, point_locations};

    erhe::toolkit::calculate_bounding_volume(
        point_source,
        primitive_geometry->bounding_box,
        primitive_geometry->bounding_sphere
    );
}

auto Primitive_builder::build() -> Primitive_geometry
{
    Primitive_geometry primitive_geometry;
    build(&primitive_geometry);
    return primitive_geometry;
}

void Primitive_builder::build(Primitive_geometry* primitive_geometry)
{
    ERHE_PROFILE_FUNCTION

    Expects(primitive_geometry != nullptr);

    //m_primitive_geometry = primitive_geometry;
    SPDLOG_LOGGER_INFO(
        log_primitive_builder,
        "Primitive_builder::build(usage = {}, normal_style = {}) geometry = {}",
        gl::c_str(m_build_info.buffer.usage),
        c_str(m_normal_style),
        m_geometry.name
    );
    //const erhe::log::Indenter indenter;

    Build_context build_context{
        m_geometry,
        m_build_info,
        m_normal_style,
        primitive_geometry
    };

    const auto& features = m_build_info.format.features;

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

    root.calculate_bounding_volume(property_maps.point_locations);
}

Build_context::~Build_context() noexcept
{
    ERHE_VERIFY(vertex_index == root.total_vertex_count);
}

void Build_context::build_polygon_id()
{
    ERHE_PROFILE_FUNCTION

    if (!root.build_info.format.features.id)
    {
        return;
    }

    if (
        erhe::graphics::Instance::info.use_integer_polygon_ids &&
        root.attributes.attribute_id_uint.is_valid()
    )
    {
        vertex_writer.write(root.attributes.attribute_id_uint, polygon_index);
    }

    if (root.attributes.id_vec3.is_valid())
    {
        const vec3 v = erhe::toolkit::vec3_from_uint(polygon_index);
        vertex_writer.write(root.attributes.id_vec3, v);
    }
}

auto Build_context::get_polygon_normal() -> vec3
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
    ERHE_PROFILE_FUNCTION

    if (
        !root.build_info.format.features.position ||
        !root.attributes.position.is_valid()
    )
    {
        return;
    }

    Expects(property_maps.point_locations != nullptr);
    const vec3 position = property_maps.point_locations->get(point_id);
    vertex_writer.write(root.attributes.position, position);

    SPDLOG_LOGGER_TRACE(
        log_primitive_builder,
        "polygon {} corner {} point {} vertex {} location {}",
        polygon_index, corner_id, point_id, vertex_index, position
    );
}

void Build_context::build_vertex_normal()
{
    ERHE_PROFILE_FUNCTION

    const vec3 polygon_normal = get_polygon_normal();
    vec3 normal{0.0f, 1.0f, 0.0f};

    const auto& features = root.build_info.format.features;

    vec3 point_normal{0.0f, 1.0f, 0.0f};
    bool found_point_normal{false};
    if (property_maps.point_normals != nullptr)
    {
        found_point_normal = property_maps.point_normals->maybe_get(point_id, point_normal);
    }
    if (!found_point_normal && (property_maps.point_normals_smooth != nullptr))
    {
        found_point_normal = property_maps.point_normals_smooth->maybe_get(point_id, point_normal);
    }

    bool found_normal{false};
    if (property_maps.corner_normals != nullptr)
    {
        found_normal = property_maps.corner_normals->maybe_get(corner_id, normal);
    }
    if (!found_normal)
    {
        normal = point_normal;
    }

    if (features.normal && root.attributes.normal.is_valid())
    {
        switch (normal_style)
        {
            //using enum Normal_style;
            case Normal_style::none:
            {
                // NOTE Was fallthrough to corner_normals
                break;
            }

            case Normal_style::corner_normals:
            {
                vertex_writer.write(root.attributes.normal, normal);
                SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} normal {}", point_id, corner_id, normal);
                break;
            }

            case Normal_style::point_normals:
            {
                vertex_writer.write(root.attributes.normal, point_normal);
                SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} point normal {}", point_id, corner_id, point_normal);
                break;
            }

            case Normal_style::polygon_normals:
            {
                vertex_writer.write(root.attributes.normal, polygon_normal);
                SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} polygon normal {}", point_id, corner_id, polygon_normal);
                break;
            }

            default:
            {
                ERHE_FATAL("bad normal style");
            }
        }
    }

    if (features.normal_flat && root.attributes.normal_flat.is_valid())
    {
        vertex_writer.write(root.attributes.normal_flat, polygon_normal);
        SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} flat polygon normal {}", point_id, corner_id, polygon_normal);
    }

    if (features.normal_smooth && root.attributes.normal_smooth.is_valid())
    {
        vec3 smooth_point_normal{0.0f, 1.0f, 0.0f};

        if ((property_maps.point_normals_smooth != nullptr) && property_maps.point_normals_smooth->has(point_id))
        {
            smooth_point_normal = property_maps.point_normals_smooth->get(point_id);
            SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} smooth point normal {}", point_id, corner_id, smooth_point_normal);
        }
        else
        {
            SPDLOG_LOGGER_WARN(log_primitive_builder, "point {} corner {} smooth unit y normal", point_id, corner_id);
            used_fallback_smooth_normal = true;
        }

        vertex_writer.write(root.attributes.normal_smooth, smooth_point_normal);
    }
}

void Build_context::build_vertex_tangent()
{
    ERHE_PROFILE_FUNCTION

    if (!root.build_info.format.features.tangent || !root.attributes.tangent.is_valid())
    {
        return;
    }

    vec4 tangent{1.0f, 0.0f, 0.0, 1.0f};
    bool found{false};
    if (property_maps.corner_tangents != nullptr)
    {
        found = property_maps.corner_tangents->maybe_get(corner_id, tangent);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        if (found)
        {
            SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} tangent {}", point_id, corner_id, tangent);
        }
#endif
    }
    if (!found && (property_maps.point_tangents != nullptr))
    {
        found = property_maps.point_tangents->maybe_get(point_id, tangent);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        if (found)
        {
            SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} point tangent {}", point_id, corner_id, tangent);
        }
#endif
    }
    if (!found)
    {
        SPDLOG_LOGGER_WARN(log_primitive_builder, "point_id {} corner {} unit x tangent", point_id, corner_id);
        used_fallback_tangent = true;
    }

    vertex_writer.write(root.attributes.tangent, tangent);
}

void Build_context::build_vertex_bitangent()
{
    ERHE_PROFILE_FUNCTION

    if (!root.build_info.format.features.bitangent || !root.attributes.bitangent.is_valid())
    {
        return;
    }

    vec4 bitangent{0.0f, 0.0f, 1.0, 1.0f};
    bool found{false};
    if (property_maps.corner_bitangents != nullptr)
    {
        found = property_maps.corner_bitangents->maybe_get(corner_id, bitangent);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        if (found)
        {
            SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} bitangent {}", point_id, corner_id, bitangent);
        }
#endif
    }
    if (!found && (property_maps.point_bitangents != nullptr))
    {
        found = property_maps.point_bitangents->maybe_get(point_id, bitangent);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        if (found)
        {
            SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} point bitangent {}", point_id, corner_id, bitangent);
        }
#endif
    }
    if (!found)
    {
        SPDLOG_LOGGER_WARN(log_primitive_builder, "point {} corner {} unit z bitangent", point_id, corner_id);
        used_fallback_bitangent = true;
    }

    vertex_writer.write(root.attributes.bitangent, bitangent);
}

void Build_context::build_vertex_texcoord()
{
    ERHE_PROFILE_FUNCTION

    if (!root.build_info.format.features.texcoord || !root.attributes.texcoord.is_valid())
    {
        return;
    }

    vec2 texcoord{0.0f, 0.0f};
    bool found{false};
    if (property_maps.corner_texcoords != nullptr)
    {
        found = property_maps.corner_texcoords->maybe_get(corner_id, texcoord);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        if (found)
        {
            SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} texcoord {}", point_id, corner_id, texcoord);
        }
#endif
    }
    if (!found && (property_maps.point_texcoords != nullptr))
    {
        found = property_maps.point_texcoords->maybe_get(point_id, texcoord);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        if (found)
        {
            SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} point texcoord {}", point_id, corner_id, texcoord);
        }
#endif
    }
    if (!found)
    {
        SPDLOG_LOGGER_WARN(log_primitive_builder, "point {} corner {} default texcoord", point_id, corner_id);
        used_fallback_texcoord = true;
    }

    vertex_writer.write(root.attributes.texcoord, texcoord);
}

// namespace {
//
// constexpr glm::vec3 unique_colors[13] =
// {
//     {1.0f, 0.3f, 0.7f}, //  3
//     {1.0f, 0.7f, 0.3f}, //  4
//     {0.7f, 1.0f, 0.3f}, //  5
//     {0.3f, 1.0f, 0.7f}, //  6
//     {0.3f, 0.7f, 1.0f}, //  7
//     {0.7f, 0.3f, 1.0f}, //  8
//     {0.8f, 0.0f, 0.0f}, //  9
//     {0.0f, 0.8f, 0.0f}, // 14
//     {0.0f, 0.0f, 0.8f}, // 15
//     {0.7f, 0.7f, 0.0f}, // 12
//     {0.7f, 0.0f, 0.7f}, // 13
//     {0.0f, 0.7f, 0.7f}, // 10
//     {0.7f, 0.7f, 0.7f}  // 11
// };
//
// }

void Build_context::build_vertex_color(const uint32_t /*polygon_corner_count*/)
{
    ERHE_PROFILE_FUNCTION

    if (!root.build_info.format.features.color || !root.attributes.color.is_valid())
    {
        return;
    }

    vec4 color{root.build_info.format.constant_color};
    bool found{false};
    if (property_maps.corner_colors != nullptr)
    {
        found = property_maps.corner_colors->maybe_get(corner_id, color);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        if (found)
        {
            SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} corner color {}", point_id, corner_id, color);
        }
#endif
    }
    if (!found && (property_maps.point_colors != nullptr))
    {
        found = property_maps.point_colors->maybe_get(point_id, color);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        if (found)
        {
            SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} point color {}", point_id, corner_id, color);
        }
#endif
    }
    if (!found && (property_maps.polygon_colors != nullptr))
    {
        found = property_maps.polygon_colors->maybe_get(polygon_id, color);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        if (found)
        {
            SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} polygon {} polygon color {}", point_id, corner_id, polygon_id, color);
        }
#else
        static_cast<void>(found);
#endif
    }
    //if (!found)
    //{
    //    color = root.build_info.format.constant_color;
    //    //trace_fmt(log_primitive_builder, "point {} corner {} constant color {}\n", point_id, corner_id, color);
    //    //if (polygon_corner_count > 3)
    //    //{
    //    //    color = glm::vec4{unique_colors[(polygon_corner_count - 3) % 13], 1.0f};
    //    //}
    //}

    vertex_writer.write(root.attributes.color, color);
}

void Build_context::build_centroid_position()
{
    if (!root.build_info.format.features.centroid_points || !root.attributes.position.is_valid())
    {
        return;
    }

    vec3 position{0.0f, 0.0f, 0.0f};
    if ((property_maps.polygon_centroids != nullptr) && property_maps.polygon_centroids->has(polygon_id))
    {
        position = property_maps.polygon_centroids->get(polygon_id);
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
    if (property_maps.polygon_normals != nullptr)
    {
        property_maps.polygon_normals->maybe_get(polygon_id, normal);
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
            root.primitive_geometry->primitive_id_to_polygon_id[primitive_index] = polygon_id;
            ++primitive_index;
        }
    }

    previous_index = vertex_index;
}

void Build_context::build_polygon_fill()
{
    ERHE_PROFILE_FUNCTION

    // TODO property_maps.corner_indices needs to be setup
    //      also if edge lines are wanted.

    property_maps.corner_indices->clear();

    vertex_index  = 0;
    polygon_index = 0;

    const bool any_normal_feature = root.build_info.format.features.normal =
        root.build_info.format.features.normal      ||
        root.build_info.format.features.normal_flat ||
        root.build_info.format.features.normal_smooth;

    const Polygon_id polygon_id_end = root.geometry.get_polygon_count();
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
        for (
            polygon_corner_id = polygon.first_polygon_corner_id;
            polygon_corner_id < polyon_corner_id_end;
            ++polygon_corner_id
        )
        {
            corner_id            = root.geometry.polygon_corners[polygon_corner_id];
            const Corner& corner = root.geometry.corners[corner_id];
            point_id             = corner.point_id;
            //const Point& point   = root.geometry.points[point_id];

            build_polygon_id      ();
            build_vertex_position ();
            if (any_normal_feature)
            {
                build_vertex_normal();
            }
            build_vertex_tangent  ();
            build_vertex_bitangent();
            build_vertex_texcoord ();
            build_vertex_color    (polygon.corner_count);

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
        log_primitive_builder->warn("Warning: Used fallback smooth normal");
    }
    if (used_fallback_tangent)
    {
        log_primitive_builder->warn("Warning: Used fallback tangent");
    }
    if (used_fallback_bitangent)
    {
        log_primitive_builder->warn("Warning: Used fallback bitangent");
    }
    if (used_fallback_texcoord)
    {
        log_primitive_builder->warn("Warning: Used fallback texcoord");
    }
}

void Build_context::build_edge_lines()
{
    ERHE_PROFILE_FUNCTION

    if (!root.build_info.format.features.edge_lines)
    {
        return;
    }

    for (
        Edge_id edge_id = 0, end = root.geometry.get_edge_count();
        edge_id < end;
        ++edge_id
    )
    {
        const Edge&           edge              = root.geometry.edges[edge_id];
        const Point&          point_a           = root.geometry.points[edge.a];
        const Point&          point_b           = root.geometry.points[edge.b];
        const Point_corner_id point_corner_id_a = point_a.first_point_corner_id;
        const Point_corner_id point_corner_id_b = point_b.first_point_corner_id;
        const Corner_id       corner_id_a       = root.geometry.point_corners[point_corner_id_a];
        const Corner_id       corner_id_b       = root.geometry.point_corners[point_corner_id_b];

        ERHE_VERIFY(edge.a != edge.b);

        if (
            property_maps.corner_indices->has(corner_id_a) &&
            property_maps.corner_indices->has(corner_id_b)
        )
        {
            const auto v0 = property_maps.corner_indices->get(corner_id_a);
            const auto v1 = property_maps.corner_indices->get(corner_id_b);
            //SPDLOG_LOGGER_TRACE(
            //    log_primitive_builder,
            //    "edge {} point {} corner {} vertex {} - point {} corner {} vertex {}",
            //    edge_id,
            //    edge.a, corner_id_a, v0,
            //    edge.b, corner_id_b, v1
            //);
            index_writer.write_edge(v0, v1);
        }
    }
}

void Build_context::build_centroid_points()
{
    ERHE_PROFILE_FUNCTION

    if (!root.build_info.format.features.centroid_points)
    {
        return;
    }

    const Polygon_id polygon_id_end = root.geometry.get_polygon_count();
    for (
        polygon_id = 0;
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
    const std::size_t       index_count,
    Index_range&             out_range
)
{
    out_range.primitive_type = primitive_type;
    out_range.first_index    = next_index_range_start;
    out_range.index_count    = index_count;
    next_index_range_start += index_count;

    // If index buffer has not yet been allocated, no check for enough room for index range
    ERHE_VERIFY(
        (primitive_geometry->index_buffer_range.count == 0) ||
        (next_index_range_start <= primitive_geometry->index_buffer_range.count)
    );
}


auto make_primitive(
    const erhe::geometry::Geometry& geometry,
    Build_info&                     build_info,
    const Normal_style              normal_style
) -> Primitive_geometry
{
    Primitive_builder builder{geometry, build_info, normal_style};
    return builder.build();
}

} // namespace erhe::primitive
