// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_primitive/primitive_builder.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_primitive/buffer_writer.hpp"
#include "erhe_primitive/index_range.hpp"
#include "erhe_primitive/mesh_optimizer.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/primitive_log.hpp"
#include "erhe_log/log.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_profile/profile.hpp"

#include <fmt/format.h>
#include <meshoptimizer.h>
#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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

namespace {

// Attribute storage conversions the optimized build knows how to perform when
// gathering the staged (base format) bytes into the optimized format. Anything
// not listed makes take_optimizable_snapshot() DECLINE - no optimized variant -
// rather than ship bytes no encoder wrote.
//
// Position is not here: it has its own encode branch (the AABB affine is not a
// pure format conversion). See doc/meshoptimizer-attribute-encodings-plan.md.
//
// The soup path does not consult this list - Primitive_shape::make_buffer_mesh()
// routes every non-position attribute through erhe::dataformat::convert()
// unconditionally, because it also serves base builds whose source soup format
// is whatever the asset happened to carry. So every pair added here must ALSO be
// one convert() actually implements, or the two paths would disagree silently.
[[nodiscard]] auto is_supported_attribute_conversion(
    const erhe::dataformat::Format source_format,
    const erhe::dataformat::Format optimized_format
) -> bool
{
    using Format = erhe::dataformat::Format;
    if (source_format == optimized_format) {
        return true;
    }
    // Vertex colors are LDR, so unorm8 per channel. vec4 in GLSL either way,
    // hardware normalized: no decode, no shader variant axis.
    if ((source_format == Format::format_32_vec4_float) && (optimized_format == Format::format_8_vec4_unorm)) {
        return true;
    }
    // Lightmap UVs (texcoord channel 2) are in [0, 1] by construction, so unorm16
    // with no affine. vec2 in GLSL either way: no decode.
    if ((source_format == Format::format_32_vec2_float) && (optimized_format == Format::format_16_vec2_unorm)) {
        return true;
    }
    return false;
}

} // anonymous namespace

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
    // Deliberately no allocation here - see allocate_buffers(), which runs
    // after the build.
}

void Build_context_root::allocate_buffers()
{
    // One atomic multi-pool allocation transaction per mesh - see
    // buffer_mesh_allocation_mutex(). The five allocations move together so
    // that guarantee is preserved. The data writes are already staged in CPU
    // memory by now and go to the mesh's own ranges, so they need no lock.
    const std::lock_guard<std::mutex> allocation_lock{buffer_mesh_allocation_mutex()};
    allocate_vertex_buffers        ();
    allocate_edge_line_vertex_buffer();
    allocate_edge_line_joint_buffer ();
    allocate_expanded_fill_buffers  ();
    allocate_index_buffer          ();

    // allocate_index_range() handed out per-type sub-ranges long before this,
    // so its own bounds check against the index allocation could not run. Do it
    // here instead, now that the allocation exists: a sub-range past the end
    // would have the build write over another mesh's indices.
    if (!build_failed) {
        ERHE_VERIFY(next_index_range_start <= buffer_mesh.index_buffer_range.count);
    }
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
    vertex_attributes.texcoord     .push_back(Vertex_attribute_info{vertex_format, Vertex_attribute_usage::tex_coord,     2});
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

    // Lockstep invariant: one indirect-draw vertexOffset (from stream 0) is
    // applied to every binding, so byte_offset / stride and the block index
    // must match across all streams (see buffer_pool.hpp). Fail the build
    // loudly instead of rendering non-position attributes from wrong offsets.
    if (!buffer_mesh.vertex_buffer_ranges.empty() && (total_vertex_count > 0)) {
        const Buffer_range& range_0     = buffer_mesh.vertex_buffer_ranges.front();
        const std::size_t   base_vertex = range_0.byte_offset / range_0.element_size;
        for (std::size_t stream = 1, stream_end = buffer_mesh.vertex_buffer_ranges.size(); stream < stream_end; ++stream) {
            const Buffer_range& range = buffer_mesh.vertex_buffer_ranges[stream];
            if (
                ((range.byte_offset % range.element_size) != 0)           ||
                ((range.byte_offset / range.element_size) != base_vertex) ||
                (range.buffer_id != range_0.buffer_id)
            ) {
                log_primitive_builder->error(
                    "allocate_vertex_buffers(): vertex stream allocations out of lockstep "
                    "(stream 0: pool {} buffer {} byte_offset {} stride {}; "
                    "stream {}: pool {} buffer {} byte_offset {} stride {}) - mesh build failed",
                    range_0.pool_id, range_0.buffer_id, range_0.byte_offset, range_0.element_size,
                    stream,
                    range.pool_id, range.buffer_id, range.byte_offset, range.element_size
                );
                build_failed = true;
                return;
            }
        }
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
    // Same lockstep invariant as allocate_vertex_buffers(), for the expanded
    // stream set (expanded_base_vertex is computed from expanded stream 0).
    if (!buffer_mesh.expanded_vertex_buffer_ranges.empty()) {
        const Buffer_range& range_0     = buffer_mesh.expanded_vertex_buffer_ranges.front();
        const std::size_t   base_vertex = range_0.byte_offset / range_0.element_size;
        for (std::size_t stream = 1, stream_end = buffer_mesh.expanded_vertex_buffer_ranges.size(); stream < stream_end; ++stream) {
            const Buffer_range& range = buffer_mesh.expanded_vertex_buffer_ranges[stream];
            if (
                ((range.byte_offset % range.element_size) != 0)           ||
                ((range.byte_offset / range.element_size) != base_vertex) ||
                (range.buffer_id != range_0.buffer_id)
            ) {
                log_primitive_builder->error(
                    "allocate_expanded_fill_buffers(): vertex stream allocations out of lockstep "
                    "(stream 0: pool {} buffer {} byte_offset {} stride {}; "
                    "stream {}: pool {} buffer {} byte_offset {} stride {}) - mesh build failed",
                    range_0.pool_id, range_0.buffer_id, range_0.byte_offset, range_0.element_size,
                    stream,
                    range.pool_id, range.buffer_id, range.byte_offset, range.element_size
                );
                build_failed = true;
                return;
            }
        }
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
    ERHE_PROFILE_FUNCTION();

    const Mesh_point_source point_source{mesh};
    erhe::math::calculate_bounding_volume(point_source, buffer_mesh.bounding_box, buffer_mesh.bounding_sphere);
}

void Build_context_root::calculate_joint_bounding_volumes(erhe::geometry::Mesh_attributes& mesh_attributes)
{
    ERHE_PROFILE_FUNCTION();

    // Rest-pose bounds per joint, so a GPU-skinned mesh can be bounded in world
    // space from the joint transforms alone (see Buffer_mesh::joint_bounding_boxes).
    // A vertex is included in the box of every joint that influences it; the
    // posed vertex is then a convex combination of its per-joint images, hence
    // inside the union of the transformed boxes.
    buffer_mesh.joint_bounding_boxes.clear();

    // Same guard as allocate_edge_line_joint_buffer(): skip the per-vertex walk
    // entirely for the (common) unskinned mesh.
    const GEO::AttributesManager& vertex_attrs = mesh.vertices.attributes();
    if (
        !vertex_attrs.is_defined(erhe::geometry::c_joint_indices_0) ||
        !vertex_attrs.is_defined(erhe::geometry::c_joint_weights_0)
    ) {
        return;
    }

    const auto include = [this](const uint32_t joint_index, const glm::vec3 position) {
        if (joint_index >= buffer_mesh.joint_bounding_boxes.size()) {
            buffer_mesh.joint_bounding_boxes.resize(joint_index + 1);
        }
        buffer_mesh.joint_bounding_boxes[joint_index].include(position);
    };

    for (GEO::index_t vertex : mesh.vertices) {
        const glm::vec3 position = to_glm_vec3(get_pointf(mesh.vertices, vertex));
        for (std::size_t usage_index = 0; usage_index < 2; ++usage_index) {
            const std::optional<GEO::vec4u> joint_indices = mesh_attributes.vertex_joint_indices(usage_index).try_get(vertex);
            const std::optional<GEO::vec4f> joint_weights = mesh_attributes.vertex_joint_weights(usage_index).try_get(vertex);
            if (!joint_indices.has_value() || !joint_weights.has_value()) {
                continue;
            }
            for (std::size_t i = 0; i < 4; ++i) {
                // A zero weight contributes nothing to the posed position, so
                // that joint must not be allowed to inflate the bound.
                if (joint_weights.value()[static_cast<GEO::index_t>(i)] == 0.0f) {
                    continue;
                }
                include(joint_indices.value()[static_cast<GEO::index_t>(i)], position);
            }
        }
    }
}

Primitive_builder::Primitive_builder(
    Buffer_mesh&       buffer_mesh,
    const GEO::Mesh&   mesh,
    const Build_info&  build_info,
    Element_mappings&  element_mappings,
    const Normal_style normal_style,
    std::string_view   name,
    const bool         build_optimized_variant
)
    : m_buffer_mesh            {buffer_mesh}
    , m_mesh                   {mesh}
    , m_build_info             {build_info}
    , m_element_mappings       {element_mappings}
    , m_normal_style           {normal_style}
    , m_name                   {name}
    , m_build_optimized_variant{build_optimized_variant}
{
}

auto Primitive_builder::take_optimized_render_shape() -> std::shared_ptr<Primitive_render_shape>
{
    return std::move(m_optimized_render_shape);
}

auto Primitive_builder::build() -> bool
{
    ERHE_PROFILE_FUNCTION();

    log_primitive_builder->trace("Primitive_builder::build(normal_style = {})", c_str(m_normal_style));

    // The optimized variant is built from these AFTER build_context is gone -
    // its writers flush on destruction, and the second build allocates from the
    // same pools, so the two transactions are kept apart.
    std::vector<Mesh_optimize_stream> optimize_streams;
    std::vector<uint32_t>             optimize_fill_indices;

    {
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

    // Snapshot BEFORE the writers are bound and flushed: this is the last point
    // at which the staged bytes are still here to be read.
    if (m_build_optimized_variant && m_build_info.buffer_info.optimize_meshes) {
        erhe::log::set_breadcrumb("primitive: take_optimizable_snapshot");
        static_cast<void>(build_context.take_optimizable_snapshot(optimize_streams, optimize_fill_indices));
    }

    // Allocate now that the final vertex and index counts are known, and hand
    // every writer its destination. The writers flush when build_context goes
    // out of scope just below; without a destination they drop their staged
    // bytes, so a failed allocation cannot write into another mesh's range.
    erhe::log::set_breadcrumb("primitive: allocate_and_bind_writers");
    if (!build_context.allocate_and_bind_writers()) {
        log_primitive_builder->debug("Primitive_builder::build() aborted: buffer allocation failed");
        return false;
    }
    } // build_context flushes here

    if (!optimize_streams.empty()) {
        erhe::log::set_breadcrumb("primitive: optimized variant");
        m_optimized_render_shape = make_optimized_render_shape_from_staged_build(
            std::move(optimize_streams),
            std::move(optimize_fill_indices),
            m_element_mappings,
            m_buffer_mesh,
            m_build_info.buffer_info,
            m_name
        );
    }
    return true;
}

[[nodiscard]] auto Build_context::get_attribute_writer(erhe::dataformat::Vertex_attribute_usage usage, std::size_t index) -> Vertex_buffer_writer*
{
    ERHE_PROFILE_FUNCTION();

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
    ERHE_PROFILE_FUNCTION();

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
                sink_stream.stride,
                root.total_vertex_count
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

    // Expanded solid-wireframe writers are created here, not in
    // build_expanded_polygon_fill(), because a writer flushes in its destructor
    // and its destination range only exists after the build. One expanded
    // vertex per fill-triangle corner.
    const erhe::dataformat::Vertex_format* expanded_format = build_info.buffer_info.expanded_vertex_format;
    if (build_info.primitive_types.fill_triangles_expanded && (expanded_format != nullptr)) {
        for (std::size_t stream_index = 0, stream_end = expanded_format->streams.size(); stream_index < stream_end; ++stream_index) {
            const erhe::dataformat::Vertex_stream& expanded_stream = expanded_format->streams[stream_index];
            expanded_vertex_writers.push_back(
                std::make_unique<Vertex_buffer_writer>(
                    *this,
                    build_info.buffer_info.vertex_buffer_sink,
                    stream_index,
                    expanded_stream.stride,
                    root.mesh_info.index_count_fill_triangles
                )
            );
        }
    }

    root.calculate_bounding_volume();
    root.calculate_joint_bounding_volumes(mesh_attributes);

    // The AABB is known now, and every position write happens after this point
    // (build_polygon_fill / build_expanded_polygon_fill / build_edge_lines /
    // build_centroid_points all run later), so the encoding pack needs no extra
    // pass over the mesh.
    root.position_encoding = erhe::dataformat::get_vertex_position_encoding(&root.vertex_format);
    if (root.position_encoding != erhe::dataformat::Vertex_position_encoding::passthrough) {
        // Same affine the primitive buffer, the BLAS build and the shaders use;
        // get_position_quantization() in erhe_scene_renderer is the C++ mirror.
        // Encoder and decoder must agree exactly, so a degenerate axis has to
        // encode to exactly 0 and decode back to the centre - which it does,
        // because the epsilon is only ever multiplied by zero.
        constexpr float epsilon = 1e-6f;
        const erhe::math::Aabb& bounding_box = root.buffer_mesh.bounding_box;
        if (bounding_box.is_valid()) {
            const glm::vec3 center      = bounding_box.center();
            const glm::vec3 half_extent = 0.5f * bounding_box.diagonal();
            const glm::vec3 scale       = glm::max(half_extent, glm::vec3{epsilon});
            root.position_encode_center    = GEO::vec3f{center.x, center.y, center.z};
            root.position_encode_inv_scale = GEO::vec3f{1.0f / scale.x, 1.0f / scale.y, 1.0f / scale.z};
        }
    }
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

void Build_context::write_position(const Vertex_attribute_info& info, const GEO::vec3f position)
{
    if (root.position_encoding == erhe::dataformat::Vertex_position_encoding::passthrough) {
        attribute_writers.position->write(info, position);
        return;
    }
    // Normalize into the AABB. The clamp only matters for values outside the box,
    // which calculate_bounding_volume() bounds away for mesh vertices - facet
    // centroids and expanded corners are inside by construction - but write_low3()
    // clamps silently anyway and convert() asserts, so be explicit here. The clamp
    // is also what makes meshopt_quantizeSnorm() below bit-identical to
    // erhe::dataformat::float_to_snorm16(): both scale by 32767 and round half away
    // from zero, and they can only differ on input outside [-1, 1].
    const GEO::vec3f biased = position - root.position_encode_center;
    const GEO::vec3f scaled{
        biased.x * root.position_encode_inv_scale.x,
        biased.y * root.position_encode_inv_scale.y,
        biased.z * root.position_encode_inv_scale.z
    };
    const GEO::vec3f encoded{
        std::clamp(scaled.x, -1.0f, 1.0f),
        std::clamp(scaled.y, -1.0f, 1.0f),
        std::clamp(scaled.z, -1.0f, 1.0f)
    };
    attribute_writers.position->write_snorm16x3(
        info,
        static_cast<int16_t>(meshopt_quantizeSnorm(encoded.x, 16)),
        static_cast<int16_t>(meshopt_quantizeSnorm(encoded.y, 16)),
        static_cast<int16_t>(meshopt_quantizeSnorm(encoded.z, 16))
    );
}

void Build_context::build_vertex_position()
{
    ERHE_PROFILE_FUNCTION();

    v_position = get_pointf(root.mesh.vertices, mesh_vertex);

    ERHE_VERIFY(std::isfinite(v_position.x) && std::isfinite(v_position.y) && std::isfinite(v_position.z));
    write_position(root.vertex_attributes.position, v_position);

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

    write_position(root.vertex_attributes.position, position);
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

auto Build_context::take_optimizable_snapshot(
    std::vector<Mesh_optimize_stream>& out_streams,
    std::vector<uint32_t>&             out_fill_indices
) -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (
        root.build_failed ||
        !root.build_info.primitive_types.fill_triangles ||
        (root.build_info.buffer_info.optimized_vertex_format == nullptr)
    ) {
        return false;
    }
    const std::size_t corner_count = root.mesh_info.vertex_count_corners;
    const std::size_t index_count  = root.mesh_info.index_count_fill_triangles;
    if ((corner_count == 0) || (index_count == 0)) {
        return false;
    }
    // build_triangle_fill_index() skips a degenerate fan start, so the emitted
    // count can in principle fall short of what the index range was sized for -
    // and then triangle_to_mesh_facet would describe more triangles than the
    // index buffer holds, which compose_element_mappings() has no way to line
    // up. Decline rather than guess.
    if (index_writer.triangle_indices_written != index_count) {
        log_primitive_builder->trace(
            "mesh optimize: {} fill indices written but {} expected; no optimized variant",
            index_writer.triangle_indices_written,
            index_count
        );
        return false;
    }

    // The staged bytes are in the SOURCE format; the variant is built in the
    // optimized one, which is the same content attributes minus the per-corner
    // facet id. So this is a per-attribute gather rather than a block copy:
    // for every attribute the optimized format keeps, copy its bytes from
    // wherever the source format put them.
    //
    // Dropping the id is not cosmetic. It is per FACET, and welding merges
    // corners across facets, so no single value describes the merged vertex -
    // and leaving it in would make every corner unique across facet boundaries
    // to the bitwise weld compare, merging nothing at all. Losing the attribute
    // also means the variant cannot be used for ID rendering even by mistake.
    //
    // Only the corner prefix: build_centroid_points() appends one vertex per
    // facet after the corners, and those belong to the centroid-point draw the
    // optimized variant does not carry.
    const erhe::dataformat::Vertex_format& optimized_format = *root.build_info.buffer_info.optimized_vertex_format;
    if (optimized_format.streams.size() != root.vertex_format.streams.size()) {
        // The two formats are meant to differ only in attributes, never in
        // stream count - a mismatch means the sink offered an unrelated format.
        return false;
    }

    // The optimized format is the only place position quantization applies: the
    // staged (base) position is always float3, and when the optimized format
    // stores format_16_vec3_snorm the gather encodes during the copy. Encoding
    // here - before the meshopt passes - keeps the weld running on the final
    // bytes (quantization merges more, and GPU data equals staged data exactly).
    // Same affine as the primitive record / shader decode; the pre-clamp is what
    // keeps meshopt_quantizeSnorm() bit-identical to float_to_snorm16().
    GEO::vec3f position_encode_center   {0.0f, 0.0f, 0.0f};
    GEO::vec3f position_encode_inv_scale{1.0f, 1.0f, 1.0f};
    {
        constexpr float epsilon = 1e-6f;
        const erhe::math::Aabb& bounding_box = root.buffer_mesh.bounding_box;
        if (bounding_box.is_valid()) {
            const glm::vec3 center      = bounding_box.center();
            const glm::vec3 half_extent = 0.5f * bounding_box.diagonal();
            const glm::vec3 scale       = glm::max(half_extent, glm::vec3{epsilon});
            position_encode_center    = GEO::vec3f{center.x, center.y, center.z};
            position_encode_inv_scale = GEO::vec3f{1.0f / scale.x, 1.0f / scale.y, 1.0f / scale.z};
        }
    }

    // UV ranges for the affine texcoord channels, over the same corner prefix the
    // gather below copies. Computed before any conversion, because the encode
    // needs the affine and the per-primitive record writer has to reproduce
    // exactly the same one from the Buffer_mesh. Welding and reordering move no
    // value, so a range taken over the corner prefix bounds the optimized
    // variant's vertices too (a dropped corner can only shrink it).
    {
        std::array<Texcoord_range, affine_texcoord_channel_count> texcoord_ranges{};
        for (std::size_t channel = 0; channel < affine_texcoord_channel_count; ++channel) {
            const erhe::dataformat::Attribute_stream source = root.vertex_format.find_attribute(
                erhe::dataformat::Vertex_attribute_usage::tex_coord, static_cast<unsigned int>(channel)
            );
            if (
                (source.attribute == nullptr) ||
                (source.attribute->format != erhe::dataformat::Format::format_32_vec2_float)
            ) {
                continue;
            }
            const std::size_t stream_index = static_cast<std::size_t>(source.stream - root.vertex_format.streams.data());
            const Vertex_buffer_writer& writer = *vertex_writers.at(stream_index).get();
            if (writer.vertex_data.size() < corner_count * writer.stride) {
                continue;
            }
            for (std::size_t vertex = 0; vertex < corner_count; ++vertex) {
                float uv[2];
                memcpy(uv, writer.vertex_data.data() + vertex * writer.stride + source.attribute->offset, sizeof(uv));
                // A non-finite UV must not poison the range for every other
                // vertex; it clamps into it during the encode instead.
                if (!std::isfinite(uv[0]) || !std::isfinite(uv[1])) {
                    continue;
                }
                texcoord_ranges[channel].add(glm::vec2{uv[0], uv[1]});
            }
        }
        root.buffer_mesh.texcoord_ranges = texcoord_ranges;
        root.buffer_mesh.has_vertex_colors = root.vertex_attributes.color[0].is_valid();
    }
    const Texcoord_quantization texcoord_quantization = get_texcoord_quantization(root.buffer_mesh);

    std::vector<Mesh_optimize_stream> streams;
    streams.reserve(optimized_format.streams.size());
    for (std::size_t stream_index = 0, stream_end = optimized_format.streams.size(); stream_index < stream_end; ++stream_index) {
        const erhe::dataformat::Vertex_stream& optimized_stream = optimized_format.streams[stream_index];
        const Vertex_buffer_writer&            writer           = *vertex_writers.at(stream_index).get();
        if (writer.vertex_data.size() < corner_count * writer.stride) {
            return false;
        }
        Mesh_optimize_stream stream;
        stream.stride = optimized_stream.stride;
        stream.data.assign(corner_count * optimized_stream.stride, uint8_t{0});
        for (const erhe::dataformat::Vertex_attribute& optimized_attribute : optimized_stream.attributes) {
            const erhe::dataformat::Attribute_stream source = root.vertex_format.find_attribute(
                optimized_attribute.usage_type, optimized_attribute.usage_index
            );
            if (source.attribute == nullptr) {
                // The optimized format asks for something this build did not
                // stage. Decline rather than ship a variant with an attribute
                // left at zero.
                return false;
            }
            const bool encode_position =
                (optimized_attribute.usage_type  == erhe::dataformat::Vertex_attribute_usage::position) &&
                (optimized_attribute.usage_index == 0) &&
                (source.attribute->format    == erhe::dataformat::Format::format_32_vec3_float) &&
                (optimized_attribute.format  == erhe::dataformat::Format::format_16_vec3_snorm);
            if (encode_position) {
                for (std::size_t vertex = 0; vertex < corner_count; ++vertex) {
                    float position[3];
                    memcpy(position, writer.vertex_data.data() + vertex * writer.stride + source.attribute->offset, sizeof(position));
                    const GEO::vec3f biased{
                        position[0] - position_encode_center.x,
                        position[1] - position_encode_center.y,
                        position[2] - position_encode_center.z
                    };
                    const int16_t encoded[3] = {
                        static_cast<int16_t>(meshopt_quantizeSnorm(std::clamp(biased.x * position_encode_inv_scale.x, -1.0f, 1.0f), 16)),
                        static_cast<int16_t>(meshopt_quantizeSnorm(std::clamp(biased.y * position_encode_inv_scale.y, -1.0f, 1.0f), 16)),
                        static_cast<int16_t>(meshopt_quantizeSnorm(std::clamp(biased.z * position_encode_inv_scale.z, -1.0f, 1.0f), 16))
                    };
                    memcpy(stream.data.data() + vertex * optimized_stream.stride + optimized_attribute.offset, encoded, sizeof(encoded));
                }
                continue;
            }
            // Skinning influences. The weights are sorted (smallest last) and
            // the indices permuted in lockstep, so BOTH attributes read BOTH
            // source attributes and each re-derives the same sort - cheaper than
            // threading state between two independent gather entries, and
            // order-independent.
            const bool joint_weights_are_implicit_sum =
                erhe::dataformat::get_vertex_joint_weights_encoding(&optimized_format) ==
                erhe::dataformat::Vertex_joint_weights_encoding::unorm16x3_implicit_sum;
            const bool is_joint_weights = (optimized_attribute.usage_type == erhe::dataformat::Vertex_attribute_usage::joint_weights);
            const bool is_joint_indices = (optimized_attribute.usage_type == erhe::dataformat::Vertex_attribute_usage::joint_indices);
            if (joint_weights_are_implicit_sum && (is_joint_weights || is_joint_indices)) {
                const erhe::dataformat::Attribute_stream source_indices = root.vertex_format.find_attribute(
                    erhe::dataformat::Vertex_attribute_usage::joint_indices, 0
                );
                const erhe::dataformat::Attribute_stream source_weights = root.vertex_format.find_attribute(
                    erhe::dataformat::Vertex_attribute_usage::joint_weights, 0
                );
                if (
                    (source_indices.attribute == nullptr) ||
                    (source_weights.attribute == nullptr) ||
                    (source_indices.attribute->format != erhe::dataformat::Format::format_8_vec4_uint) ||
                    (source_weights.attribute->format != erhe::dataformat::Format::format_8_vec4_unorm)
                ) {
                    return false;
                }
                const std::size_t indices_stream_index = static_cast<std::size_t>(source_indices.stream - root.vertex_format.streams.data());
                const std::size_t weights_stream_index = static_cast<std::size_t>(source_weights.stream - root.vertex_format.streams.data());
                const Vertex_buffer_writer& indices_writer = *vertex_writers.at(indices_stream_index).get();
                const Vertex_buffer_writer& weights_writer = *vertex_writers.at(weights_stream_index).get();
                if (
                    (indices_writer.vertex_data.size() < corner_count * indices_writer.stride) ||
                    (weights_writer.vertex_data.size() < corner_count * weights_writer.stride)
                ) {
                    return false;
                }
                for (std::size_t vertex = 0; vertex < corner_count; ++vertex) {
                    uint8_t raw_indices[4];
                    uint8_t raw_weights[4];
                    memcpy(raw_indices, indices_writer.vertex_data.data() + vertex * indices_writer.stride + source_indices.attribute->offset, sizeof(raw_indices));
                    memcpy(raw_weights, weights_writer.vertex_data.data() + vertex * weights_writer.stride + source_weights.attribute->offset, sizeof(raw_weights));
                    const Joint_influences influences = sort_joint_influences(
                        glm::vec4{
                            static_cast<float>(raw_indices[0]), static_cast<float>(raw_indices[1]),
                            static_cast<float>(raw_indices[2]), static_cast<float>(raw_indices[3])
                        },
                        glm::vec4{
                            erhe::dataformat::unorm8_to_float(raw_weights[0]), erhe::dataformat::unorm8_to_float(raw_weights[1]),
                            erhe::dataformat::unorm8_to_float(raw_weights[2]), erhe::dataformat::unorm8_to_float(raw_weights[3])
                        }
                    );
                    uint8_t* const destination = stream.data.data() + vertex * optimized_stream.stride + optimized_attribute.offset;
                    if (is_joint_indices) {
                        memcpy(destination, influences.indices.data(), influences.indices.size());
                    } else {
                        const std::array<uint16_t, 3> encoded = encode_implicit_sum_joint_weights(influences.weights);
                        memcpy(destination, encoded.data(), encoded.size() * sizeof(uint16_t));
                    }
                }
                continue;
            }
            // The tangent slot of an optimized format carries the whole tangent
            // frame as a quaternion, so this reads TWO staged attributes
            // (normal and tangent) into one - the only gather entry that does.
            const bool encode_tbn =
                (optimized_attribute.usage_type == erhe::dataformat::Vertex_attribute_usage::tangent) &&
                (optimized_attribute.format     == erhe::dataformat::Format::format_16_vec4_sint);
            if (encode_tbn) {
                const erhe::dataformat::Attribute_stream source_normal = root.vertex_format.find_attribute(
                    erhe::dataformat::Vertex_attribute_usage::normal, erhe::dataformat::normal_attribute
                );
                if (
                    (source.attribute->format != erhe::dataformat::Format::format_32_vec4_float) ||
                    (source_normal.attribute == nullptr) ||
                    (source_normal.attribute->format != erhe::dataformat::Format::format_32_vec3_float)
                ) {
                    return false;
                }
                const std::size_t normal_stream_index = static_cast<std::size_t>(source_normal.stream - root.vertex_format.streams.data());
                const Vertex_buffer_writer& normal_writer = *vertex_writers.at(normal_stream_index).get();
                if (normal_writer.vertex_data.size() < corner_count * normal_writer.stride) {
                    return false;
                }
                for (std::size_t vertex = 0; vertex < corner_count; ++vertex) {
                    float normal [3];
                    float tangent[4];
                    memcpy(normal,  normal_writer.vertex_data.data() + vertex * normal_writer.stride + source_normal.attribute->offset, sizeof(normal));
                    memcpy(tangent, writer       .vertex_data.data() + vertex * writer       .stride + source        .attribute->offset, sizeof(tangent));
                    const std::array<int16_t, 4> encoded = encode_tbn_quaternion(
                        glm::vec3{normal [0], normal [1], normal [2]},
                        glm::vec4{tangent[0], tangent[1], tangent[2], tangent[3]}
                    );
                    memcpy(
                        stream.data.data() + vertex * optimized_stream.stride + optimized_attribute.offset,
                        encoded.data(),
                        encoded.size() * sizeof(int16_t)
                    );
                }
                continue;
            }
            // Texcoord channels 0 and 1 are normalized into the primitive's own
            // per-channel UV range, so they are an affine encode rather than a
            // pure format conversion - exactly like the position. Channel 2 is
            // NOT here: it is in [0, 1] by construction and goes through the
            // plain conversion below.
            const bool encode_texcoord =
                (optimized_attribute.usage_type  == erhe::dataformat::Vertex_attribute_usage::tex_coord) &&
                (optimized_attribute.usage_index <  affine_texcoord_channel_count)                       &&
                (source.attribute->format        == erhe::dataformat::Format::format_32_vec2_float)      &&
                (optimized_attribute.format      == erhe::dataformat::Format::format_16_vec2_unorm);
            if (encode_texcoord) {
                const glm::length_t channel = static_cast<glm::length_t>(2 * optimized_attribute.usage_index);
                const glm::vec2     scale {texcoord_quantization.scale [channel + 0], texcoord_quantization.scale [channel + 1]};
                const glm::vec2     offset{texcoord_quantization.offset[channel + 0], texcoord_quantization.offset[channel + 1]};
                const glm::vec2   inv_scale{1.0f / scale.x, 1.0f / scale.y};
                for (std::size_t vertex = 0; vertex < corner_count; ++vertex) {
                    float uv[2];
                    memcpy(uv, writer.vertex_data.data() + vertex * writer.stride + source.attribute->offset, sizeof(uv));
                    // The clamp is what makes meshopt_quantizeUnorm() safe here:
                    // a non-finite or out-of-range UV lands on a range endpoint
                    // instead of wrapping.
                    const glm::vec2 normalized = glm::clamp(
                        (glm::vec2{uv[0], uv[1]} - offset) * inv_scale,
                        glm::vec2{0.0f},
                        glm::vec2{1.0f}
                    );
                    const uint16_t encoded[2] = {
                        static_cast<uint16_t>(meshopt_quantizeUnorm(normalized.x, 16)),
                        static_cast<uint16_t>(meshopt_quantizeUnorm(normalized.y, 16))
                    };
                    memcpy(stream.data.data() + vertex * optimized_stream.stride + optimized_attribute.offset, encoded, sizeof(encoded));
                }
                continue;
            }
            if (!is_supported_attribute_conversion(source.attribute->format, optimized_attribute.format)) {
                // The optimized format stages this attribute differently and no
                // conversion is defined for it. Decline rather than guess.
                return false;
            }
            if (source.attribute->format == optimized_attribute.format) {
                const std::size_t size = erhe::dataformat::get_format_size_bytes(optimized_attribute.format);
                for (std::size_t vertex = 0; vertex < corner_count; ++vertex) {
                    memcpy(
                        stream.data.data()        + vertex * optimized_stream.stride + optimized_attribute.offset,
                        writer.vertex_data.data() + vertex * writer.stride           + source.attribute->offset,
                        size
                    );
                }
                continue;
            }
            // Converted attribute. Same reference encoder the soup path uses, so
            // the two paths cannot drift; and it runs BEFORE the meshopt passes,
            // so the weld compares the final bytes (a quantized attribute merges
            // strictly more corners than a float one, and the GPU data equals the
            // staged data exactly).
            for (std::size_t vertex = 0; vertex < corner_count; ++vertex) {
                erhe::dataformat::convert(
                    writer.vertex_data.data() + vertex * writer.stride           + source.attribute->offset,
                    source.attribute->format,
                    stream.data.data()        + vertex * optimized_stream.stride + optimized_attribute.offset,
                    optimized_attribute.format,
                    1.0f
                );
            }
        }
        streams.push_back(std::move(stream));
    }

    const erhe::dataformat::Format index_type      = root.build_info.buffer_info.index_type;
    const std::size_t              index_type_size = index_writer.index_type_size;
    const uint8_t* const           index_base      = index_writer.triangle_fill_index_data_span.data();
    std::vector<uint32_t>          fill_indices(index_count, 0u);
    for (std::size_t i = 0; i < index_count; ++i) {
        const uint8_t* const source = index_base + i * index_type_size;
        switch (index_type) {
            case erhe::dataformat::Format::format_8_scalar_uint: {
                fill_indices[i] = *source;
                break;
            }
            case erhe::dataformat::Format::format_16_scalar_uint: {
                uint16_t value = 0;
                memcpy(&value, source, sizeof(value));
                fill_indices[i] = value;
                break;
            }
            case erhe::dataformat::Format::format_32_scalar_uint: {
                uint32_t value = 0;
                memcpy(&value, source, sizeof(value));
                fill_indices[i] = value;
                break;
            }
            default: {
                return false;
            }
        }
    }

    out_streams      = std::move(streams);
    out_fill_indices = std::move(fill_indices);
    return true;
}

auto Build_context::allocate_and_bind_writers() -> bool
{
    if (root.build_failed) {
        return false;
    }
    root.allocate_buffers();
    if (root.build_failed) {
        // Out of pool memory, or streams out of lockstep. No writer is given a
        // range, so every one of them drops its staged bytes on destruction.
        return false;
    }

    ERHE_VERIFY(root.buffer_mesh.vertex_buffer_ranges.size() == vertex_writers.size());
    for (std::size_t stream = 0, end = vertex_writers.size(); stream < end; ++stream) {
        vertex_writers[stream]->set_buffer_range(root.buffer_mesh.vertex_buffer_ranges[stream]);
    }
    ERHE_VERIFY(root.buffer_mesh.expanded_vertex_buffer_ranges.size() == expanded_vertex_writers.size());
    for (std::size_t stream = 0, end = expanded_vertex_writers.size(); stream < end; ++stream) {
        expanded_vertex_writers[stream]->set_buffer_range(root.buffer_mesh.expanded_vertex_buffer_ranges[stream]);
    }
    index_writer.set_buffer_range(root.buffer_mesh.index_buffer_range);

    // The edge-line side buffers are not written through a Vertex_buffer_writer
    // (build_edge_lines stages raw bytes), so they are enqueued here by hand,
    // under the same "only once the range exists" rule.
    if ((m_edge_line_vertex_bytes_written > 0) && (root.buffer_mesh.edge_line_vertex_buffer_range.count > 0)) {
        root.build_info.buffer_info.vertex_buffer_sink.enqueue_vertex_data(
            root.buffer_mesh.edge_line_vertex_buffer_range,
            std::move(m_edge_line_vertex_data)
        );
    }
    if ((m_edge_line_joint_bytes_written > 0) && (root.buffer_mesh.edge_line_joint_buffer_range.count > 0)) {
        root.build_info.buffer_info.vertex_buffer_sink.enqueue_vertex_data(
            root.buffer_mesh.edge_line_joint_buffer_range,
            std::move(m_edge_line_joint_data)
        );
    }
    return true;
}

auto Build_context::is_ready() const -> bool
{
    // Mesh counts, not allocated ranges: allocation happens after the build,
    // so the ranges are empty while this is being asked. The counts are what
    // the allocation will be sized from, so the meaning is the same - "this
    // mesh has something to build" - just available earlier.
    const bool ready =
        !root.build_failed &&
        (root.total_index_count  != 0) &&
        (root.total_vertex_count != 0);
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
    const bool do_vertex_texcoord_2    = root.vertex_attributes.texcoord[2]       .is_valid();
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
            if (do_vertex_texcoord_2   ) build_vertex_texcoord     (2);
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
    // No expanded vertex format supplied -> nothing to build.
    if (expanded_vertex_writers.empty()) {
        return;
    }
    const erhe::dataformat::Vertex_format* expanded_format = root.build_info.buffer_info.expanded_vertex_format;
    if (expanded_format == nullptr) {
        return;
    }

    using namespace erhe::dataformat;

    // The packed wireframe attribute lives only in the expanded format.
    const Vertex_attribute_info wireframe_info{*expanded_format, Vertex_attribute_usage::custom, custom_attribute_wireframe};

    std::vector<std::unique_ptr<Vertex_buffer_writer>>& expanded_writers = expanded_vertex_writers;

    const auto expanded_writer_for = [&](Vertex_attribute_usage usage, std::size_t index) -> Vertex_buffer_writer* {
        const Attribute_stream as = expanded_format->find_attribute(usage, index);
        if (as.attribute == nullptr) {
            return nullptr;
        }
        const std::size_t stream_index = static_cast<std::size_t>(as.stream - expanded_format->streams.data());
        return expanded_writers.at(stream_index).get();
    };

    Vertex_buffer_writer* wireframe_writer = expanded_writer_for(Vertex_attribute_usage::custom, custom_attribute_wireframe);

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
    const bool do_vertex_texcoord_2    = (attribute_writers.texcoord_0      != nullptr) && root.vertex_attributes.texcoord[2].is_valid();
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
                if (do_vertex_texcoord_2)    build_vertex_texcoord     (2);
                if (do_vertex_color_0)       build_vertex_color        (0);
                if (do_vertex_color_1)       build_vertex_color        (1);
                if (do_aniso_control)        build_vertex_aniso_control();
                if (do_joint_indices_0)      build_vertex_joint_indices(0);
                if (do_joint_weights_0)      build_vertex_joint_weights(0);

                if (wireframe_writer != nullptr) {
                    const uint32_t packed = j | (edge_mask << 2);
                    wireframe_writer->write(wireframe_info, packed);
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
    // matching the compute shader's edge_line_vertex SSBO struct. position.w is
    // an unused pad slot; normal.w packs two per-face signs (its sign = the
    // interior-tangent sign, its magnitude = the edge-traversal winding tdir; see
    // the pack site below). The buffer range itself is allocated up front by
    // Build_context_root::allocate_edge_line_vertex_buffer().
    // The range is allocated AFTER the build, so this cannot ask whether one
    // exists - it mirrors the conditions allocate_edge_line_vertex_buffer()
    // allocates under. Staging is into a member vector that outlives this
    // function; allocate_and_bind_writers() enqueues it once the range exists.
    const bool has_edge_line_vertex_buffer =
        (root.mesh_info.edge_count > 0) &&
        (root.build_info.buffer_info.edge_line_vertex_stream != nullptr);
    std::vector<uint8_t>& edge_line_vertex_data = m_edge_line_vertex_data;
    const std::size_t     vertex_element_size = 8 * sizeof(float); // vec4 position + vec4 normal
    if (has_edge_line_vertex_buffer) {
        const std::size_t edge_count = root.mesh_info.edge_count;
        edge_line_vertex_data.resize(edge_count * 2 * vertex_element_size);
    }
    std::size_t edge_vertex_write_offset = 0;

    // Companion joint side buffer for skinned edge lines: uvec4 joint indices
    // + vec4 joint weights per endpoint. Allocated only when the mesh has
    // joint attributes (see Build_context_root::allocate_edge_line_joint_buffer).
    // Same reasoning as the vertex buffer above; mirrors
    // allocate_edge_line_joint_buffer()'s conditions, skinned meshes included.
    const GEO::AttributesManager& edge_joint_vertex_attrs = root.mesh.vertices.attributes();
    const bool has_edge_line_joint_buffer =
        (root.mesh_info.edge_count > 0) &&
        (root.build_info.buffer_info.edge_line_joint_stream != nullptr) &&
        edge_joint_vertex_attrs.is_defined(erhe::geometry::c_joint_indices_0) &&
        edge_joint_vertex_attrs.is_defined(erhe::geometry::c_joint_weights_0);
    std::vector<uint8_t>& edge_line_joint_data = m_edge_line_joint_data;
    const std::size_t     joint_element_size = 4 * sizeof(uint32_t) + 4 * sizeof(float); // uvec4 + vec4
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

            // Pack BOTH per-face signs into normal.w: its SIGN carries the
            // interior-tangent sign (sign_a/sign_b, +/-1), its MAGNITUDE carries the
            // edge-traversal winding tdir (tdir > 0 -> 1, tdir < 0 -> 2). Exact in
            // fp32 (values are +/-1 or +/-2). The tent wide-line path decodes both;
            // the simple-quad and geometry-shader backends read only normal.xyz, so
            // the packed .w does not affect them. A facet-less edge keeps sign 0 ->
            // packed 0 -> the shader's degenerate-frame path.
            const float packed_w_a = sign_a * ((tdir_a < 0.0f) ? 2.0f : 1.0f);
            const float packed_w_b = sign_b * ((tdir_b < 0.0f) ? 2.0f : 1.0f);
            const float data_a[8] = { pos_a.x, pos_a.y, pos_a.z, 0.0f, normal_a.x, normal_a.y, normal_a.z, packed_w_a };
            const float data_b[8] = { pos_b.x, pos_b.y, pos_b.z, 0.0f, normal_b.x, normal_b.y, normal_b.z, packed_w_b };
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

    // Enqueued by allocate_and_bind_writers(), once the ranges exist. Anything
    // staged but not written is dropped there rather than here.
    m_edge_line_vertex_bytes_written = edge_vertex_write_offset;
    m_edge_line_joint_bytes_written  = edge_joint_write_offset;
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

    // The index buffer is allocated after the build, so there is nothing to
    // check against here. Build_context_root::allocate_buffers() makes the
    // equivalent check once the allocation exists.
}

auto build_buffer_mesh(
    Buffer_mesh&                             buffer_mesh,
    const GEO::Mesh&                         source_mesh,
    const Build_info&                        build_info,
    Element_mappings&                        element_mappings,
    Normal_style                             normal_style,
    std::shared_ptr<Primitive_render_shape>* out_optimized_shape,
    std::string_view                         name
) -> bool
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(element_mappings.triangle_to_mesh_facet.empty());
    ERHE_VERIFY(element_mappings.mesh_corner_to_vertex_buffer_index.empty());
    ERHE_VERIFY(element_mappings.mesh_vertex_to_vertex_buffer_index.empty());
    Primitive_builder builder{
        buffer_mesh, source_mesh, build_info, element_mappings, normal_style, name,
        out_optimized_shape != nullptr
    };
    if (!builder.build()) {
        return false;
    }
    if (out_optimized_shape != nullptr) {
        *out_optimized_shape = builder.take_optimized_render_shape();
    }
    return true;
}

} // namespace erhe::primitive
