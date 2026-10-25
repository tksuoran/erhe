#include "erhe_primitive/prepared_primitive.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/primitive_builder.hpp"
#include "erhe_primitive/primitive_log.hpp"
#include "erhe_buffer/ibuffer.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_hash/hash.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_raytrace/igeometry.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <geogram/mesh/mesh.h>

#include <array>
#include <cstring>
#include <initializer_list>

namespace erhe::primitive {

namespace {

using erhe::dataformat::Format;
using erhe::dataformat::Vertex_attribute;
using erhe::dataformat::Vertex_attribute_usage;
using erhe::dataformat::Vertex_format;
using erhe::dataformat::Vertex_stream;

// The attribute sets the content build writes (Build_context::build_polygon_fill()).
constexpr std::size_t c_texcoord_set_count = 3;
constexpr std::size_t c_color_set_count    = 2;
constexpr std::size_t c_joint_set_count    = 2;

class Core_format_plan
{
public:
    Vertex_format format;
    std::string   refusal;
};

// Present sets must be 0 .. n-1: the build writes set n through the writer
// it looked up for set 0.
[[nodiscard]] auto is_prefix(const std::array<bool, 3>& present, const std::size_t count) -> bool
{
    bool gap = false;
    for (std::size_t i = 0; i < count; ++i) {
        if (!present[i]) {
            gap = true;
        } else if (gap) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] auto plan_core_format(const Triangle_soup& soup) -> Core_format_plan
{
    Core_format_plan plan;
    if (soup.vertex_format.streams.size() != 1) {
        plan.refusal = fmt::format("{} vertex streams", soup.vertex_format.streams.size());
        return plan;
    }
    std::array<bool, 3> texcoord{false, false, false};
    std::array<bool, 3> color   {false, false, false};
    std::array<bool, 3> joints  {false, false, false};
    std::array<bool, 3> weights {false, false, false};
    std::array<Format, 3> joint_format{Format::format_undefined, Format::format_undefined, Format::format_undefined};
    bool has_position = false;
    for (const Vertex_attribute& attribute : soup.vertex_format.streams.front().attributes) {
        const std::size_t index = attribute.usage_index;
        switch (attribute.usage_type) {
            case Vertex_attribute_usage::position: {
                has_position = has_position || (index == 0);
                break;
            }
            case Vertex_attribute_usage::tex_coord: {
                if (index >= c_texcoord_set_count) {
                    plan.refusal = fmt::format("TEXCOORD_{} is not written by the content build", index);
                    return plan;
                }
                texcoord[index] = true;
                break;
            }
            case Vertex_attribute_usage::color: {
                if (index >= c_color_set_count) {
                    plan.refusal = fmt::format("COLOR_{} is not written by the content build", index);
                    return plan;
                }
                color[index] = true;
                break;
            }
            case Vertex_attribute_usage::joint_indices: {
                if (index >= c_joint_set_count) {
                    plan.refusal = fmt::format("JOINTS_{} is not written by the content build", index);
                    return plan;
                }
                if ((attribute.format != Format::format_8_vec4_uint) && (attribute.format != Format::format_16_vec4_uint)) {
                    plan.refusal = fmt::format("JOINTS_{} format {} is not vec4 uint8 / uint16", index, erhe::dataformat::c_str(attribute.format));
                    return plan;
                }
                joints[index]       = true;
                joint_format[index] = attribute.format;
                break;
            }
            case Vertex_attribute_usage::joint_weights: {
                if (index >= c_joint_set_count) {
                    plan.refusal = fmt::format("WEIGHTS_{} is not written by the content build", index);
                    return plan;
                }
                weights[index] = true;
                break;
            }
            default: {
                // NORMAL and TANGENT are always written; erhe-only attributes
                // (bitangent, custom, smooth normal) are not part of the core.
                break;
            }
        }
    }
    if (!has_position) {
        plan.refusal = "no POSITION";
        return plan;
    }
    if (
        !is_prefix(texcoord, c_texcoord_set_count) ||
        !is_prefix(color,    c_color_set_count)    ||
        !is_prefix(joints,   c_joint_set_count)    ||
        !is_prefix(weights,  c_joint_set_count)
    ) {
        plan.refusal = "attribute sets with a gap";
        return plan;
    }

    Vertex_stream stream{0};
    stream.emplace_back(Format::format_32_vec3_float, Vertex_attribute_usage::position, 0);
    stream.emplace_back(Format::format_32_vec3_float, Vertex_attribute_usage::normal,   0);
    stream.emplace_back(Format::format_32_vec4_float, Vertex_attribute_usage::tangent,  0);
    for (std::size_t i = 0; i < c_texcoord_set_count; ++i) {
        if (texcoord[i]) {
            stream.emplace_back(Format::format_32_vec2_float, Vertex_attribute_usage::tex_coord, i);
        }
    }
    for (std::size_t i = 0; i < c_color_set_count; ++i) {
        if (color[i]) {
            stream.emplace_back(Format::format_32_vec4_float, Vertex_attribute_usage::color, i);
        }
    }
    for (std::size_t i = 0; i < c_joint_set_count; ++i) {
        if (joints[i]) {
            stream.emplace_back(joint_format[i], Vertex_attribute_usage::joint_indices, i);
        }
    }
    for (std::size_t i = 0; i < c_joint_set_count; ++i) {
        if (weights[i]) {
            stream.emplace_back(Format::format_32_vec4_float, Vertex_attribute_usage::joint_weights, i);
        }
    }
    stream.finalize_stride();
    plan.format.streams.push_back(std::move(stream));
    return plan;
}

[[nodiscard]] auto refuse(std::string reason) -> Prepare_primitive_result
{
    Prepare_primitive_result result;
    result.skip_reason = std::move(reason);
    return result;
}

// A BVH over the core triangles in index order, through the running backend.
[[nodiscard]] auto build_core_bvh(const std::span<const uint32_t> indices, const std::span<const float> positions_xyz) -> std::optional<erhe::raytrace::Prebuilt_bvh>
{
    ERHE_PROFILE_FUNCTION();

    // Tail padding of 16 bytes is required by Embree 4 for shared buffers;
    // the buffers are declared first so they outlive the geometry.
    static constexpr std::size_t tail_padding = 16;
    erhe::buffer::Cpu_buffer vertex_buffer{"prepared_export_bvh_vertex", positions_xyz.size_bytes(), tail_padding};
    erhe::buffer::Cpu_buffer index_buffer {"prepared_export_bvh_index",  indices.size_bytes(),       tail_padding};
    std::memcpy(vertex_buffer.get_span().data(), positions_xyz.data(), positions_xyz.size_bytes());
    std::memcpy(index_buffer .get_span().data(), indices.data(),       indices.size_bytes());

    std::unique_ptr<erhe::raytrace::IGeometry> geometry = erhe::raytrace::IGeometry::create_unique(
        "prepared_export", erhe::raytrace::Geometry_type::GEOMETRY_TYPE_TRIANGLE
    );
    geometry->set_buffer(
        erhe::raytrace::Buffer_type::BUFFER_TYPE_VERTEX, 0, Format::format_32_vec3_float,
        &vertex_buffer, 0, 3 * sizeof(float), positions_xyz.size() / 3
    );
    geometry->set_buffer(
        erhe::raytrace::Buffer_type::BUFFER_TYPE_INDEX, 0, Format::format_32_vec3_uint,
        &index_buffer, 0, 3 * sizeof(uint32_t), indices.size() / 3
    );
    geometry->commit();
    erhe::raytrace::Prebuilt_bvh bvh;
    if (!geometry->export_bvh(bvh)) {
        return {};
    }
    return bvh;
}

// The shape's committed real raytrace BVH re-expressed over the core
// triangles, or empty when its triangles do not map 1:1 onto them.
[[nodiscard]] auto remap_committed_bvh(
    Primitive_shape&                shape,
    const std::span<const uint32_t> core_triangle_from_facet,
    const std::size_t               facet_count,
    const std::span<const uint32_t> core_indices,
    const std::span<const float>    core_positions_xyz
) -> std::optional<erhe::raytrace::Prebuilt_bvh>
{
    if (!shape.has_real_raytrace()) {
        return {};
    }
    const Primitive_raytrace& raytrace = shape.get_raytrace();
    const std::shared_ptr<erhe::raytrace::IGeometry>& rt_geometry = raytrace.get_raytrace_geometry();
    if (!rt_geometry) {
        return {};
    }
    erhe::raytrace::Prebuilt_bvh bvh;
    if (!rt_geometry->export_bvh(bvh)) {
        return {};
    }
    const std::size_t core_triangle_count = core_indices.size() / 3;
    if ((bvh.triangle_count != core_triangle_count) || (bvh.triangle_count != facet_count)) {
        return {};
    }

    // Raytrace triangle r is facet raytrace.get_mesh_facet_from_triangle(r);
    // the map has to be a bijection onto the core triangles.
    std::vector<uint32_t> core_from_raytrace(bvh.triangle_count, 0);
    std::vector<bool>     seen(core_triangle_count, false);
    for (std::size_t r = 0; r < bvh.triangle_count; ++r) {
        const GEO::index_t facet = raytrace.get_mesh_facet_from_triangle(static_cast<uint32_t>(r));
        if ((facet == GEO::NO_INDEX) || (facet >= facet_count)) {
            return {};
        }
        const uint32_t core_triangle = core_triangle_from_facet[facet];
        if ((core_triangle == Mesh_optimize_result::no_vertex) || seen[core_triangle]) {
            return {};
        }
        seen[core_triangle] = true;
        core_from_raytrace[r] = core_triangle;
    }

    // The node bounds were built from the raytrace's triangle positions; they
    // stay valid only if those are the core positions. The committed hash is
    // over the raytrace order, so hash the core triangles in that order.
    {
        std::vector<uint32_t> indices_in_raytrace_order(core_indices.size());
        for (std::size_t r = 0; r < bvh.triangle_count; ++r) {
            const std::size_t t = core_from_raytrace[r];
            for (std::size_t i = 0; i < 3; ++i) {
                indices_in_raytrace_order[(3 * r) + i] = core_indices[(3 * t) + i];
            }
        }
        if (erhe::raytrace::compute_triangle_hash(indices_in_raytrace_order, core_positions_xyz) != bvh.triangle_hash) {
            return {};
        }
    }

    for (uint32_t& primitive_id : bvh.primitive_ids) {
        if (primitive_id >= core_from_raytrace.size()) {
            return {};
        }
        primitive_id = core_from_raytrace[primitive_id];
    }
    bvh.triangle_hash  = erhe::raytrace::compute_triangle_hash(core_indices, core_positions_xyz);
    bvh.triangle_count = core_triangle_count;
    const std::string reason = erhe::raytrace::validate_prebuilt_bvh(
        bvh,
        erhe::raytrace::get_prebuilt_bvh_backend(),
        erhe::raytrace::get_prebuilt_bvh_library(),
        bvh.triangle_hash,
        bvh.triangle_count,
        erhe::raytrace::bvh_v2_traversal_stack_size
    );
    if (!reason.empty()) {
        log_primitive->warn("prepare_primitive_for_export: remapped BVH rejected ({}), building a new one", reason);
        return {};
    }
    return bvh;
}

} // anonymous namespace

auto c_str(const Prepared_bvh_source source) -> const char*
{
    switch (source) {
        case Prepared_bvh_source::none:    return "none";
        case Prepared_bvh_source::reused:  return "reused";
        case Prepared_bvh_source::rebuilt: return "rebuilt";
        default:                           return "?";
    }
}

auto compute_mesh_content_hash(const std::span<const uint32_t> indices, const std::span<const float> positions_xyz) -> uint64_t
{
    // erhe::hash::hash() hashes the in-memory bytes; every erhe target is
    // little-endian, which is the byte order the extension specifies.
    uint64_t hash_code = erhe::hash::c_seed;
    hash_code = erhe::hash::hash(indices.data(),       indices.size_bytes(),       hash_code);
    hash_code = erhe::hash::hash(positions_xyz.data(), positions_xyz.size_bytes(), hash_code);
    return hash_code;
}

namespace {

[[nodiscard]] auto same_position(const std::span<const float> positions_xyz, const uint32_t a, const uint32_t b) -> bool
{
    return std::memcmp(&positions_xyz[3 * static_cast<std::size_t>(a)], &positions_xyz[3 * static_cast<std::size_t>(b)], 3 * sizeof(float)) == 0;
}

} // anonymous namespace

auto validate_prepared_core(
    const std::span<const uint32_t> indices,
    const std::span<const float>    positions_xyz,
    const uint64_t                  content_hash
) -> std::string
{
    ERHE_PROFILE_FUNCTION();

    if ((positions_xyz.size() % 3) != 0) {
        return "position count is not whole";
    }
    const std::size_t vertex_count = positions_xyz.size() / 3;
    if (indices.empty() || ((indices.size() % 3) != 0)) {
        return "index count is not a positive multiple of 3";
    }
    // Every vertex referenced: count the vertices seen for the first time.
    // A local vector is fine here: this runs once per primitive at load.
    std::vector<uint8_t> referenced(vertex_count, uint8_t{0});
    std::size_t          referenced_count = 0;
    for (std::size_t corner = 0, end = indices.size(); corner < end; corner += 3) {
        const uint32_t v0 = indices[corner + 0];
        const uint32_t v1 = indices[corner + 1];
        const uint32_t v2 = indices[corner + 2];
        if ((v0 >= vertex_count) || (v1 >= vertex_count) || (v2 >= vertex_count)) {
            return "index out of range";
        }
        if (same_position(positions_xyz, v0, v1) || same_position(positions_xyz, v1, v2) || same_position(positions_xyz, v2, v0)) {
            return "degenerate triangle";
        }
        for (const uint32_t v : {v0, v1, v2}) {
            if (referenced[v] == 0) {
                referenced[v] = 1;
                ++referenced_count;
            }
        }
    }
    if (referenced_count != vertex_count) {
        return "unreferenced vertex";
    }
    if (compute_mesh_content_hash(indices, positions_xyz) != content_hash) {
        return "content_hash mismatch";
    }
    return {};
}

auto validate_vertex_points(
    const std::span<const uint32_t> vertex_points,
    const uint32_t                  point_count,
    const std::span<const float>    positions_xyz
) -> std::string
{
    ERHE_PROFILE_FUNCTION();

    const std::size_t vertex_count = positions_xyz.size() / 3;
    if (vertex_points.size() != vertex_count) {
        return "vertex_points size mismatch";
    }
    if ((point_count == 0) || (point_count > vertex_count)) {
        return "point_count out of range";
    }
    // first_vertex[p]: the first vertex of point p; its size is the count of
    // distinct ids seen so far.
    std::vector<uint32_t> first_vertex;
    first_vertex.reserve(point_count);
    for (std::size_t v = 0; v < vertex_count; ++v) {
        const uint32_t point = vertex_points[v];
        if (point < first_vertex.size()) {
            if (!same_position(positions_xyz, first_vertex[point], static_cast<uint32_t>(v))) {
                return "point vertices differ in position";
            }
        } else if (point == first_vertex.size()) {
            if (first_vertex.size() == point_count) {
                return "point id out of range";
            }
            first_vertex.push_back(static_cast<uint32_t>(v));
        } else {
            return "not in first-reference order";
        }
    }
    if (first_vertex.size() != point_count) {
        return "point_count mismatch";
    }
    return {};
}

auto get_float_positions(const Triangle_soup& soup) -> std::vector<float>
{
    if (soup.vertex_format.streams.empty()) {
        return {};
    }
    const Vertex_stream&    stream   = soup.vertex_format.streams.front();
    const Vertex_attribute* position = stream.find_attribute(Vertex_attribute_usage::position, 0);
    if ((position == nullptr) || (position->format != Format::format_32_vec3_float)) {
        return {};
    }
    const std::size_t  vertex_count = soup.get_vertex_count();
    std::vector<float> positions(3 * vertex_count);
    for (std::size_t vertex = 0; vertex < vertex_count; ++vertex) {
        std::memcpy(&positions[3 * vertex], soup.vertex_data.data() + (vertex * stream.stride) + position->offset, 3 * sizeof(float));
    }
    return positions;
}

auto prepare_primitive_for_export(Primitive_shape& shape) -> Prepare_primitive_result
{
    ERHE_PROFILE_FUNCTION();

    const std::shared_ptr<Triangle_soup>& soup = shape.get_triangle_soup();
    if (!soup) {
        return refuse("no triangle soup");
    }
    if (soup->primitive_type != Primitive_type::triangles) {
        return refuse("not a triangle list");
    }
    Core_format_plan plan = plan_core_format(*soup);
    if (!plan.refusal.empty()) {
        return refuse(plan.refusal);
    }

    const std::shared_ptr<erhe::geometry::Geometry>& geometry = shape.get_geometry();
    if (!geometry) {
        return refuse("no geometry");
    }
    const GEO::Mesh& mesh = geometry->get_mesh();
    const GEO::index_t facet_count = mesh.facets.nb();
    if (facet_count == 0) {
        return refuse("no facets");
    }
    for (GEO::index_t facet = 0; facet < facet_count; ++facet) {
        if (mesh.facets.nb_vertices(facet) != 3) {
            log_primitive->warn("prepare_primitive_for_export: facet {} has {} corners - not prepared", facet, mesh.facets.nb_vertices(facet));
            return refuse("non-triangle facet");
        }
    }

    // --- Content build into float CPU buffers ------------------------------
    // One vertex per facet corner, with the values the content build writes
    // for that corner (Normal_style::corner_normals, fill triangles only).
    const Vertex_stream& stream       = plan.format.streams.front();
    const std::size_t    stride       = stream.stride;
    const erhe::geometry::Mesh_info mesh_info = erhe::geometry::get_mesh_info(mesh);
    const std::size_t    corner_count = mesh_info.vertex_count_corners;
    const std::size_t    index_count  = mesh_info.index_count_fill_triangles;

    std::vector<uint8_t>  corner_vertex_data;
    std::vector<uint32_t> corner_indices;
    Element_mappings      element_mappings;
    {
        // Buffers first: the Buffer_mesh allocations return their bytes to
        // them on destruction.
        erhe::buffer::Cpu_buffer vertex_buffer{"prepared_export_vertex", corner_count * stride};
        erhe::buffer::Cpu_buffer index_buffer {"prepared_export_index",  index_count * sizeof(uint32_t)};
        Cpu_vertex_buffer_sink   vertex_sink{{&vertex_buffer}};
        Cpu_index_buffer_sink    index_sink {index_buffer};
        const Build_info build_info{
            .primitive_types = {
                .fill_triangles = true,
            },
            .buffer_info = {
                .normal_style       = Normal_style::corner_normals,
                .index_type         = Format::format_32_scalar_uint,
                .vertex_format      = plan.format,
                .vertex_buffer_sink = vertex_sink,
                .index_buffer_sink  = index_sink
            }
        };
        Buffer_mesh buffer_mesh;
        if (!build_buffer_mesh(buffer_mesh, mesh, build_info, element_mappings, Normal_style::corner_normals)) {
            return refuse("content build failed");
        }
        if (
            (buffer_mesh.vertex_buffer_ranges.size() != 1) ||
            (buffer_mesh.vertex_buffer_ranges.front().count != corner_count) ||
            (buffer_mesh.triangle_fill_indices.index_count != index_count) ||
            (element_mappings.triangle_to_mesh_facet.size() != static_cast<std::size_t>(facet_count)) ||
            (element_mappings.mesh_corner_to_vertex_buffer_index.size() != corner_count)
        ) {
            return refuse("unexpected content build layout");
        }
        const Buffer_range& vertex_range = buffer_mesh.vertex_buffer_ranges.front();
        const std::span<const std::byte> vertex_bytes = vertex_buffer.get_span().subspan(vertex_range.byte_offset, corner_count * stride);
        corner_vertex_data.resize(vertex_bytes.size());
        std::memcpy(corner_vertex_data.data(), vertex_bytes.data(), vertex_bytes.size());

        const Buffer_range& index_range = buffer_mesh.index_buffer_range;
        const std::size_t   first_index = buffer_mesh.triangle_fill_indices.first_index;
        corner_indices.resize(index_count);
        std::memcpy(
            corner_indices.data(),
            index_buffer.get_span().data() + index_range.byte_offset + (first_index * sizeof(uint32_t)),
            index_count * sizeof(uint32_t)
        );
    }

    // Corner of each built vertex, and so its geogram point.
    std::vector<GEO::index_t> point_from_built_vertex(corner_count, GEO::NO_INDEX);
    for (GEO::index_t corner = 0; corner < static_cast<GEO::index_t>(corner_count); ++corner) {
        const uint32_t vertex = element_mappings.mesh_corner_to_vertex_buffer_index[corner];
        if (vertex >= corner_count) {
            return refuse("unexpected corner mapping");
        }
        point_from_built_vertex[vertex] = mesh.facet_corners.vertex(corner);
    }

    // --- Drop degenerate triangles -----------------------------------------
    // Two corners of a facet are distinct points; only distinct points whose
    // float positions coincide make a degenerate core triangle.
    const Vertex_attribute* position_attribute = stream.find_attribute(Vertex_attribute_usage::position, 0);
    ERHE_VERIFY(position_attribute != nullptr);
    const auto same_position = [&](const uint32_t a, const uint32_t b) -> bool {
        return std::memcmp(
            corner_vertex_data.data() + (a * stride) + position_attribute->offset,
            corner_vertex_data.data() + (b * stride) + position_attribute->offset,
            3 * sizeof(float)
        ) == 0;
    };
    std::vector<uint32_t> kept_indices;
    std::vector<uint32_t> facet_from_kept_triangle;
    kept_indices.reserve(index_count);
    facet_from_kept_triangle.reserve(index_count / 3);
    std::size_t dropped_count = 0;
    for (std::size_t triangle = 0, end = index_count / 3; triangle < end; ++triangle) {
        const uint32_t v0 = corner_indices[(3 * triangle) + 0];
        const uint32_t v1 = corner_indices[(3 * triangle) + 1];
        const uint32_t v2 = corner_indices[(3 * triangle) + 2];
        if (same_position(v0, v1) || same_position(v0, v2) || same_position(v1, v2)) {
            ++dropped_count;
            continue;
        }
        kept_indices.push_back(v0);
        kept_indices.push_back(v1);
        kept_indices.push_back(v2);
        facet_from_kept_triangle.push_back(element_mappings.triangle_to_mesh_facet[triangle]);
    }
    if (kept_indices.empty()) {
        return refuse("only degenerate triangles");
    }

    // --- meshoptimizer passes ----------------------------------------------
    Prepared_primitive_export prepared;
    prepared.dropped_degenerate_triangle_count = dropped_count;
    std::vector<Mesh_optimize_stream> streams(1);
    streams.front().data   = std::move(corner_vertex_data);
    streams.front().stride = stride;
    std::vector<uint32_t> indices = std::move(kept_indices);
    Mesh_optimize_result  optimization;
    if (!optimize_indexed_mesh(streams, indices, plan.format, prepared.options, optimization)) {
        return refuse("optimizer refused the mesh");
    }
    const std::size_t vertex_count   = streams.front().data.size() / stride;
    const std::size_t triangle_count = indices.size() / 3;

    // --- vertex_points -----------------------------------------------------
    // The point of each output vertex is the point of its first built vertex
    // (all of them share the position bytes the weld compared), renumbered in
    // first-reference order over the output vertices.
    std::vector<GEO::index_t> point_from_vertex(vertex_count, GEO::NO_INDEX);
    for (std::size_t built_vertex = 0; built_vertex < corner_count; ++built_vertex) {
        const uint32_t vertex = optimization.vertex_remap[built_vertex];
        if (vertex == Mesh_optimize_result::no_vertex) {
            continue;
        }
        if (point_from_vertex[vertex] == GEO::NO_INDEX) {
            point_from_vertex[vertex] = point_from_built_vertex[built_vertex];
        }
    }
    {
        std::vector<uint32_t> renumbered(mesh.vertices.nb(), Vertex_colocation::no_point);
        uint32_t next_point = 0;
        prepared.vertex_points.resize(vertex_count);
        for (std::size_t vertex = 0; vertex < vertex_count; ++vertex) {
            const GEO::index_t point = point_from_vertex[vertex];
            if ((point == GEO::NO_INDEX) || (point >= mesh.vertices.nb())) {
                return refuse("output vertex without a point");
            }
            if (renumbered[point] == Vertex_colocation::no_point) {
                renumbered[point] = next_point++;
            }
            prepared.vertex_points[vertex] = renumbered[point];
        }
        prepared.point_count = next_point;
    }

    // --- Core soup, hash, bounding volume ----------------------------------
    prepared.core.vertex_format  = plan.format;
    prepared.core.vertex_data    = std::move(streams.front().data);
    prepared.core.index_data     = std::move(indices);
    prepared.core.primitive_type = Primitive_type::triangles;
    const std::vector<float> positions = get_float_positions(prepared.core);
    ERHE_VERIFY(positions.size() == (3 * vertex_count));
    prepared.content_hash = compute_mesh_content_hash(prepared.core.index_data, positions);
    {
        erhe::math::Point_vector_bounding_volume_source point_source{vertex_count};
        for (std::size_t vertex = 0; vertex < vertex_count; ++vertex) {
            point_source.add(positions[(3 * vertex) + 0], positions[(3 * vertex) + 1], positions[(3 * vertex) + 2]);
        }
        erhe::math::calculate_bounding_volume(point_source, prepared.bounding_box, prepared.bounding_sphere);
    }

    // --- BVH ---------------------------------------------------------------
    if (!erhe::raytrace::get_prebuilt_bvh_backend().empty()) {
        std::optional<erhe::raytrace::Prebuilt_bvh> bvh;
        if (dropped_count == 0) {
            std::vector<uint32_t> core_triangle_from_facet(facet_count, Mesh_optimize_result::no_vertex);
            for (std::size_t t = 0; t < triangle_count; ++t) {
                const uint32_t facet = facet_from_kept_triangle[optimization.triangle_permutation[t]];
                if (facet < static_cast<uint32_t>(facet_count)) {
                    core_triangle_from_facet[facet] = static_cast<uint32_t>(t);
                }
            }
            bvh = remap_committed_bvh(shape, core_triangle_from_facet, facet_count, prepared.core.index_data, positions);
            if (bvh.has_value()) {
                prepared.bvh_source = Prepared_bvh_source::reused;
            }
        }
        if (!bvh.has_value()) {
            bvh = build_core_bvh(prepared.core.index_data, positions);
            if (bvh.has_value()) {
                prepared.bvh_source = Prepared_bvh_source::rebuilt;
            }
        }
        prepared.bvh = std::move(bvh);
    }

    Prepare_primitive_result result;
    result.prepared = std::move(prepared);
    return result;
}

} // namespace erhe::primitive
