// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_primitive/primitive_builder.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_primitive/buffer_writer.hpp"
#include "erhe_primitive/index_range.hpp"
#include "erhe_primitive/primitive_log.hpp"
#include "erhe_log/log.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_profile/profile.hpp"

#include <fmt/format.h>
#include "erhe_verify/verify.hpp"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <utility>

using erhe::geometry::get_mesh_info;
using erhe::geometry::get_pointf;
using erhe::geometry::mesh_facet_centerf;
using erhe::geometry::mesh_facet_normalf;
using erhe::geometry::to_glm_vec3;
using erhe::geometry::to_glm_vec4;
using erhe::geometry::vec3_from_index;

namespace erhe::primitive {

Build_context_root::Build_context_root(
    Buffer_mesh&      buffer_mesh,
    const GEO::Mesh&  mesh,
    const Build_info& build_info,
    Element_mappings& element_mappings_in
)
    : buffer_mesh     {buffer_mesh}
    , mesh            {mesh}
    , build_info      {build_info}
    , element_mappings{element_mappings_in}
    , mesh_info       {::get_mesh_info(mesh)}
    , vertex_format   {build_info.buffer_info.vertex_format}
{
    get_mesh_info                  ();
    get_vertex_attributes          ();
    allocate_vertex_buffers        ();
    allocate_edge_line_vertex_buffer();
    allocate_edge_line_joint_buffer ();
    allocate_expanded_fill_buffers  ();
    allocate_index_buffer          ();
}

void Build_context_root::get_mesh_info()
{
    const Primitive_types& primitive_types = build_info.primitive_types;

    // Count vertices
    total_vertex_count = 0;
    total_vertex_count += mesh_info.vertex_count_corners;
    if (primitive_types.centroid_points) {
        total_vertex_count += mesh_info.vertex_count_centroids;
    }

    // Count indices
    if (primitive_types.fill_triangles) {
        total_index_count += mesh_info.index_count_fill_triangles;
        allocate_index_range(Primitive_type::triangles, mesh_info.index_count_fill_triangles, buffer_mesh.triangle_fill_indices);
        // One entry per fill triangle (the map is keyed by the 0-based triangle
        // index), not per index: index_count_fill_triangles counts 3 indices per
        // triangle, so divide by 3.
        const std::size_t triangle_count = mesh_info.index_count_fill_triangles / 3;
        element_mappings.triangle_to_mesh_facet.resize(triangle_count);
    }

    // Expanded solid-wireframe fill: one sequential index per expanded vertex
    // (3 per fill triangle), values 0..3N-1 into the dedicated expanded vertex
    // buffer. Only when the caller supplied an expanded vertex format.
    if (primitive_types.fill_triangles_expanded && (build_info.buffer_info.expanded_vertex_format != nullptr)) {
        total_index_count += mesh_info.index_count_fill_triangles;
        allocate_index_range(Primitive_type::triangles, mesh_info.index_count_fill_triangles, buffer_mesh.expanded_triangle_fill_indices);
    }

    if (primitive_types.edge_lines) {
        total_index_count += mesh_info.index_count_edge_lines;
        allocate_index_range(Primitive_type::lines, mesh_info.index_count_edge_lines, buffer_mesh.edge_line_indices);
    }

    if (primitive_types.corner_points) {
        total_index_count += mesh_info.index_count_corner_points;
        allocate_index_range(Primitive_type::points, mesh_info.index_count_corner_points, buffer_mesh.corner_point_indices);
    }

    if (primitive_types.centroid_points) {
        total_index_count += mesh_info.index_count_centroid_points;
        allocate_index_range(Primitive_type::points, mesh_info.facet_count, buffer_mesh.polygon_centroid_indices);
    }
}

void Build_context_root::get_vertex_attributes()
{
    ERHE_PROFILE_FUNCTION();

    using namespace erhe::dataformat;
    vertex_attributes.position              = Vertex_attribute_info{vertex_format, Vertex_attribute_usage::position,      0};
    vertex_attributes.normal                = Vertex_attribute_info{vertex_format, Vertex_attribute_usage::normal,        0}; // content normals
    vertex_attributes.normal_smooth         = Vertex_attribute_info{vertex_format, Vertex_attribute_usage::normal,        1}; // smooth normals
    vertex_attributes.tangent               = Vertex_attribute_info{vertex_format, Vertex_attribute_usage::tangent,       0};
    vertex_attributes.bitangent             = Vertex_attribute_info{vertex_format, Vertex_attribute_usage::bitangent,     0};
    vertex_attributes.id_vec4               = Vertex_attribute_info{vertex_format, Vertex_attribute_usage::custom,        custom_attribute_id};
    vertex_attributes.aniso_control         = Vertex_attribute_info{vertex_format, Vertex_attribute_usage::custom,        custom_attribute_aniso_control};
    vertex_attributes.valency_edge_count    = Vertex_attribute_info{vertex_format, Vertex_attribute_usage::custom,        custom_attribute_valency_edge_count};
    vertex_attributes.color        .push_back(Vertex_attribute_info{vertex_format, Vertex_attribute_usage::color,         0});
    vertex_attributes.color        .push_back(Vertex_attribute_info{vertex_format, Vertex_attribute_usage::color,         1});
    vertex_attributes.texcoord     .push_back(Vertex_attribute_info{vertex_format, Vertex_attribute_usage::tex_coord,     0});
    vertex_attributes.texcoord     .push_back(Vertex_attribute_info{vertex_format, Vertex_attribute_usage::tex_coord,     1});
    vertex_attributes.joint_indices.push_back(Vertex_attribute_info{vertex_format, Vertex_attribute_usage::joint_indices, 0});
    vertex_attributes.joint_indices.push_back(Vertex_attribute_info{vertex_format, Vertex_attribute_usage::joint_indices, 1});
    vertex_attributes.joint_weights.push_back(Vertex_attribute_info{vertex_format, Vertex_attribute_usage::joint_weights, 0});
    vertex_attributes.joint_weights.push_back(Vertex_attribute_info{vertex_format, Vertex_attribute_usage::joint_weights, 1});
}

void Build_context_root::allocate_vertex_buffers()
{
    // Lazy pools start with no allocated GPU memory and report 0 available;
    // they grow on demand inside allocate_vertex_buffer(). The previous
    // pre-flight check on get_available_vertex_byte_count() is therefore
    // skipped -- we drive the allocation directly and treat a zero-count
    // result (Buffer_sink_allocation default state) as the failure path.
    //
    // Special case: a primitive with zero vertices is legal (e.g. a
    // point-cloud / empty mesh). The allocator returns a successful
    // zero-byte allocation, so we must not interpret count == 0 as
    // failure when total_vertex_count is also 0.
    for (size_t i = 0, end = vertex_format.streams.size(); i < end; ++i) {
        const erhe::dataformat::Vertex_stream& sink_stream = vertex_format.streams[i];
        Buffer_sink_allocation sink_allocation = build_info.buffer_info.vertex_buffer_sink.allocate_vertex_buffer_range(sink_stream, total_vertex_count);
        if (sink_allocation.range.count == 0 && total_vertex_count > 0) {
            // Allocator refused: out of memory / pool exhausted.
            build_failed = true;
            return;
        }
        buffer_mesh.vertex_buffer_ranges.emplace_back(sink_allocation.range);
        buffer_mesh.vertex_allocations.emplace_back(std::move(sink_allocation.allocation));
    }
}

void Build_context_root::allocate_edge_line_vertex_buffer()
{
    // Allocate the dedicated edge-line vertex buffer range only when
    // edge lines were requested, the mesh actually has edges, and the
    // Buffer_info provides the wide-line vertex stream. CPU-buffer
    // sinks (e.g. raytrace) leave edge_line_vertex_stream null and
    // skip this entirely. The data is two vec4 (position + smooth
    // normal) per edge endpoint -- see Build_context::build_edge_lines.
    if (build_failed) {
        return;
    }
    if (!build_info.primitive_types.edge_lines) {
        return;
    }
    if (mesh_info.edge_count == 0) {
        return;
    }
    if (build_info.buffer_info.edge_line_vertex_stream == nullptr) {
        return;
    }

    const std::size_t edge_line_vertex_count = mesh_info.edge_count * 2;
    Buffer_sink_allocation sink_allocation = build_info.buffer_info.vertex_buffer_sink.allocate_vertex_buffer_range(
        *build_info.buffer_info.edge_line_vertex_stream,
        edge_line_vertex_count
    );
    if (sink_allocation.range.count == 0) {
        build_failed = true;
        return;
    }
    buffer_mesh.edge_line_vertex_buffer_range = sink_allocation.range;
    buffer_mesh.edge_line_vertex_allocation   = std::move(sink_allocation.allocation);
}

void Build_context_root::allocate_edge_line_joint_buffer()
{
    // Companion to allocate_edge_line_vertex_buffer: holds per-endpoint
    // joint indices + weights so the skinned variant of the wide-line
    // compute shader can skin edges on the GPU. Allocated only when the
    // source mesh actually carries joint attributes (i.e. is skinned)
    // and the caller has provided an edge_line_joint_stream.
    if (build_failed) {
        return;
    }
    if (!build_info.primitive_types.edge_lines) {
        return;
    }
    if (mesh_info.edge_count == 0) {
        return;
    }
    if (build_info.buffer_info.edge_line_joint_stream == nullptr) {
        return;
    }
    const GEO::AttributesManager& vertex_attrs = mesh.vertices.attributes();
    if (
        !vertex_attrs.is_defined(erhe::geometry::c_joint_indices_0) ||
        !vertex_attrs.is_defined(erhe::geometry::c_joint_weights_0)
    ) {
        return;
    }

    const std::size_t edge_line_vertex_count = mesh_info.edge_count * 2;
    Buffer_sink_allocation sink_allocation = build_info.buffer_info.vertex_buffer_sink.allocate_vertex_buffer_range(
        *build_info.buffer_info.edge_line_joint_stream,
        edge_line_vertex_count
    );
    if (sink_allocation.range.count == 0) {
        build_failed = true;
        return;
    }
    buffer_mesh.edge_line_joint_buffer_range = sink_allocation.range;
    buffer_mesh.edge_line_joint_allocation   = std::move(sink_allocation.allocation);
}

void Build_context_root::allocate_expanded_fill_buffers()
{
    // Allocate the dedicated expanded solid-wireframe fill vertex stream(s).
    // The expanded mesh has one un-shared vertex per fill-triangle corner
    // (3 * triangle_count) in the expanded vertex format (fill attributes plus
    // custom_attribute_wireframe). Only when fill_triangles_expanded was
    // requested and the caller supplied an expanded vertex format.
    if (build_failed) {
        return;
    }
    if (!build_info.primitive_types.fill_triangles_expanded) {
        return;
    }
    if (build_info.buffer_info.expanded_vertex_format == nullptr) {
        return;
    }
    const std::size_t expanded_vertex_count = mesh_info.index_count_fill_triangles; // 3 per triangle
    if (expanded_vertex_count == 0) {
        return;
    }
    for (const erhe::dataformat::Vertex_stream& stream : build_info.buffer_info.expanded_vertex_format->streams) {
        Buffer_sink_allocation sink_allocation = build_info.buffer_info.vertex_buffer_sink.allocate_vertex_buffer_range(stream, expanded_vertex_count);
        if (sink_allocation.range.count == 0) {
            build_failed = true;
            return;
        }
        buffer_mesh.expanded_vertex_buffer_ranges.emplace_back(sink_allocation.range);
        buffer_mesh.expanded_vertex_allocations.emplace_back(std::move(sink_allocation.allocation));
    }
    buffer_mesh.expanded_vertex_input_key = build_info.buffer_info.expanded_vertex_input_key;
}

void Build_context_root::allocate_index_buffer()
{
    ERHE_VERIFY(total_index_count > 0);

    const erhe::dataformat::Format index_type           {build_info.buffer_info.index_type};
    const std::size_t              index_type_size_bytes{erhe::dataformat::get_format_size_bytes(index_type)};

    log_primitive_builder->trace(
        "allocating index buffer "
        "total_index_count = {}, index type size = {}",
        total_index_count,
        index_type_size_bytes
    );

    Buffer_sink_allocation sink_allocation = build_info.buffer_info.index_buffer_sink.allocate_index_buffer_range(
        build_info.buffer_info.index_type,
        total_index_count
    );
    if (sink_allocation.range.count == 0) {
        build_failed = true;
        return;
    }
    buffer_mesh.index_buffer_range = sink_allocation.range;
    buffer_mesh.index_allocation   = std::move(sink_allocation.allocation);
}

class Mesh_point_source : public erhe::math::Bounding_volume_source
{
public:
    explicit Mesh_point_source(const GEO::Mesh& mesh) : m_mesh{mesh} {}

    auto get_element_count() const -> std::size_t override { return m_mesh.vertices.nb(); }
    auto get_element_point_count(const std::size_t) const -> std::size_t override { return 1;}
    auto get_point(const std::size_t element_index, const std::size_t) const -> std::optional<glm::vec3> override
    {
        const GEO::vec3f p = get_pointf(m_mesh.vertices, static_cast<GEO::index_t>(element_index));
        return to_glm_vec3(p);
    }

private:
    const GEO::Mesh& m_mesh;
};

void Build_context_root::calculate_bounding_volume()
{
    const Mesh_point_source point_source{mesh};
    erhe::math::calculate_bounding_volume(point_source, buffer_mesh.bounding_box, buffer_mesh.bounding_sphere);
}

Primitive_builder::Primitive_builder(
    Buffer_mesh&       buffer_mesh,
    const GEO::Mesh&   mesh,
    const Build_info&  build_info,
    Element_mappings&  element_mappings,
    const Normal_style normal_style
)
    : m_buffer_mesh     {buffer_mesh}
    , m_mesh            {mesh}
    , m_build_info      {build_info}
    , m_element_mappings{element_mappings}
    , m_normal_style    {normal_style}
{
}

auto Primitive_builder::build() -> bool
{
    ERHE_PROFILE_FUNCTION();

    log_primitive_builder->trace("Primitive_builder::build(normal_style = {})", c_str(m_normal_style));

    Build_context build_context{
        m_buffer_mesh,
        m_mesh,
        m_build_info,
        m_element_mappings,
        m_normal_style,
    };

    m_buffer_mesh.vertex_input_key = m_build_info.buffer_info.vertex_input_key;

    if (!build_context.is_ready()) {
        log_primitive_builder->debug("Primitive_builder::build() aborted because build_context is not ready");
        return false;
    }

    // Breadcrumbs localize which build sub-step a spinning render thread is
    // stuck in (these walk mesh corners/edges and can loop forever on
    // degenerate / non-manifold geometry). See doc/intermittent_main_loop_hang.md.
    const Primitive_types& primitive_types = m_build_info.primitive_types;
    if (primitive_types.fill_triangles) {
        // Include mesh counts so the watchdog dump reveals whether the mesh is
        // corrupt/absurdly sized (the build_polygon_fill spin walks these).
        erhe::log::set_breadcrumb(
            fmt::format(
                "primitive: build_polygon_fill facets={} verts={} corners={}",
                m_mesh.facets.nb(), m_mesh.vertices.nb(), m_mesh.facet_corners.nb()
            )
        );
        build_context.build_polygon_fill();
    }

    if (primitive_types.fill_triangles_expanded) {
        erhe::log::set_breadcrumb("primitive: build_expanded_polygon_fill");
        build_context.build_expanded_polygon_fill();
    }

    if (primitive_types.edge_lines) {
        erhe::log::set_breadcrumb("primitive: build_edge_lines");
        build_context.build_edge_lines();
    }

    if (primitive_types.centroid_points) {
        erhe::log::set_breadcrumb("primitive: build_centroid_points");
        build_context.build_centroid_points();
    }
    return true;
}

[[nodiscard]] auto Build_context::get_attribute_writer(erhe::dataformat::Vertex_attribute_usage usage, std::size_t index) -> Vertex_buffer_writer*
{
    erhe::dataformat::Attribute_stream info = root.vertex_format.find_attribute(usage, index);
    if (info.attribute != nullptr) {
        std::size_t stream_index = info.stream - root.vertex_format.streams.data();
        return vertex_writers.at(stream_index).get();
    }
    return nullptr;
}

Build_context::Build_context(
    Buffer_mesh&       buffer_mesh,
    const GEO::Mesh&   mesh,
    const Build_info&  build_info,
    Element_mappings&  element_mappings,
    const Normal_style normal_style
)
    : root           {buffer_mesh, mesh, build_info, element_mappings}
    , normal_style   {normal_style}
    , index_writer   {*this, build_info.buffer_info.index_buffer_sink}
    , mesh_attributes{mesh}
{
    if (root.build_failed) {
        return;
    }
    for (std::size_t stream_index = 0, stream_end = root.vertex_format.streams.size(); stream_index < stream_end; ++stream_index) {
        const erhe::dataformat::Vertex_stream& sink_stream = root.vertex_format.streams.at(stream_index);
        //Vertex_buffer_writer vertex_writer{*this, build_info.buffer_info.buffer_sink, stream_index, sink_stream.stride};
        vertex_writers.push_back(
            std::make_unique<Vertex_buffer_writer>(
                *this,
                build_info.buffer_info.vertex_buffer_sink,
                stream_index,
                sink_stream.stride
            )
        );
    }

    using namespace erhe::dataformat;
    attribute_writers.position           = get_attribute_writer(Vertex_attribute_usage::position);
    attribute_writers.normal             = get_attribute_writer(Vertex_attribute_usage::normal, normal_attribute);
    attribute_writers.normal_smooth      = get_attribute_writer(Vertex_attribute_usage::normal, normal_attribute_smooth);
    attribute_writers.tangent            = get_attribute_writer(Vertex_attribute_usage::tangent);
    attribute_writers.bitangent          = get_attribute_writer(Vertex_attribute_usage::bitangent);
    attribute_writers.color_0            = get_attribute_writer(Vertex_attribute_usage::color);
    attribute_writers.texcoord_0         = get_attribute_writer(Vertex_attribute_usage::tex_coord);
    attribute_writers.joint_indices_0    = get_attribute_writer(Vertex_attribute_usage::joint_indices);
    attribute_writers.joint_weights_0    = get_attribute_writer(Vertex_attribute_usage::joint_weights);
    attribute_writers.id                 = get_attribute_writer(Vertex_attribute_usage::custom, custom_attribute_id);
    attribute_writers.aniso_control      = get_attribute_writer(Vertex_attribute_usage::custom, custom_attribute_aniso_control);
    attribute_writers.valency_edge_count = get_attribute_writer(Vertex_attribute_usage::custom, custom_attribute_valency_edge_count);

    root.calculate_bounding_volume();
}

Build_context::~Build_context() noexcept
{
    if (root.build_failed) {
        log_primitive_builder->warn("Primitive build failed");
    }
    ERHE_VERIFY(root.build_failed || (vertex_buffer_index == root.total_vertex_count));
}

void Build_context::build_polygon_id()
{
    ERHE_PROFILE_FUNCTION();

    if (attribute_writers.id == nullptr) {
        return;
    }

    const glm::vec4 id_vec4 = erhe::math::vec4_from_uint(static_cast<uint32_t>(mesh_facet));
    attribute_writers.id->write(root.vertex_attributes.id_vec4, id_vec4);
}

void Build_context::build_vertex_position()
{
    ERHE_PROFILE_FUNCTION();

    v_position = get_pointf(root.mesh.vertices, mesh_vertex);

    ERHE_VERIFY(std::isfinite(v_position.x) && std::isfinite(v_position.y) && std::isfinite(v_position.z));
    attribute_writers.position->write(root.vertex_attributes.position, v_position);

    SPDLOG_LOGGER_TRACE(
        log_primitive_builder,
        "Mesh: facet {} corner {} vertex {} Vertex buffer index {} location {}",
        mesh_facet, mesh_corner, mesh_vertex, vertex_buffer_index, v_position
    );
}

auto Build_context::get_facet_normal() -> GEO::vec3f
{
    ERHE_PROFILE_FUNCTION();
    {
        const std::optional<GEO::vec3f> facet_normal = mesh_attributes.facet_normal.try_get(mesh_facet);
        if (facet_normal.has_value()) {
            ERHE_VERIFY(GEO::length2(facet_normal.value()) > 0.9f);
            return facet_normal.value();
        }
    }

    const GEO::vec3f facet_normal = GEO::normalize(mesh_facet_normalf(root.mesh, mesh_facet));
    return facet_normal;
}

/////////////////////////////

auto sign(const float x) -> float
{
    return x < 0.0f ? -1.0f : 1.0f;
}

void ortho_basis_pixar_r1(const GEO::vec3f N, GEO::vec4f& T, GEO::vec3f& B)
{
    const float      sz    = sign(N.z);
    const float      a     = 1.0f / (sz + N.z);
    const float      sx    = sz * N.x;
    const float      b     = N.x * N.y * a;
    const GEO::vec3f t_    = GEO::vec3f{sx * N.x * a - 1.f, sz * b, sx};
    const GEO::vec3f b_    = GEO::vec3f{b, N.y * N.y * a - sz, N.y};
    const GEO::vec3f t_xyz = GEO::normalize(t_ - N * GEO::dot(N, t_));
    const float      t_w   = (GEO::dot(GEO::cross(N, t_), b_) < 0.0f) ? -1.0f : 1.0f;
    const GEO::vec3f b_xyz = GEO::normalize(b_ - N * GEO::dot(N, b_));
    //const float      b_w   = (GEO::dot(GEO::cross(b_, N), t_) < 0.0f) ? -1.0f : 1.0f;
    T = GEO::vec4f{t_xyz, t_w};
    B = b_xyz;
}

void Build_context::build_tangent_frame()
{
    v_normal = GEO::vec3f{0.0f, 1.0f, 0.0f};

    std::optional<GEO::vec3f> corner_normal = mesh_attributes.corner_normal.try_get(mesh_corner);
    std::optional<GEO::vec3f> facet_normal  = mesh_attributes.facet_normal .try_get(mesh_facet);
    std::optional<GEO::vec3f> vertex_normal = mesh_attributes.vertex_normal.try_get(mesh_vertex);

    {
        ERHE_PROFILE_SCOPE("n");
        switch (normal_style) {
            //using enum Normal_style;
            case Normal_style::none: {
                // NOTE Was fallthrough to corner_normals
                break;
            }

            case Normal_style::corner_normals: {
                v_normal = 
                    corner_normal.has_value() ? corner_normal.value() :
                    facet_normal .has_value() ? facet_normal .value() :
                    vertex_normal.has_value() ? vertex_normal.value() : get_facet_normal();
                break;
            }

            case Normal_style::point_normals: {
                v_normal =
                    vertex_normal.has_value() ? vertex_normal.value() :
                    facet_normal .has_value() ? facet_normal .value() : get_facet_normal();

                break;
            }

            case Normal_style::polygon_normals: {
                v_normal = facet_normal.has_value() ? facet_normal.value() : get_facet_normal();
                break;
            }

            default: {
                ERHE_FATAL("bad normal style");
            }
        }
    }

    std::optional<GEO::vec4f> corner_tangent = mesh_attributes.corner_tangent.try_get(mesh_corner);
    std::optional<GEO::vec4f> facet_tangent  = mesh_attributes.facet_tangent .try_get(mesh_facet);
    std::optional<GEO::vec4f> vertex_tangent = mesh_attributes.vertex_tangent.try_get(mesh_vertex);

    const bool gen_tangent = 
        !corner_tangent.has_value() &&
        !facet_tangent .has_value() &&
        !vertex_tangent.has_value();

    GEO::vec4f fallback_tangent;
    GEO::vec3f fallback_bitangent;
    if (gen_tangent) {
        ortho_basis_pixar_r1(v_normal, fallback_tangent, fallback_bitangent);
    }

    v_tangent =
        corner_tangent.has_value() ? corner_tangent.value() :
        facet_tangent .has_value() ? facet_tangent .value() :
        vertex_tangent.has_value() ? vertex_tangent.value() : fallback_tangent;

    const GEO::vec3f v_tangent3{v_tangent};

    std::optional<GEO::vec3f> corner_bitangent = mesh_attributes.corner_bitangent.try_get(mesh_corner);
    std::optional<GEO::vec3f> facet_bitangent  = mesh_attributes.facet_bitangent .try_get(mesh_facet);
    std::optional<GEO::vec3f> vertex_bitangent = mesh_attributes.vertex_bitangent.try_get(mesh_vertex);

    v_bitangent =
        corner_bitangent.has_value() ? corner_bitangent.value() :
        facet_bitangent .has_value() ? facet_bitangent .value() :
        vertex_bitangent.has_value() ? vertex_bitangent.value() : fallback_bitangent;
}

/////////////////////////////

void Build_context::build_vertex_normal(bool do_normal, bool do_normal_smooth)
{
    ERHE_PROFILE_FUNCTION();

    /// if (!root.attributes.normal.is_valid() && !root.attributes.normal_smooth.is_valid()) {
    ///     return;
    /// }

    if (do_normal) {
        attribute_writers.normal->write(root.vertex_attributes.normal, to_glm_vec3(v_normal));
    }

    // if (features.normal_flat && root.attributes.normal_flat.is_valid()) {
    //     vertex_writer.write(root.attributes.normal_flat, polygon_normal);
    //     SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} flat polygon normal {}", point_id, corner_id, polygon_normal);
    // }
    // 
    if (do_normal_smooth) {
        ERHE_PROFILE_SCOPE("2n");
    
        std::optional<GEO::vec3f> smooth_vertex_normal = mesh_attributes.vertex_normal_smooth.try_get(mesh_vertex);
        if (smooth_vertex_normal.has_value()) {
            attribute_writers.normal_smooth->write(root.vertex_attributes.normal_smooth, to_glm_vec3(smooth_vertex_normal.value()));
        } else {
            // Smooth normals are currently used only for wide line depth bias.
            // If edge lines are not used, do not generate warning about missing smooth normals.
            if (root.build_info.primitive_types.edge_lines) {
                SPDLOG_LOGGER_TRACE(log_primitive_builder, "point {} corner {} smooth unit y normal", point_id, corner_id);
                used_fallback_smooth_normal = true;
            }
            const GEO::vec3f fallback_vertex_normal_smooth{0.0f, 1.0f, 0.0f};
            attribute_writers.normal_smooth->write(root.vertex_attributes.normal_smooth, fallback_vertex_normal_smooth);
        }
    }
}

void Build_context::build_vertex_tangent()
{
    attribute_writers.tangent->write(root.vertex_attributes.tangent, to_glm_vec4(v_tangent));
}

void Build_context::build_vertex_bitangent()
{
    attribute_writers.bitangent->write(root.vertex_attributes.bitangent, to_glm_vec3(v_bitangent));
}

void Build_context::build_vertex_texcoord(size_t usage_index)
{
    std::optional<GEO::vec2f> corner_texcoord = mesh_attributes.corner_texcoord(usage_index).try_get(mesh_corner);
    std::optional<GEO::vec2f> vertex_texcoord = mesh_attributes.vertex_texcoord(usage_index).try_get(mesh_vertex);

    GEO::vec2f texcoord = 
        corner_texcoord.has_value() ? corner_texcoord.value() :
        vertex_texcoord.has_value() ? vertex_texcoord.value() : GEO::vec2f{0.0f, 0.0f};

    attribute_writers.texcoord_0->write(root.vertex_attributes.texcoord[usage_index], texcoord);
}

void Build_context::build_vertex_joint_indices(size_t usage_index)
{
    std::optional<GEO::vec4u> vertex_joint_indices = mesh_attributes.vertex_joint_indices(usage_index).try_get(mesh_vertex);
    GEO::vec4u joint_indices = vertex_joint_indices.has_value() ? vertex_joint_indices.value() : GEO::vec4u{0, 0, 0, 0};
    attribute_writers.joint_indices_0->write(root.vertex_attributes.joint_indices[usage_index], joint_indices);
}

void Build_context::build_vertex_joint_weights(size_t usage_index)
{
    std::optional<GEO::vec4f> vertex_joint_weights = mesh_attributes.vertex_joint_weights(usage_index).try_get(mesh_vertex);
    GEO::vec4f joint_weights = vertex_joint_weights.has_value() ? vertex_joint_weights.value() : GEO::vec4f{1.0f, 0.0f, 0.0f, 0.0f};
    attribute_writers.joint_weights_0->write(root.vertex_attributes.joint_weights[usage_index], joint_weights);
}

void Build_context::build_vertex_color(size_t usage_index)
{
    const std::optional<GEO::vec4f> corner_color = mesh_attributes.corner_color(usage_index).try_get(mesh_corner);
    const std::optional<GEO::vec4f> facet_color  = mesh_attributes.facet_color (usage_index).try_get(mesh_facet);
    const std::optional<GEO::vec4f> vertex_color = mesh_attributes.vertex_color(usage_index).try_get(mesh_vertex);

    GEO::vec4f color =
        corner_color.has_value() ? corner_color.value() :
        facet_color .has_value() ? facet_color .value() :
        vertex_color.has_value() ? vertex_color.value() : root.build_info.constant_color;

    attribute_writers.color_0->write(root.vertex_attributes.color[usage_index], color);
}

void Build_context::build_vertex_aniso_control()
{
    // X is used to modulate anisotropy level:
    //   0.0 -- Anisotropic
    //   1.0 -- Isotropic when approaching texcoord (0, 0)
    // Y is used for tangent space selection/control:
    //   0.0 -- Use geometry T and B (from vertex attribute
    //   1.0 -- Use T and B derived from texcoord
    std::optional<GEO::vec2f> corner_aniso_control = mesh_attributes.corner_aniso_control.try_get(mesh_corner);
    std::optional<GEO::vec2f> facet_aniso_control  = mesh_attributes.facet_aniso_control .try_get(mesh_facet);
    std::optional<GEO::vec2f> vertex_aniso_control = mesh_attributes.vertex_aniso_control.try_get(mesh_vertex);
    GEO::vec2f aniso_control = 
        corner_aniso_control.has_value() ? corner_aniso_control.value() :
        facet_aniso_control .has_value() ? facet_aniso_control .value() :
        vertex_aniso_control.has_value() ? vertex_aniso_control.value() : GEO::vec2f{1.0f, 1.0f};

    attribute_writers.aniso_control->write(root.vertex_attributes.aniso_control, aniso_control);
}

void Build_context::build_centroid_position()
{
    if (!root.build_info.primitive_types.centroid_points) {
        return;
    }

    std::optional<GEO::vec3f> facet_centroid = mesh_attributes.facet_centroid.try_get(mesh_facet);
    GEO::vec3f position = facet_centroid.has_value() 
        ? facet_centroid.value() 
        : mesh_facet_centerf(root.mesh, mesh_facet);

    attribute_writers.position->write(root.vertex_attributes.position, position);
}

void Build_context::build_centroid_normal()
{
    if (!root.build_info.primitive_types.centroid_points) {
        return;
    }

    if (root.vertex_attributes.normal.is_valid()) {
        std::optional<GEO::vec3f> facet_normal = mesh_attributes.facet_normal.try_get(mesh_facet);
        GEO::vec3f normal = facet_normal.has_value() ? facet_normal.value() : GEO::vec3f{0.0f, 1.0f, 0.0f};
        attribute_writers.normal->write(root.vertex_attributes.normal, normal);
    }
}

void Build_context::build_valency_edge_count()
{
    // TODO
    //// //if (root.attributes.valency_edge_count.is_valid()) 
    //// //{
    //// const unsigned int vertex_valency      = static_cast<unsigned int>(root.geometry.points.at(point_id).corner_count);
    //// const unsigned int polygone_edge_count = static_cast<unsigned int>(root.geometry.polygons.at(polygon_id).corner_count);
    //// const glm::uvec2 valency_edge_count{vertex_valency, polygone_edge_count};
    //// vertex_writer.write(root.attributes.valency_edge_count, valency_edge_count);
    //// //}
}

void Build_context::build_corner_point_index()
{
    //if (root.build_info.primitive_types.corner_points) {
    index_writer.write_corner(vertex_buffer_index);
    //}
}

void Build_context::build_triangle_fill_index()
{
    if (root.build_info.primitive_types.fill_triangles) {
        if (previous_index != first_index) {
            index_writer.write_triangle(first_index, previous_index, vertex_buffer_index);
            root.element_mappings.triangle_to_mesh_facet[primitive_index] = mesh_facet;
            ++primitive_index;
        }
    }

    previous_index = vertex_buffer_index;
}

auto Build_context::is_ready() const -> bool
{
    const bool ready = 
        !root.build_failed &&
        (root.buffer_mesh.index_buffer_range.count != 0) &&
        (root.buffer_mesh.vertex_buffer_ranges.front().count != 0);
    return ready;
}

void Build_context::build_polygon_fill()
{
    ERHE_PROFILE_FUNCTION();

    if (!is_ready()) {
        return;
    }

    // TODO mesh_attributes.corner_indices needs to be setup
    //      also if edge lines are wanted.

    vertex_buffer_index = 0;

    //const bool any_normal_feature = root.build_info.format.features.normal =
    //    root.build_info.format.features.normal      ||
    //    root.build_info.format.features.normal_flat ||
    //    root.build_info.format.features.normal_smooth;

    //const Polygon_id polygon_id_end = root.geometry.get_polygon_count();
    root.element_mappings.mesh_corner_to_vertex_buffer_index.resize(root.mesh.facet_corners.nb());
    root.element_mappings.mesh_vertex_to_vertex_buffer_index.resize(root.mesh.vertices.nb());

    const bool do_polygon_id           = root.vertex_attributes.id_vec4           .is_valid();
    const bool do_vertex_position      = root.vertex_attributes.position          .is_valid();
    const bool do_vertex_normal        = root.vertex_attributes.normal            .is_valid();
    const bool do_vertex_normal_smooth = root.vertex_attributes.normal_smooth     .is_valid();
    const bool do_vertex_normal_either = do_vertex_normal || do_vertex_normal_smooth;
    const bool do_vertex_tangent       = root.vertex_attributes.tangent           .is_valid();
    const bool do_vertex_bitangent     = root.vertex_attributes.bitangent         .is_valid();
    const bool do_vertex_texcoord_0    = root.vertex_attributes.texcoord[0]       .is_valid();
    const bool do_vertex_texcoord_1    = root.vertex_attributes.texcoord[1]       .is_valid();
    const bool do_vertex_color_0       = root.vertex_attributes.color[0]          .is_valid();
    const bool do_vertex_color_1       = root.vertex_attributes.color[1]          .is_valid();
    const bool do_aniso_control        = root.vertex_attributes.aniso_control     .is_valid();
    const bool do_joint_indices_0      = root.vertex_attributes.joint_indices[0]  .is_valid();
    const bool do_joint_indices_1      = root.vertex_attributes.joint_indices[1]  .is_valid();
    const bool do_joint_weights_0      = root.vertex_attributes.joint_weights[0]  .is_valid();
    const bool do_joint_weights_1      = root.vertex_attributes.joint_weights[1]  .is_valid();
    const bool do_vertex_valency       = root.vertex_attributes.valency_edge_count.is_valid();
    const bool do_corner_points        = root.build_info.primitive_types.corner_points;
    const bool do_tangent_frame = do_vertex_normal_either || do_vertex_tangent || do_vertex_bitangent;

    for (GEO::index_t facet : root.mesh.facets) {
        mesh_facet = facet;
        ERHE_PROFILE_SCOPE("polygon");
        first_index    = vertex_buffer_index;
        previous_index = first_index;

        mesh_attributes.facet_id.set(mesh_facet, vec3_from_index(mesh_facet));

        //const Polygon_corner_id polyon_corner_id_end = polygon.first_polygon_corner_id + polygon.corner_count;
        for (GEO::index_t corner : root.mesh.facets.corners(mesh_facet)) {
            mesh_corner = corner;
            ERHE_PROFILE_SCOPE("corner");
            mesh_vertex = root.mesh.facet_corners.vertex(mesh_corner);
            ERHE_VERIFY(mesh_vertex != GEO::NO_INDEX);

            root.element_mappings.mesh_corner_to_vertex_buffer_index[mesh_corner] = vertex_buffer_index;
            root.element_mappings.mesh_vertex_to_vertex_buffer_index[mesh_vertex] = vertex_buffer_index;

            if (do_polygon_id          ) build_polygon_id          ();

            if (do_tangent_frame       ) build_tangent_frame       ();
            if (do_vertex_position     ) build_vertex_position     ();
            if (do_vertex_normal_either) build_vertex_normal       (do_vertex_normal, do_vertex_normal_smooth);
            if (do_vertex_tangent      ) build_vertex_tangent      ();
            if (do_vertex_bitangent    ) build_vertex_bitangent    ();
            if (do_vertex_texcoord_0   ) build_vertex_texcoord     (0);
            if (do_vertex_texcoord_1   ) build_vertex_texcoord     (1);
            if (do_vertex_color_0      ) build_vertex_color        (0);
            if (do_vertex_color_1      ) build_vertex_color        (1);
            if (do_aniso_control       ) build_vertex_aniso_control();
            if (do_joint_indices_0     ) build_vertex_joint_indices(0);
            if (do_joint_indices_1     ) build_vertex_joint_indices(1);
            if (do_joint_weights_0     ) build_vertex_joint_weights(0);
            if (do_joint_weights_1     ) build_vertex_joint_weights(1);
            if (do_vertex_valency      ) build_valency_edge_count  ();

            // Indices
            if (do_corner_points) build_corner_point_index();
            build_triangle_fill_index();

            for (const std::unique_ptr<Vertex_buffer_writer>& vertex_writer : vertex_writers) {
                vertex_writer->next_vertex();
            }
            ++vertex_buffer_index;
        }
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

void Build_context::build_expanded_polygon_fill()
{
    ERHE_PROFILE_FUNCTION();

    if (!is_ready()) {
        return;
    }
    if (!root.build_info.primitive_types.fill_triangles_expanded) {
        return;
    }
    // Nothing allocated (no expanded vertex format supplied) -> skip.
    if (root.buffer_mesh.expanded_vertex_buffer_ranges.empty()) {
        return;
    }
    const erhe::dataformat::Vertex_format* expanded_format = root.build_info.buffer_info.expanded_vertex_format;
    if (expanded_format == nullptr) {
        return;
    }

    using namespace erhe::dataformat;

    // The packed wireframe attribute lives only in the expanded format.
    const Vertex_attribute_info wireframe_info{*expanded_format, Vertex_attribute_usage::custom, custom_attribute_wireframe};

    // Writers over the expanded vertex ranges (one per expanded-format stream).
    std::vector<std::unique_ptr<Vertex_buffer_writer>> expanded_writers;
    for (std::size_t stream_index = 0, stream_end = expanded_format->streams.size(); stream_index < stream_end; ++stream_index) {
        const Vertex_stream& stream = expanded_format->streams[stream_index];
        expanded_writers.push_back(
            std::make_unique<Vertex_buffer_writer>(
                *this,
                root.build_info.buffer_info.vertex_buffer_sink,
                stream_index,
                stream.stride,
                root.buffer_mesh.expanded_vertex_buffer_ranges[stream_index]
            )
        );
    }

    const auto expanded_writer_for = [&](Vertex_attribute_usage usage, std::size_t index) -> Vertex_buffer_writer* {
        const Attribute_stream as = expanded_format->find_attribute(usage, index);
        if (as.attribute == nullptr) {
            return nullptr;
        }
        const std::size_t stream_index = static_cast<std::size_t>(as.stream - expanded_format->streams.data());
        return expanded_writers.at(stream_index).get();
    };

    Vertex_buffer_writer* wireframe_writer = expanded_writer_for(Vertex_attribute_usage::custom, custom_attribute_wireframe);

    // Corner-cap data for the ID-buffer edge-line method: the triangle's three
    // corner object positions, replicated onto its three soup vertices so the
    // fill fragment can project them and cap real-edge corners (no Z-fight: the
    // cap is shaded by the fill fragment at the fill's own depth).
    const Vertex_attribute_info corner_pos_info[3] = {
        Vertex_attribute_info{*expanded_format, Vertex_attribute_usage::custom, custom_attribute_corner_position_0},
        Vertex_attribute_info{*expanded_format, Vertex_attribute_usage::custom, custom_attribute_corner_position_1},
        Vertex_attribute_info{*expanded_format, Vertex_attribute_usage::custom, custom_attribute_corner_position_2}
    };
    Vertex_buffer_writer* corner_pos_writer[3] = {
        expanded_writer_for(Vertex_attribute_usage::custom, custom_attribute_corner_position_0),
        expanded_writer_for(Vertex_attribute_usage::custom, custom_attribute_corner_position_1),
        expanded_writer_for(Vertex_attribute_usage::custom, custom_attribute_corner_position_2)
    };

    // Redirect the build_vertex_* helpers' writers to the expanded streams for
    // the duration of this pass; the per-attribute offsets in
    // root.vertex_attributes are valid because the expanded format mirrors the
    // shared fill streams (the wireframe attribute is in a separate stream).
    const Vertex_writers saved_attribute_writers = attribute_writers;
    attribute_writers.position           = expanded_writer_for(Vertex_attribute_usage::position,      0);
    attribute_writers.normal             = expanded_writer_for(Vertex_attribute_usage::normal,        normal_attribute);
    attribute_writers.normal_smooth      = expanded_writer_for(Vertex_attribute_usage::normal,        normal_attribute_smooth);
    attribute_writers.tangent            = expanded_writer_for(Vertex_attribute_usage::tangent,       0);
    attribute_writers.bitangent          = expanded_writer_for(Vertex_attribute_usage::bitangent,     0);
    attribute_writers.color_0            = expanded_writer_for(Vertex_attribute_usage::color,         0);
    attribute_writers.texcoord_0         = expanded_writer_for(Vertex_attribute_usage::tex_coord,     0);
    attribute_writers.joint_indices_0    = expanded_writer_for(Vertex_attribute_usage::joint_indices, 0);
    attribute_writers.joint_weights_0    = expanded_writer_for(Vertex_attribute_usage::joint_weights, 0);
    attribute_writers.id                 = expanded_writer_for(Vertex_attribute_usage::custom,        custom_attribute_id);
    attribute_writers.aniso_control      = expanded_writer_for(Vertex_attribute_usage::custom,        custom_attribute_aniso_control);
    attribute_writers.valency_edge_count = expanded_writer_for(Vertex_attribute_usage::custom,        custom_attribute_valency_edge_count);

    const bool do_polygon_id           = (attribute_writers.id              != nullptr) && root.vertex_attributes.id_vec4.is_valid();
    const bool do_vertex_position      = (attribute_writers.position        != nullptr);
    const bool do_vertex_normal        = (attribute_writers.normal          != nullptr) && root.vertex_attributes.normal.is_valid();
    const bool do_vertex_normal_smooth = (attribute_writers.normal_smooth   != nullptr) && root.vertex_attributes.normal_smooth.is_valid();
    const bool do_vertex_normal_either = do_vertex_normal || do_vertex_normal_smooth;
    const bool do_vertex_tangent       = (attribute_writers.tangent         != nullptr) && root.vertex_attributes.tangent.is_valid();
    const bool do_vertex_bitangent     = (attribute_writers.bitangent       != nullptr) && root.vertex_attributes.bitangent.is_valid();
    const bool do_vertex_texcoord_0    = (attribute_writers.texcoord_0      != nullptr) && root.vertex_attributes.texcoord[0].is_valid();
    const bool do_vertex_texcoord_1    = (attribute_writers.texcoord_0      != nullptr) && root.vertex_attributes.texcoord[1].is_valid();
    const bool do_vertex_color_0       = (attribute_writers.color_0         != nullptr) && root.vertex_attributes.color[0].is_valid();
    const bool do_vertex_color_1       = (attribute_writers.color_0         != nullptr) && root.vertex_attributes.color[1].is_valid();
    const bool do_aniso_control        = (attribute_writers.aniso_control   != nullptr) && root.vertex_attributes.aniso_control.is_valid();
    const bool do_joint_indices_0      = (attribute_writers.joint_indices_0 != nullptr) && root.vertex_attributes.joint_indices[0].is_valid();
    const bool do_joint_weights_0      = (attribute_writers.joint_weights_0 != nullptr) && root.vertex_attributes.joint_weights[0].is_valid();
    const bool do_tangent_frame        = do_vertex_normal_either || do_vertex_tangent || do_vertex_bitangent;

    // Per-facet boundary-edge set: an expanded-triangle edge is a real polygon
    // edge (drawn) only if it is a consecutive-corner pair of its facet; fan
    // diagonals are not, and are masked off in the shader.
    std::unordered_set<uint64_t> facet_boundary_edges;
    const auto edge_key = [](GEO::index_t a, GEO::index_t b) -> uint64_t {
        const GEO::index_t lo = (a < b) ? a : b;
        const GEO::index_t hi = (a < b) ? b : a;
        return (static_cast<uint64_t>(lo) << 32) | static_cast<uint64_t>(hi);
    };

    uint32_t expanded_vertex_index = 0;
    for (GEO::index_t facet : root.mesh.facets) {
        mesh_facet = facet;
        const GEO::index_t facet_corner_count = root.mesh.facets.nb_corners(facet);
        if (facet_corner_count < 3) {
            continue;
        }

        facet_boundary_edges.clear();
        for (GEO::index_t local = 0; local < facet_corner_count; ++local) {
            const GEO::index_t corner      = root.mesh.facets.corner(facet, local);
            const GEO::index_t next_corner = root.mesh.facets.corner(facet, (local + 1) % facet_corner_count);
            facet_boundary_edges.insert(
                edge_key(root.mesh.facet_corners.vertex(corner), root.mesh.facet_corners.vertex(next_corner))
            );
        }
        const auto is_boundary = [&](GEO::index_t a, GEO::index_t b) -> bool {
            return facet_boundary_edges.find(edge_key(a, b)) != facet_boundary_edges.end();
        };

        const GEO::index_t corner0 = root.mesh.facets.corner(facet, 0);
        const GEO::index_t vertex0 = root.mesh.facet_corners.vertex(corner0);
        // Fan triangulation matching build_triangle_fill_index:
        // triangles (corner0, corner_{k-1}, corner_k) for k = 2 .. n-1.
        for (GEO::index_t k = 2; k < facet_corner_count; ++k) {
            const GEO::index_t corner1 = root.mesh.facets.corner(facet, k - 1);
            const GEO::index_t corner2 = root.mesh.facets.corner(facet, k);
            const GEO::index_t vertex1 = root.mesh.facet_corners.vertex(corner1);
            const GEO::index_t vertex2 = root.mesh.facet_corners.vertex(corner2);

            // Bit b gates barycentric component b (b ~ 0 on the edge OPPOSITE
            // triangle vertex b): bit0 edge (v1,v2), bit1 edge (v2,v0), bit2 edge (v0,v1).
            const uint32_t edge_mask =
                (is_boundary(vertex1, vertex2) ? 0x1u : 0u) |
                (is_boundary(vertex2, vertex0) ? 0x2u : 0u) |
                (is_boundary(vertex0, vertex1) ? 0x4u : 0u);

            const GEO::index_t tri_corners [3] = { corner0, corner1, corner2 };
            const GEO::index_t tri_vertices[3] = { vertex0, vertex1, vertex2 };
            const GEO::vec3f   corner_positions[3] = {
                get_pointf(root.mesh.vertices, vertex0),
                get_pointf(root.mesh.vertices, vertex1),
                get_pointf(root.mesh.vertices, vertex2)
            };

            const uint32_t base = expanded_vertex_index;
            index_writer.write_expanded_triangle(base + 0u, base + 1u, base + 2u);

            for (uint32_t j = 0; j < 3u; ++j) {
                mesh_corner = tri_corners[j];
                mesh_vertex = tri_vertices[j];

                if (do_polygon_id)           build_polygon_id          ();
                if (do_tangent_frame)        build_tangent_frame       ();
                if (do_vertex_position)      build_vertex_position     ();
                if (do_vertex_normal_either) build_vertex_normal       (do_vertex_normal, do_vertex_normal_smooth);
                if (do_vertex_tangent)       build_vertex_tangent      ();
                if (do_vertex_bitangent)     build_vertex_bitangent    ();
                if (do_vertex_texcoord_0)    build_vertex_texcoord     (0);
                if (do_vertex_texcoord_1)    build_vertex_texcoord     (1);
                if (do_vertex_color_0)       build_vertex_color        (0);
                if (do_vertex_color_1)       build_vertex_color        (1);
                if (do_aniso_control)        build_vertex_aniso_control();
                if (do_joint_indices_0)      build_vertex_joint_indices(0);
                if (do_joint_weights_0)      build_vertex_joint_weights(0);

                if (wireframe_writer != nullptr) {
                    const uint32_t packed = j | (edge_mask << 2);
                    wireframe_writer->write(wireframe_info, packed);
                }

                for (uint32_t c = 0; c < 3u; ++c) {
                    if (corner_pos_writer[c] != nullptr) {
                        corner_pos_writer[c]->write(corner_pos_info[c], corner_positions[c]);
                    }
                }

                for (std::unique_ptr<Vertex_buffer_writer>& w : expanded_writers) {
                    w->next_vertex();
                }
                ++expanded_vertex_index;
            }
        }
    }

    // Restore the shared-fill attribute writers; expanded_writers flush on scope exit.
    attribute_writers = saved_attribute_writers;
}

void Build_context::build_edge_lines()
{
    if (!is_ready()) {
        return;
    }

    if (!root.build_info.primitive_types.edge_lines) {
        return;
    }

    // Edge-line vertex data feeds Content_wide_line_renderer's compute
    // expansion. Two vec4s per edge endpoint (position + per-edge face normal)
    // matching the compute shader's edge_line_vertex SSBO struct. position.w packs
    // the adjacent facet id; normal.w packs two per-face signs (its sign = the
    // interior-tangent sign, its magnitude = the edge-traversal winding tdir; see
    // the pack site below). The buffer range itself is allocated up front by
    // Build_context_root::allocate_edge_line_vertex_buffer().
    const bool has_edge_line_vertex_buffer = (root.buffer_mesh.edge_line_vertex_buffer_range.count > 0);
    std::vector<uint8_t> edge_line_vertex_data;
    const std::size_t    vertex_element_size = 8 * sizeof(float); // vec4 position + vec4 normal
    if (has_edge_line_vertex_buffer) {
        const std::size_t edge_count = root.mesh_info.edge_count;
        edge_line_vertex_data.resize(edge_count * 2 * vertex_element_size);
    }
    std::size_t edge_vertex_write_offset = 0;

    // Companion joint side buffer for skinned edge lines: uvec4 joint indices
    // + vec4 joint weights per endpoint. Allocated only when the mesh has
    // joint attributes (see Build_context_root::allocate_edge_line_joint_buffer).
    const bool has_edge_line_joint_buffer = (root.buffer_mesh.edge_line_joint_buffer_range.count > 0);
    std::vector<uint8_t> edge_line_joint_data;
    const std::size_t    joint_element_size = 4 * sizeof(uint32_t) + 4 * sizeof(float); // uvec4 + vec4
    if (has_edge_line_joint_buffer) {
        const std::size_t edge_count = root.mesh_info.edge_count;
        edge_line_joint_data.resize(edge_count * 2 * joint_element_size);
    }
    std::size_t edge_joint_write_offset = 0;

    // Per-edge surface frame for the tent wide-line method: each edge needs the
    // (up to two) facets adjacent to it so the compute shader can make each half
    // of the wide-line ribbon coplanar with its own face. Build an edge -> facets
    // adjacency from the raw GEO::Mesh by walking every facet's consecutive
    // corner pairs (same construction as erhe::geometry::Geometry's
    // m_edge_to_facets). Cold path: runs once per mesh at build time.
    // Per shared edge: the (up to two) adjacent facets AND the direction each
    // facet walks the edge in its own corner loop (forward = lo -> hi). That
    // traversal direction is the face's winding at the edge; the wide-line compute
    // shader needs it to decide front/back the SAME way the rasterizer culls the
    // polygon fill (projected signed area), instead of a normal-vs-view dot that
    // assumes outward normals and misclassifies at grazing silhouettes.
    class Edge_facets
    {
    public:
        GEO::index_t facet  [2]{GEO::NO_INDEX, GEO::NO_INDEX};
        bool         forward[2]{false, false}; // facet[i] walks the edge lo -> hi
    };
    std::unordered_map<uint64_t, Edge_facets> edge_to_facets;
    if (has_edge_line_vertex_buffer) {
        for (GEO::index_t facet : root.mesh.facets) {
            const GEO::index_t facet_corner_count = root.mesh.facets.nb_corners(facet);
            for (GEO::index_t local = 0; local < facet_corner_count; ++local) {
                const GEO::index_t corner      = root.mesh.facets.corner(facet, local);
                const GEO::index_t next_corner = root.mesh.facets.corner(facet, (local + 1) % facet_corner_count);
                const GEO::index_t va          = root.mesh.facet_corners.vertex(corner);
                const GEO::index_t vb          = root.mesh.facet_corners.vertex(next_corner);
                const GEO::index_t lo          = (va < vb) ? va : vb;
                const GEO::index_t hi          = (va < vb) ? vb : va;
                const uint64_t     key         = (static_cast<uint64_t>(lo) << 32) | static_cast<uint64_t>(hi);
                const bool         forward     = (va == lo); // this facet walks lo -> hi
                Edge_facets&       facets      = edge_to_facets.try_emplace(key, Edge_facets{}).first->second;
                if (facets.facet[0] == GEO::NO_INDEX) {
                    facets.facet[0]   = facet;
                    facets.forward[0] = forward;
                } else if ((facets.facet[1] == GEO::NO_INDEX) && (facet != facets.facet[0])) {
                    facets.facet[1]   = facet;
                    facets.forward[1] = forward;
                }
            }
        }
    }

    for (GEO::index_t mesh_edge : root.mesh.edges) {
        const GEO::index_t mesh_vertex_a  = root.mesh.edges.vertex(mesh_edge, 0);
        const GEO::index_t mesh_vertex_b  = root.mesh.edges.vertex(mesh_edge, 1);
        const uint32_t     vertex_index_a = root.element_mappings.mesh_vertex_to_vertex_buffer_index[mesh_vertex_a];
        const uint32_t     vertex_index_b = root.element_mappings.mesh_vertex_to_vertex_buffer_index[mesh_vertex_b];
        index_writer.write_edge(vertex_index_a, vertex_index_b);

        if (has_edge_line_vertex_buffer) {
            const GEO::vec3f pos_a = get_pointf(root.mesh.vertices, mesh_vertex_a);
            const GEO::vec3f pos_b = get_pointf(root.mesh.vertices, mesh_vertex_b);

            // Per-edge surface frame for the tent wide-line method. Endpoint 0
            // carries face A's geometric normal plus the interior-tangent sign
            // (in normal.w); endpoint 1 carries face B's normal. Both normals are
            // object-local; compute_before_content_line.comp lifts them to world
            // and builds the two coplanar half-quads. Boundary edge (one facet)
            // -> normal_b = normal_a (single-plane hug). Facet-less edge -> fall
            // back to the smooth/vertex normal so the (toggle-off) simple-quad
            // path still has a usable normal. Mirrors compute_edge_surface_frame()
            // in mesh_component_selection_tool.cpp.
            const GEO::vec3f fallback_normal{0.0f, 1.0f, 0.0f};
            const std::optional<GEO::vec3f> vertex_normal_a = mesh_attributes.vertex_normal.try_get(mesh_vertex_a);
            const std::optional<GEO::vec3f> vertex_normal_b = mesh_attributes.vertex_normal.try_get(mesh_vertex_b);
            const std::optional<GEO::vec3f> smooth_normal_a = mesh_attributes.vertex_normal_smooth.try_get(mesh_vertex_a);
            const std::optional<GEO::vec3f> smooth_normal_b = mesh_attributes.vertex_normal_smooth.try_get(mesh_vertex_b);
            const GEO::vec3f fb_normal_a = smooth_normal_a.has_value() ? smooth_normal_a.value() : vertex_normal_a.has_value() ? vertex_normal_a.value() : fallback_normal;
            const GEO::vec3f fb_normal_b = smooth_normal_b.has_value() ? smooth_normal_b.value() : vertex_normal_b.has_value() ? vertex_normal_b.value() : fallback_normal;

            const GEO::index_t edge_lo   = (mesh_vertex_a < mesh_vertex_b) ? mesh_vertex_a : mesh_vertex_b;
            const GEO::index_t edge_hi   = (mesh_vertex_a < mesh_vertex_b) ? mesh_vertex_b : mesh_vertex_a;
            const uint64_t     edge_key  = (static_cast<uint64_t>(edge_lo) << 32) | static_cast<uint64_t>(edge_hi);
            GEO::index_t       facet_a   = GEO::NO_INDEX;
            GEO::index_t       facet_b   = GEO::NO_INDEX;
            bool               fwd_a     = false;
            bool               fwd_b     = false;
            const auto         facets_it = edge_to_facets.find(edge_key);
            if (facets_it != edge_to_facets.end()) {
                facet_a = facets_it->second.facet[0];
                facet_b = facets_it->second.facet[1];
                fwd_a   = facets_it->second.forward[0];
                fwd_b   = facets_it->second.forward[1];
            }
            // Edge-traversal winding tdir per face, relative to the SSBO endpoint
            // order (mesh_vertex_a -> mesh_vertex_b): +1 if the face walks the edge
            // in that same order, -1 if reversed. (forward[] is lo -> hi; the SSBO
            // order is lo -> hi iff mesh_vertex_a < mesh_vertex_b.)
            const bool a_to_b_is_lo_to_hi = (mesh_vertex_a < mesh_vertex_b);

            GEO::vec3f normal_a = fb_normal_a;
            GEO::vec3f normal_b = fb_normal_b;
            float      sign_a   = 0.0f;
            float      sign_b   = 0.0f;
            float      tdir_a   = 0.0f; // edge-traversal winding (+/-1), 0 if facet-less
            float      tdir_b   = 0.0f;
            if (facet_a != GEO::NO_INDEX) {
                normal_a = GEO::normalize(mesh_facet_normalf(root.mesh, facet_a));
                const GEO::vec3f edge_dir      = pos_b - pos_a;
                const GEO::vec3f edge_mid      = 0.5f * (pos_a + pos_b);
                // Interior-tangent sign for face A: sign so that
                // sign_a * cross(normal_a, edge_dir) points from the edge
                // midpoint toward face A's centroid.
                const GEO::vec3f tangent_a     = GEO::cross(normal_a, edge_dir);
                const GEO::vec3f center_a      = mesh_facet_centerf(root.mesh, facet_a);
                const GEO::vec3f to_interior_a = center_a - edge_mid;
                sign_a = (GEO::dot(tangent_a, to_interior_a) >= 0.0f) ? 1.0f : -1.0f;
                tdir_a = (fwd_a == a_to_b_is_lo_to_hi) ? 1.0f : -1.0f;
                if (facet_b != GEO::NO_INDEX) {
                    normal_b = GEO::normalize(mesh_facet_normalf(root.mesh, facet_b));
                    // Interior-tangent sign for face B, computed the SAME way from
                    // face B's own centroid. The compute shader needs each face's
                    // true interior side to place its half-quad: at a silhouette
                    // edge both interiors project to the same screen side, which
                    // only a per-face sign (not a heuristic) gets right.
                    const GEO::vec3f tangent_b     = GEO::cross(normal_b, edge_dir);
                    const GEO::vec3f center_b      = mesh_facet_centerf(root.mesh, facet_b);
                    const GEO::vec3f to_interior_b = center_b - edge_mid;
                    sign_b = (GEO::dot(tangent_b, to_interior_b) >= 0.0f) ? 1.0f : -1.0f;
                    tdir_b = (fwd_b == a_to_b_is_lo_to_hi) ? 1.0f : -1.0f;
                } else {
                    // Boundary edge (one facet): face B reuses face A's plane on the
                    // opposite interior side, so the two half-quads form a full ribbon.
                    normal_b = normal_a;
                    sign_b   = -sign_a;
                    tdir_b   = -tdir_a;
                }
            }

            // Per-endpoint adjacent facet id for the ID-buffer edge-line method
            // (Content_wide_line_renderer id mode): endpoint 0 carries face A's
            // facet index, endpoint 1 carries face B's (falling back to A on a
            // boundary edge / to 0 on a facet-less edge). Stored in the otherwise
            // spare position.w slot so the edge SSBO struct size is unchanged; the
            // (toggle-off) color path ignores position.w. These facet indices are
            // the SAME index space build_polygon_id() writes into the fill mesh's
            // custom_attribute_id, so the fill fragment can match face for face.
            const uint32_t id_a = (facet_a != GEO::NO_INDEX) ? static_cast<uint32_t>(facet_a) : 0u;
            const uint32_t id_b = (facet_b != GEO::NO_INDEX) ? static_cast<uint32_t>(facet_b)
                                : (facet_a != GEO::NO_INDEX) ? static_cast<uint32_t>(facet_a)
                                : 0u;

            // Pack BOTH per-face signs into normal.w: its SIGN carries the
            // interior-tangent sign (sign_a/sign_b, +/-1), its MAGNITUDE carries the
            // edge-traversal winding tdir (tdir > 0 -> 1, tdir < 0 -> 2). Exact in
            // fp32 (values are +/-1 or +/-2). The tent wide-line path decodes both;
            // the simple-quad and geometry-shader backends read only normal.xyz, so
            // the packed .w does not affect them. A facet-less edge keeps sign 0 ->
            // packed 0 -> the shader's degenerate-frame path.
            const float packed_w_a = sign_a * ((tdir_a < 0.0f) ? 2.0f : 1.0f);
            const float packed_w_b = sign_b * ((tdir_b < 0.0f) ? 2.0f : 1.0f);
            const float data_a[8] = { pos_a.x, pos_a.y, pos_a.z, static_cast<float>(id_a), normal_a.x, normal_a.y, normal_a.z, packed_w_a };
            const float data_b[8] = { pos_b.x, pos_b.y, pos_b.z, static_cast<float>(id_b), normal_b.x, normal_b.y, normal_b.z, packed_w_b };
            memcpy(edge_line_vertex_data.data() + edge_vertex_write_offset, data_a, vertex_element_size);
            edge_vertex_write_offset += vertex_element_size;
            memcpy(edge_line_vertex_data.data() + edge_vertex_write_offset, data_b, vertex_element_size);
            edge_vertex_write_offset += vertex_element_size;
        }

        if (has_edge_line_joint_buffer) {
            const GEO::vec4u fallback_indices{0u, 0u, 0u, 0u};
            const GEO::vec4f fallback_weights{1.0f, 0.0f, 0.0f, 0.0f};
            const std::optional<GEO::vec4u> joint_indices_a = mesh_attributes.vertex_joint_indices_0.try_get(mesh_vertex_a);
            const std::optional<GEO::vec4u> joint_indices_b = mesh_attributes.vertex_joint_indices_0.try_get(mesh_vertex_b);
            const std::optional<GEO::vec4f> joint_weights_a = mesh_attributes.vertex_joint_weights_0.try_get(mesh_vertex_a);
            const std::optional<GEO::vec4f> joint_weights_b = mesh_attributes.vertex_joint_weights_0.try_get(mesh_vertex_b);
            const GEO::vec4u indices_a = joint_indices_a.has_value() ? joint_indices_a.value() : fallback_indices;
            const GEO::vec4u indices_b = joint_indices_b.has_value() ? joint_indices_b.value() : fallback_indices;
            const GEO::vec4f weights_a = joint_weights_a.has_value() ? joint_weights_a.value() : fallback_weights;
            const GEO::vec4f weights_b = joint_weights_b.has_value() ? joint_weights_b.value() : fallback_weights;

            const uint32_t idx_a[4] = { indices_a.x, indices_a.y, indices_a.z, indices_a.w };
            const uint32_t idx_b[4] = { indices_b.x, indices_b.y, indices_b.z, indices_b.w };
            const float    wgt_a[4] = { weights_a.x, weights_a.y, weights_a.z, weights_a.w };
            const float    wgt_b[4] = { weights_b.x, weights_b.y, weights_b.z, weights_b.w };
            memcpy(edge_line_joint_data.data() + edge_joint_write_offset, idx_a, sizeof(idx_a));
            edge_joint_write_offset += sizeof(idx_a);
            memcpy(edge_line_joint_data.data() + edge_joint_write_offset, wgt_a, sizeof(wgt_a));
            edge_joint_write_offset += sizeof(wgt_a);
            memcpy(edge_line_joint_data.data() + edge_joint_write_offset, idx_b, sizeof(idx_b));
            edge_joint_write_offset += sizeof(idx_b);
            memcpy(edge_line_joint_data.data() + edge_joint_write_offset, wgt_b, sizeof(wgt_b));
            edge_joint_write_offset += sizeof(wgt_b);
        }
    }

    if (has_edge_line_vertex_buffer && (edge_vertex_write_offset > 0)) {
        root.build_info.buffer_info.vertex_buffer_sink.enqueue_vertex_data(
            root.buffer_mesh.edge_line_vertex_buffer_range,
            std::move(edge_line_vertex_data)
        );
    }

    if (has_edge_line_joint_buffer && (edge_joint_write_offset > 0)) {
        root.build_info.buffer_info.vertex_buffer_sink.enqueue_vertex_data(
            root.buffer_mesh.edge_line_joint_buffer_range,
            std::move(edge_line_joint_data)
        );
    }
}

void Build_context::build_centroid_points()
{
    if (!is_ready()) {
        return;
    }

    if (!root.build_info.primitive_types.centroid_points) {
        return;
    }

    for (GEO::index_t facet : root.mesh.facets) {
        mesh_facet = facet;
        build_centroid_position();
        build_centroid_normal();

        index_writer.write_centroid(vertex_buffer_index);
        for (const std::unique_ptr<Vertex_buffer_writer>& vertex_writer : vertex_writers) {
            vertex_writer->next_vertex();
        }
        ++vertex_buffer_index;
    }
}

void Build_context_root::allocate_index_range(const Primitive_type primitive_type, const std::size_t index_count, Index_range& out_range)
{
    out_range.primitive_type = primitive_type;
    out_range.first_index    = next_index_range_start;
    out_range.index_count    = index_count;
    next_index_range_start += index_count;

    // If index buffer has not yet been allocated, no check for enough room for index range
    ERHE_VERIFY(
        (buffer_mesh.index_buffer_range.count == 0) ||
        (next_index_range_start <= buffer_mesh.index_buffer_range.count)
    );
}

auto build_buffer_mesh(
    Buffer_mesh&      buffer_mesh,
    const GEO::Mesh&  source_mesh,
    const Build_info& build_info,
    Element_mappings& element_mappings,
    Normal_style      normal_style
) -> bool
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(element_mappings.triangle_to_mesh_facet.empty());
    ERHE_VERIFY(element_mappings.mesh_corner_to_vertex_buffer_index.empty());
    ERHE_VERIFY(element_mappings.mesh_vertex_to_vertex_buffer_index.empty());
    Primitive_builder builder{buffer_mesh, source_mesh, build_info, element_mappings, normal_style};
    return builder.build();
}

} // namespace erhe::primitive
