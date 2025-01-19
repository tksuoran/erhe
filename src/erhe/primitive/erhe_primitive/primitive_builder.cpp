// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_primitive/primitive_builder.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_primitive/buffer_writer.hpp"
#include "erhe_primitive/index_range.hpp"
#include "erhe_primitive/primitive_log.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/property_map.hpp"
#include "erhe_gl/enum_string_functions.hpp"
#include "erhe_gl/gl_helpers.hpp"
#include "erhe_graphics/vertex_attribute.hpp"
#include "erhe_graphics/vertex_format.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/glm.hpp>

namespace erhe::primitive {

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

using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;
using uvec4 = glm::uvec4;
using mat4 = glm::mat4;

Build_context_root::Build_context_root(
    const erhe::geometry::Geometry& geometry,
    const Build_info&               build_info,
    Element_mappings&               element_mappings_in,
    Buffer_mesh*                    buffer_mesh
)
    : geometry        {geometry}
    , build_info      {build_info}
    , element_mappings{element_mappings_in}
    , buffer_mesh     {buffer_mesh}
    , vertex_format   {build_info.buffer_info.vertex_format}
    , vertex_stride   {vertex_format.stride()}
{
    ERHE_PROFILE_FUNCTION();

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

    const Primitive_types& primitive_types = build_info.primitive_types;

    // Count vertices
    total_vertex_count = 0;
    total_vertex_count += mi.vertex_count_corners;
    if (primitive_types.centroid_points) {
        SPDLOG_LOGGER_INFO(log_primitive_builder, "{} centroid point indices", mi.vertex_count_centroids);
        total_vertex_count += mi.vertex_count_centroids;
    }

    // Count indices
    if (primitive_types.fill_triangles) {
        SPDLOG_LOGGER_INFO(log_primitive_builder, "{} triangle fill indices", mi.index_count_fill_triangles);
        total_index_count += mi.index_count_fill_triangles;
        allocate_index_range(gl::Primitive_type::triangles, mi.index_count_fill_triangles, buffer_mesh->triangle_fill_indices);
        const std::size_t primitive_count = mi.index_count_fill_triangles;
        element_mappings.primitive_id_to_polygon_id.resize(primitive_count);
    }

    if (primitive_types.edge_lines) {
        SPDLOG_LOGGER_INFO(log_primitive_builder, "{} edge line indices", mi.index_count_edge_lines);
        total_index_count += mi.index_count_edge_lines;
        allocate_index_range(gl::Primitive_type::lines, mi.index_count_edge_lines, buffer_mesh->edge_line_indices);
    }

    if (primitive_types.corner_points) {
        SPDLOG_LOGGER_INFO(log_primitive_builder, "{} corner point indices", mi.index_count_corner_points);
        total_index_count += mi.index_count_corner_points;
        allocate_index_range(gl::Primitive_type::points, mi.index_count_corner_points, buffer_mesh->corner_point_indices);
    }

    if (primitive_types.centroid_points) {
        SPDLOG_LOGGER_INFO(log_primitive_builder, "{} centroid point indices", mi.index_count_centroid_points);
        total_index_count += mi.index_count_centroid_points;
        allocate_index_range(gl::Primitive_type::points, mi.polygon_count, buffer_mesh->polygon_centroid_indices);
    }

    SPDLOG_LOGGER_INFO(log_primitive_builder, "Total {} vertices", total_vertex_count);
}

void Build_context_root::get_vertex_attributes()
{
    ERHE_PROFILE_FUNCTION();

    attributes.position       = Vertex_attribute_info(vertex_format, Vertex_attribute::Usage_type::position,      0);
    attributes.normal         = Vertex_attribute_info(vertex_format, Vertex_attribute::Usage_type::normal,        0); // content normals
    attributes.normal_smooth  = Vertex_attribute_info(vertex_format, Vertex_attribute::Usage_type::normal,        1); // smooth normals
    //attributes.normal_flat   = Vertex_attribute_info(vertex_format, Vertex_attribute::Usage_type::normal,    2); // flat normals
    attributes.tangent        = Vertex_attribute_info(vertex_format, Vertex_attribute::Usage_type::tangent,       0);
    attributes.bitangent      = Vertex_attribute_info(vertex_format, Vertex_attribute::Usage_type::bitangent,     0);
    attributes.color          = Vertex_attribute_info(vertex_format, Vertex_attribute::Usage_type::color,         0);
    attributes.aniso_control  = Vertex_attribute_info(vertex_format, Vertex_attribute::Usage_type::aniso_control, 0);
    attributes.texcoord       = Vertex_attribute_info(vertex_format, Vertex_attribute::Usage_type::tex_coord,     0);
    attributes.id_vec3        = Vertex_attribute_info(vertex_format, Vertex_attribute::Usage_type::id,            0);
    //// TODO
    //// if (erhe::graphics::g_instance->info.use_integer_polygon_ids)
    //// {
    ////     attributes.attribute_id_uint = Vertex_attribute_info(vertex_format, format_info.id_uint_type, 1, Vertex_attribute::Usage_type::id, 0);
    //// }
    attributes.joint_indices      = Vertex_attribute_info(vertex_format, Vertex_attribute::Usage_type::joint_indices, 0);
    attributes.joint_weights      = Vertex_attribute_info(vertex_format, Vertex_attribute::Usage_type::joint_weights, 0);
    attributes.valency_edge_count = Vertex_attribute_info(vertex_format, Vertex_attribute::Usage_type::valency_edge_count, 0);
}

void Build_context_root::allocate_vertex_buffer()
{
    ERHE_VERIFY(total_vertex_count > 0);

    buffer_mesh->vertex_buffer_range = build_info.buffer_info.buffer_sink.allocate_vertex_buffer(
        total_vertex_count, vertex_stride
    );
}

void Build_context_root::allocate_index_buffer()
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(total_index_count > 0);

    const erhe::dataformat::Format index_type     {build_info.buffer_info.index_type};
    const std::size_t              index_type_size{erhe::dataformat::get_format_size(index_type)};

    log_primitive_builder->trace(
        "allocating index buffer "
        "total_index_count = {}, index type size = {}",
        total_index_count,
        index_type_size
    );

    buffer_mesh->index_buffer_range = build_info.buffer_info.buffer_sink.allocate_index_buffer(
        total_index_count,
        index_type_size
    );
}

class Geometry_point_source : public erhe::math::Bounding_volume_source
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
        if (m_point_locations == nullptr) {
            return 0;
        }
        return m_geometry.get_point_count();
    }

    auto get_element_point_count(const std::size_t element_index) const -> std::size_t override
    {
        static_cast<void>(element_index);
        return 1;
    }

    auto get_point(const std::size_t element_index, const std::size_t point_index) const -> std::optional<glm::vec3> override
    {
        static_cast<void>(point_index);
        const auto point_id = static_cast<erhe::geometry::Point_id>(element_index);
        if (m_point_locations->has(point_id)) {
            return m_point_locations->get(point_id);
        }
        return {};
    }

private:
    const erhe::geometry::Geometry&               m_geometry;
    erhe::geometry::Property_map<Point_id, vec3>* m_point_locations{nullptr};
};

void Build_context_root::calculate_bounding_volume(erhe::geometry::Property_map<Point_id, vec3>* point_locations)
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(buffer_mesh != nullptr);

    const Geometry_point_source point_source{geometry, point_locations};

    erhe::math::calculate_bounding_volume(point_source, buffer_mesh->bounding_box, buffer_mesh->bounding_sphere);
}

Primitive_builder::Primitive_builder(
    const erhe::geometry::Geometry& geometry,
    const Build_info&               build_info,
    Element_mappings&               element_mappings,
    const Normal_style              normal_style
)
    : m_geometry        {geometry}
    , m_build_info      {build_info}
    , m_element_mappings{element_mappings}
    , m_normal_style    {normal_style}
{
}

auto Primitive_builder::build() -> Buffer_mesh
{
    Buffer_mesh buffer_mesh;
    build(&buffer_mesh);
    return buffer_mesh;
}

void Primitive_builder::build(Buffer_mesh* buffer_mesh)
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(buffer_mesh != nullptr);

    SPDLOG_LOGGER_INFO(
        log_primitive_builder,
        "Primitive_builder::build(usage = {}, normal_style = {}) geometry = {}",
        gl::c_str(m_build_info.buffer_info.usage),
        c_str(m_normal_style),
        m_geometry.name
    );

    Build_context build_context{
        m_geometry,
        m_build_info,
        m_element_mappings,
        m_normal_style,
        buffer_mesh
    };

    const Primitive_types& primitive_types = m_build_info.primitive_types;
    if (primitive_types.fill_triangles) {
        build_context.build_polygon_fill();
    }

    if (primitive_types.edge_lines) {
        build_context.build_edge_lines();
    }

    if (primitive_types.centroid_points) {
        build_context.build_centroid_points();
    }
}

Build_context::Build_context(
    const erhe::geometry::Geometry& geometry,
    const Build_info&               build_info,
    Element_mappings&               element_mappings,
    const Normal_style              normal_style,
    Buffer_mesh*                    buffer_mesh
)
    : root         {geometry, build_info, element_mappings, buffer_mesh}
    , normal_style {normal_style}
    , vertex_writer{*this, build_info.buffer_info.buffer_sink}
    , index_writer {*this, build_info.buffer_info.buffer_sink}
    , property_maps{geometry, build_info.primitive_types, build_info.buffer_info.vertex_format}
{
    ERHE_VERIFY(property_maps.point_locations != nullptr);

    root.calculate_bounding_volume(property_maps.point_locations);
}

Build_context::~Build_context() noexcept
{
    ERHE_VERIFY(vertex_index == root.total_vertex_count);
}

void Build_context::build_polygon_id()
{
    ERHE_PROFILE_FUNCTION();

    //// TODO
    //// if (
    ////     erhe::graphics::g_instance->info.use_integer_polygon_ids &&
    ////     root.attributes.attribute_id_uint.is_valid()
    //// ) {
    ////     vertex_writer.write(root.attributes.attribute_id_uint, polygon_index);
    //// }

    //// if (root.attributes.id_vec3.is_valid()) 
    {
        const vec3 v = erhe::math::vec3_from_uint(polygon_index);
        vertex_writer.write(root.attributes.id_vec3, v);
    }
}

void Build_context::build_vertex_position()
{
    ERHE_PROFILE_FUNCTION();

    // if (!root.attributes.position.is_valid()) {
    //     return;
    // }

    //// ERHE_VERIFY(property_maps.point_locations != nullptr);
    v_position = property_maps.point_locations->get(point_id);
    vertex_writer.write(root.attributes.position, v_position);

    SPDLOG_LOGGER_TRACE(
        log_primitive_builder,
        "polygon {} corner {} point {} vertex {} location {}",
        polygon_index, corner_id, point_id, vertex_index, v_position
    );
}

auto Build_context::get_polygon_normal() -> vec3
{
    ERHE_PROFILE_FUNCTION();
    vec3 polygon_normal{0.0f, 1.0f, 0.0f};
    if (property_maps.polygon_normals != nullptr) {
        if (property_maps.polygon_normals->maybe_get(polygon_id, polygon_normal)) {
            if (glm::length(polygon_normal) > 0.9f) {
                return polygon_normal;
            }
        }
    }

    polygon_normal = root.geometry.polygons[polygon_id].compute_normal(root.geometry, *property_maps.point_locations);
    if (glm::length(polygon_normal) > 0.9f) {
        return polygon_normal;
    }
    SPDLOG_LOGGER_WARN(log_primitive_builder, "Polygon {} - no normal - using fallback (0, 1, 0)", polygon_id);
    return glm::vec3{0.0f, 1.0f, 0.0f};
}

/////////////////////////////

void Build_context::build_tangent_frame()
{
    v_normal = vec3{0.0f, 1.0f, 0.0f};
    vec3 corner_normal {0.0f, 1.0f, 0.0f};
    vec3 point_normal  {0.0f, 1.0f, 0.0f};
    vec3 polygon_normal{0.0f, 1.0f, 0.0f};
    bool found_corner_normal{false};
    bool found_point_normal{false};
    bool found_polygon_normal{false};
    if (property_maps.corner_normals != nullptr) {
        found_corner_normal = property_maps.corner_normals->maybe_get(corner_id, corner_normal) && (glm::length(corner_normal) > 0.9f);
    }
    if (property_maps.point_normals != nullptr) {
        found_point_normal = property_maps.point_normals->maybe_get(point_id, point_normal) && (glm::length(point_normal) > 0.9f);
    }
    if (property_maps.polygon_normals != nullptr) {
        found_polygon_normal = property_maps.polygon_normals->maybe_get(polygon_id, polygon_normal) && (glm::length(polygon_normal) > 0.9f);
    }

    {
        ERHE_PROFILE_SCOPE("n");
        switch (normal_style) {
            //using enum Normal_style;
            case Normal_style::none: {
                // NOTE Was fallthrough to corner_normals
                break;
            }

            case Normal_style::corner_normals: {
                //// ERHE_VERIFY(glm::length(normal) > 0.9f);
                v_normal = found_corner_normal ? corner_normal : found_point_normal ? point_normal : found_polygon_normal ? polygon_normal : get_polygon_normal();
                break;
            }

            case Normal_style::point_normals: {
                v_normal = found_point_normal ? point_normal : found_polygon_normal ? polygon_normal : get_polygon_normal();
                break;
            }

            case Normal_style::polygon_normals: {
                v_normal = found_polygon_normal ? polygon_normal : get_polygon_normal();
                break;
            }

            default: {
                ERHE_FATAL("bad normal style");
            }
        }
    }

    v_tangent = vec4{1.0f, 0.0f, 0.0, 1.0f};
    bool found_tangent{false};
    if (property_maps.corner_tangents != nullptr) {
        found_tangent = property_maps.corner_tangents->maybe_get(corner_id, v_tangent) && (glm::length(v_tangent) > 0.9f);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        if (found_tangent) {
            SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} tangent {}", point_id, corner_id, tangent);
        }
#endif
    }
    if (!found_tangent && (property_maps.point_tangents != nullptr)) {
        found_tangent = property_maps.point_tangents->maybe_get(point_id, v_tangent) && (glm::length(v_tangent) > 0.9f);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        if (found_tangent) {
            SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} point tangent {}", point_id, corner_id, tangent);
        }
#endif
    }
    if (!found_tangent) {
        SPDLOG_LOGGER_TRACE(log_primitive_builder, "point_id {} corner {} fallback tangent", point_id, corner_id);
        v_tangent = glm::vec4{erhe::math::min_axis<float>(v_normal), 1.0f};
        used_fallback_tangent = true;
    }

    v_bitangent = vec4{0.0f, 0.0f, 1.0, 1.0f};
    bool found_bitangent{false};
    if (property_maps.corner_bitangents != nullptr) {
        found_bitangent = property_maps.corner_bitangents->maybe_get(corner_id, v_bitangent) && (glm::length(v_bitangent) > 0.9f);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        if (found_bitangent) {
            SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} bitangent {}", point_id, corner_id, bitangent);
        }
#endif
    }
    if (!found_bitangent && (property_maps.point_bitangents != nullptr)) {
        found_bitangent = property_maps.point_bitangents->maybe_get(point_id, v_bitangent) && (glm::length(v_bitangent) > 0.9f);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        if (found_bitangent) {
            SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} point bitangent {}", point_id, corner_id, bitangent);
        }
#endif
    }
    if (!found_bitangent) {
        SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} fallback bitangent", point_id, corner_id);
        glm::vec3 n0{v_normal};
        glm::vec3 t0{v_tangent};
        ERHE_VERIFY(glm::length(n0) > 0.9f);
        ERHE_VERIFY(glm::length(t0) > 0.9f);
        //glm::vec3 b0 = glm::cross(n, t);
        //ERHE_VERIFY(glm::length(b0) > 0.9f);
        //glm::vec3 b  = glm::normalize(b0);
        glm::vec3 b0 = erhe::math::safe_normalize_cross<float>(n0, t0);
        used_fallback_bitangent = true;
    }
    glm::vec3 n0{v_normal};
    glm::vec3 t0{v_tangent};
    glm::vec3 b0{v_bitangent};
    glm::vec3 n{v_normal};
    glm::vec3 t{v_tangent};
    glm::vec3 b{v_bitangent};
    erhe::math::gram_schmidt<float>(t0, b0, n0, t, b, n);
    v_tangent = glm::vec4{t, 1.0f};
    v_bitangent = glm::vec4{b, 1.0f};
}

/////////////////////////////

void Build_context::build_vertex_normal(bool do_normal, bool do_normal_smooth)
{
    ERHE_PROFILE_FUNCTION();

    /// if (!root.attributes.normal.is_valid() && !root.attributes.normal_smooth.is_valid()) {
    ///     return;
    /// }

    if (do_normal) {
        vertex_writer.write(root.attributes.normal, v_normal);
    }

    // if (features.normal_flat && root.attributes.normal_flat.is_valid()) {
    //     vertex_writer.write(root.attributes.normal_flat, polygon_normal);
    //     SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} flat polygon normal {}", point_id, corner_id, polygon_normal);
    // }
    // 
    if (do_normal_smooth) {
        ERHE_PROFILE_SCOPE("2n");
        vec3 smooth_point_normal{0.0f, 1.0f, 0.0f};
    
        if ((property_maps.point_normals_smooth != nullptr) && property_maps.point_normals_smooth->has(point_id)) {
            smooth_point_normal = property_maps.point_normals_smooth->get(point_id);
            SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} smooth point normal {}", point_id, corner_id, smooth_point_normal);
        } else {
            // Smooth normals are currently used only for wide line depth bias.
            // If edge lines are not used, do not generate warning about missing smooth normals.
            if (root.build_info.primitive_types.edge_lines) {
                SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} smooth unit y normal", point_id, corner_id);
                used_fallback_smooth_normal = true;
            }
        }
        vertex_writer.write(root.attributes.normal_smooth, smooth_point_normal);
    }
}

void Build_context::build_vertex_tangent()
{
    ERHE_PROFILE_FUNCTION();

    vertex_writer.write(root.attributes.tangent, v_tangent);
}

void Build_context::build_vertex_bitangent()
{
    ERHE_PROFILE_FUNCTION();

    vertex_writer.write(root.attributes.bitangent, v_bitangent);
}

void Build_context::build_vertex_texcoord()
{
    ERHE_PROFILE_FUNCTION();

    // if (!root.attributes.texcoord.is_valid()) {
    //     return;
    // }

    vec2 texcoord{0.0f, 0.0f};
    bool found{false};
    if (property_maps.corner_texcoords != nullptr) {
        found = property_maps.corner_texcoords->maybe_get(corner_id, texcoord);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        if (found) {
            SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} texcoord {}", point_id, corner_id, texcoord);
        }
#endif
    }
    if (!found && (property_maps.point_texcoords != nullptr)) {
        found = property_maps.point_texcoords->maybe_get(point_id, texcoord);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        if (found) {
            SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} point texcoord {}", point_id, corner_id, texcoord);
        }
#endif
    }
    if (!found) {
        SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} default texcoord", point_id, corner_id);
        used_fallback_texcoord = true;
    }

    vertex_writer.write(root.attributes.texcoord, texcoord);
}

void Build_context::build_vertex_joint_indices()
{
    ERHE_PROFILE_FUNCTION();

    //if (!root.attributes.joint_indices.is_valid()) {
    //    return;
    //}

    uvec4 joint_indices{0u, 0u, 0u, 0u};
    if (property_maps.point_joint_indices != nullptr) {
        property_maps.point_joint_indices->maybe_get(point_id, joint_indices);
    }
    vertex_writer.write(root.attributes.joint_indices, joint_indices);

    SPDLOG_LOGGER_TRACE(
        log_primitive_builder,
        "polygon {} corner {} point {} vertex {} joint_indices {}",
        polygon_index, corner_id, point_id, vertex_index, joint_indices
    );
}

void Build_context::build_vertex_joint_weights()
{
    ERHE_PROFILE_FUNCTION();

    //if (!root.attributes.joint_weights.is_valid()) {
    //    return;
    //}

    vec4 joint_weights{1.0f, 0.0f, 0.0f, 0.0f};
    if (property_maps.point_joint_weights != nullptr) {
        property_maps.point_joint_weights->maybe_get(point_id, joint_weights);
    }
    vertex_writer.write(root.attributes.joint_weights, joint_weights);

    SPDLOG_LOGGER_TRACE(
        log_primitive_builder,
        "polygon {} corner {} point {} vertex {} joint_weights {}",
        polygon_index, corner_id, point_id, vertex_index, joint_weights
    );
}

// namespace {
//
// constexpr glm::vec3 unique_colors[13] = {
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
    ERHE_PROFILE_FUNCTION();

    //if (!root.attributes.color.is_valid()) {
    //    return;
    //}

    vec4 color{root.build_info.constant_color};
    bool found{false};
    if (property_maps.corner_colors != nullptr) {
        found = property_maps.corner_colors->maybe_get(corner_id, color);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        if (found) {
            SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} corner color {}", point_id, corner_id, color);
        }
#endif
    }
    if (!found && (property_maps.point_colors != nullptr)) {
        found = property_maps.point_colors->maybe_get(point_id, color);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        if (found) {
            SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} point color {}", point_id, corner_id, color);
        }
#endif
    }
    if (!found && (property_maps.polygon_colors != nullptr)) {
        found = property_maps.polygon_colors->maybe_get(polygon_id, color);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        if (found) {
            SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} polygon {} polygon color {}", point_id, corner_id, polygon_id, color);
        }
#else
        static_cast<void>(found);
#endif
    }
    //if (!found) {
    //    color = root.build_info.format.constant_color;
    //    //trace_fmt(log_primitive_builder, "point {} corner {} constant color {}\n", point_id, corner_id, color);
    //    //if (polygon_corner_count > 3)
    //    //{
    //    //    color = glm::vec4{unique_colors[(polygon_corner_count - 3) % 13], 1.0f};
    //    //}
    //}

    vertex_writer.write(root.attributes.color, color);
}

void Build_context::build_vertex_aniso_control()
{
    ERHE_PROFILE_FUNCTION();

    //if (!root.attributes.aniso_control.is_valid()) {
    //    return;
    //}

    // X is used to modulate anisotropy level:
    //   0.0 -- Anisotropic
    //   1.0 -- Isotropic when approaching texcoord (0, 0)
    // Y is used for tangent space selection/control:
    //   0.0 -- Use geometry T and B (from vertex attribute
    //   1.0 -- Use T and B derived from texcoord
    vec2 value{1.0f, 1.0f};
    bool found{false};
    if (property_maps.corner_aniso_control != nullptr) {
        found = property_maps.corner_aniso_control->maybe_get(corner_id, value);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        if (found) {
            SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} corner aniso control {}", point_id, corner_id, value);
        }
#endif
    }
    if (!found && (property_maps.point_aniso_control != nullptr)) {
        found = property_maps.point_aniso_control->maybe_get(point_id, value);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        if (found) {
            SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} point aniso control {}", point_id, corner_id, value);
        }
#endif
    }
    if (!found && (property_maps.polygon_aniso_control != nullptr)) {
        found = property_maps.polygon_aniso_control->maybe_get(polygon_id, value);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        if (found) {
            SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} polygon {} polygon aniso control {}", point_id, corner_id, polygon_id, value);
        }
#else
        static_cast<void>(found);
#endif
    }

    vertex_writer.write(root.attributes.aniso_control, value);
}

void Build_context::build_centroid_position()
{
    if (!root.build_info.primitive_types.centroid_points || !root.attributes.position.is_valid()) {
        return;
    }

    vec3 position{0.0f, 0.0f, 0.0f};
    if ((property_maps.polygon_centroids != nullptr) && property_maps.polygon_centroids->has(polygon_id)) {
        position = property_maps.polygon_centroids->get(polygon_id);
    }

    vertex_writer.write(root.attributes.position, position);
}

void Build_context::build_centroid_normal()
{
    //const auto& features = root.build_info.format.features;

    if (!root.build_info.primitive_types.centroid_points) {
        return;
    }

    vec3 normal{0.0f, 1.0f, 0.0f};
    if (property_maps.polygon_normals != nullptr) {
        property_maps.polygon_normals->maybe_get(polygon_id, normal);
    }

    if (root.attributes.normal.is_valid()) {
        vertex_writer.write(root.attributes.normal, normal);
    }

    // if (root.attributes.normal_flat.is_valid()) {
    //     vertex_writer.write(root.attributes.normal_flat, normal);
    // }
}

void Build_context::build_valency_edge_count()
{
    //if (root.attributes.valency_edge_count.is_valid()) 
    //{
    const unsigned int vertex_valency      = static_cast<unsigned int>(root.geometry.points.at(point_id).corner_count);
    const unsigned int polygone_edge_count = static_cast<unsigned int>(root.geometry.polygons.at(polygon_id).corner_count);
    const glm::uvec2 valency_edge_count{vertex_valency, polygone_edge_count};
    vertex_writer.write(root.attributes.valency_edge_count, valency_edge_count);
    //}
}

void Build_context::build_corner_point_index()
{
    //if (root.build_info.primitive_types.corner_points) {
    index_writer.write_corner(vertex_index);
    //}
}

void Build_context::build_triangle_fill_index()
{
    if (root.build_info.primitive_types.fill_triangles) {
        if (previous_index != first_index) {
            index_writer.write_triangle(first_index, previous_index, vertex_index);
            root.element_mappings.primitive_id_to_polygon_id[primitive_index] = polygon_id;
            ++primitive_index;
        }
    }

    previous_index = vertex_index;
}

void Build_context::build_polygon_fill()
{
    ERHE_PROFILE_FUNCTION();

    // TODO property_maps.corner_indices needs to be setup
    //      also if edge lines are wanted.

    property_maps.corner_indices->clear();

    vertex_index  = 0;
    polygon_index = 0;

    //const bool any_normal_feature = root.build_info.format.features.normal =
    //    root.build_info.format.features.normal      ||
    //    root.build_info.format.features.normal_flat ||
    //    root.build_info.format.features.normal_smooth;

    const Polygon_id polygon_id_end = root.geometry.get_polygon_count();
    root.element_mappings.corner_to_vertex_id.resize(root.geometry.get_corner_count());

    const bool do_polygon_id           = root.attributes.id_vec3           .is_valid();
    const bool do_vertex_position      = root.attributes.position          .is_valid();
    const bool do_vertex_normal        = root.attributes.normal            .is_valid();
    const bool do_vertex_normal_smooth = root.attributes.normal_smooth     .is_valid();
    const bool do_vertex_normal_either = do_vertex_normal || do_vertex_normal_smooth;
    const bool do_vertex_tangent       = root.attributes.tangent           .is_valid();
    const bool do_vertex_bitangent     = root.attributes.bitangent         .is_valid();
    const bool do_vertex_texcoord      = root.attributes.texcoord          .is_valid();
    const bool do_vertex_color         = root.attributes.color             .is_valid();
    const bool do_aniso_control        = root.attributes.aniso_control     .is_valid();
    const bool do_joint_indices        = root.attributes.joint_indices     .is_valid();
    const bool do_joint_weights        = root.attributes.joint_weights     .is_valid();
    const bool do_vertex_valency       = root.attributes.valency_edge_count.is_valid();
    const bool do_corner_points        = root.build_info.primitive_types.corner_points;
    const bool do_tangent_frame = do_vertex_normal_either || do_vertex_tangent || do_vertex_bitangent;

    for (polygon_id = 0; polygon_id < polygon_id_end; ++polygon_id) {
        ERHE_PROFILE_SCOPE("polygon");
        const Polygon& polygon = root.geometry.polygons[polygon_id];
        first_index    = vertex_index;
        previous_index = first_index;

        if (property_maps.polygon_ids_uint32 != nullptr) {
            property_maps.polygon_ids_uint32->put(polygon_id, polygon_index);
        }

        if (property_maps.polygon_ids_vector3 != nullptr) {
            property_maps.polygon_ids_vector3->put(polygon_id, erhe::math::vec3_from_uint(polygon_index));
        }

        const Polygon_corner_id polyon_corner_id_end = polygon.first_polygon_corner_id + polygon.corner_count;
        for (polygon_corner_id = polygon.first_polygon_corner_id; polygon_corner_id < polyon_corner_id_end; ++polygon_corner_id) {
            ERHE_PROFILE_SCOPE("corner");
            corner_id            = root.geometry.polygon_corners[polygon_corner_id];
            const Corner& corner = root.geometry.corners[corner_id];
            point_id             = corner.point_id;

            root.element_mappings.corner_to_vertex_id[corner_id] = vertex_index;

            if (do_polygon_id          ) build_polygon_id          ();

            if (do_tangent_frame       ) build_tangent_frame       ();
            if (do_vertex_position     ) build_vertex_position     ();
            if (do_vertex_normal_either) build_vertex_normal       (do_vertex_normal, do_vertex_normal_smooth);
            if (do_vertex_tangent      ) build_vertex_tangent      ();
            if (do_vertex_bitangent    ) build_vertex_bitangent    ();
            if (do_vertex_texcoord     ) build_vertex_texcoord     ();
            if (do_vertex_color        ) build_vertex_color        (polygon.corner_count);
            if (do_aniso_control       ) build_vertex_aniso_control();
            if (do_joint_indices       ) build_vertex_joint_indices();
            if (do_joint_weights       ) build_vertex_joint_weights();
            if (do_vertex_valency      ) build_valency_edge_count  ();

            // Indices
            property_maps.corner_indices->put(corner_id, vertex_index);

            if (do_corner_points) build_corner_point_index();
            build_triangle_fill_index();

            vertex_writer.move(root.vertex_stride);
            ++vertex_index;
        }

        ++polygon_index;
    }

    if (used_fallback_smooth_normal) {
        log_primitive_builder->warn("Warning: Used fallback smooth normal");
    }
    if (used_fallback_tangent) {
        log_primitive_builder->warn("Warning: Used fallback tangent");
    }
    if (used_fallback_bitangent) {
        log_primitive_builder->warn("Warning: Used fallback bitangent");
    }
    if (used_fallback_texcoord) {
        log_primitive_builder->warn("Warning: Used fallback texcoord");
    }
}

void Build_context::build_edge_lines()
{
    ERHE_PROFILE_FUNCTION();

    if (!root.build_info.primitive_types.edge_lines) {
        return;
    }

    for (Edge_id edge_id = 0, end = root.geometry.get_edge_count(); edge_id < end; ++edge_id) {
        const Edge&           edge              = root.geometry.edges[edge_id];
        const Point&          point_a           = root.geometry.points[edge.a];
        const Point&          point_b           = root.geometry.points[edge.b];
        const Point_corner_id point_corner_id_a = point_a.first_point_corner_id;
        const Point_corner_id point_corner_id_b = point_b.first_point_corner_id;
        const Corner_id       corner_id_a       = root.geometry.point_corners[point_corner_id_a];
        const Corner_id       corner_id_b       = root.geometry.point_corners[point_corner_id_b];

        ERHE_VERIFY(edge.a != edge.b);

        if (property_maps.corner_indices->has(corner_id_a) && property_maps.corner_indices->has(corner_id_b)) {
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
    ERHE_PROFILE_FUNCTION();

    if (!root.build_info.primitive_types.centroid_points) {
        return;
    }

    const Polygon_id polygon_id_end = root.geometry.get_polygon_count();
    for (polygon_id = 0; polygon_id < polygon_id_end; ++polygon_id) {
        build_centroid_position();
        build_centroid_normal();

        index_writer.write_centroid(vertex_index);
        vertex_writer.move(root.vertex_stride);
        ++vertex_index;
    }
}

void Build_context_root::allocate_index_range(const gl::Primitive_type primitive_type, const std::size_t index_count, Index_range& out_range)
{
    out_range.primitive_type = primitive_type;
    out_range.first_index    = next_index_range_start;
    out_range.index_count    = index_count;
    next_index_range_start += index_count;

    // If index buffer has not yet been allocated, no check for enough room for index range
    ERHE_VERIFY(
        (buffer_mesh->index_buffer_range.count == 0) ||
        (next_index_range_start <= buffer_mesh->index_buffer_range.count)
    );
}

auto make_buffer_mesh(
    const erhe::geometry::Geometry& geometry,
    const Build_info&               build_info,
    Element_mappings&               element_mappings,
    const Normal_style              normal_style
) -> Buffer_mesh
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(element_mappings.primitive_id_to_polygon_id.empty());
    ERHE_VERIFY(element_mappings.corner_to_vertex_id.empty());
    Primitive_builder builder{geometry, build_info, element_mappings, normal_style};
    return builder.build();
}

} // namespace erhe::primitive
