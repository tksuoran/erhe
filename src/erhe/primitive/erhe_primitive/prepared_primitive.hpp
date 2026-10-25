#pragma once

#include "erhe_primitive/mesh_optimizer.hpp"
#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_math/aabb.hpp"
#include "erhe_math/sphere.hpp"
#include "erhe_raytrace/prebuilt_bvh.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace erhe::primitive {

class Primitive_shape;

// How the BVH of a Prepared_primitive_export was obtained.
enum class Prepared_bvh_source : unsigned int {
    none    = 0, // no BVH: the raytrace backend cannot export one (embree, tinybvh, none) or the build failed
    reused  = 1, // the shape's committed raytrace BVH, primitive ids remapped onto the core triangle order
    rebuilt = 2, // built over the core triangles, because the committed one did not map 1:1 onto them
    stored  = 3  // a retained prepared primitive: the BVH its raytrace loaded with, exported back from the raytrace
};

[[nodiscard]] auto c_str(Prepared_bvh_source source) -> const char*;

// A soup primitive prepared for export with the ERHE_mesh_optimized,
// ERHE_mesh_topology and ERHE_raytrace_bvh glTF extensions
// (doc/plans/gltf_fast_load.md "Saved core primitive",
// doc/gltf_extensions/ERHE_mesh_optimized.md): the core accessors and the
// three payloads.
class Prepared_primitive_export
{
public:
    // The core primitive: TRIANGLES with uint32 indices and one interleaved
    // vertex stream holding POSITION (vec3 float), NORMAL (vec3 float),
    // TANGENT (vec4 float), and, as the source soup carries them,
    // TEXCOORD_0..2 (vec2 float), COLOR_0..1 (vec4 float), JOINTS_0..1
    // (vec4 uint8 / uint16, the soup's width) and WEIGHTS_0..1 (vec4 float).
    // Every value is what erhe's content build writes for the corner (the
    // Normal_style::corner_normals build of the shape's Geometry): positions
    // are the Geometry's colocated point positions, tangents include the ones
    // computed when the soup had none, WEIGHTS_0 is normalized. Welded
    // bitwise, no degenerate triangles, in meshoptimizer vertex-cache,
    // overdraw and vertex-fetch order.
    Triangle_soup         core;
    // Set instead of `core` (which then stays empty) for a retained
    // primitive: one loaded with prepared mesh data and not edited since
    // (Primitive_shape::is_prepared_data_current()). Its soup is the core the
    // file held, written back unchanged.
    std::shared_ptr<const Triangle_soup> retained_core;

    // ERHE_mesh_topology: the point of each core vertex, in first-reference
    // order (vertex_points[0] == 0, each value at most one more than the
    // largest before it, largest == point_count - 1).
    std::vector<uint32_t> vertex_points;
    uint32_t              point_count{0};

    // ERHE_mesh_optimized
    Mesh_optimize_options options;
    uint64_t              content_hash{0};
    erhe::math::Aabb      bounding_box{};
    erhe::math::Sphere    bounding_sphere{};

    // ERHE_raytrace_bvh: over the core triangles in index order. Empty when
    // bvh_source is none.
    std::optional<erhe::raytrace::Prebuilt_bvh> bvh;
    Prepared_bvh_source                         bvh_source{Prepared_bvh_source::none};

    // Triangles of the content build dropped because two of their corners
    // had bitwise-identical positions (distinct points that round to one
    // float position).
    std::size_t           dropped_degenerate_triangle_count{0};
};

class Prepare_primitive_result
{
public:
    std::optional<Prepared_primitive_export> prepared;
    // Why the primitive was not prepared; empty when it was.
    std::string                              skip_reason;
};

// Prepares the soup primitive of `shape` for export. A shape whose prepared
// mesh data is current (loaded with it, not edited since) is retained: its
// soup, stored colocation, marker values and the BVH exported from its
// raytrace are returned as they are (retained_core, Prepared_bvh_source::stored
// or none without a real raytrace), with no Geometry and no meshoptimizer.
// Otherwise it builds the shape's
// Geometry when it does not exist yet (Primitive_shape::get_geometry()), runs
// the content build of it into float CPU buffers, drops degenerate
// triangles, runs the meshoptimizer passes (optimize_indexed_mesh() with
// default Mesh_optimize_options), derives vertex_points from the Geometry,
// computes the content hash and the bounding volume, and takes the BVH from
// the shape's committed real raytrace (remapped through the triangle
// permutation) or, when that does not map 1:1 onto the core triangles,
// builds one over the core. Refuses (empty `prepared`, `skip_reason` set) a
// shape without a soup, with a non-triangle facet, with attributes the
// content build does not write (TEXCOORD_3+, COLOR_2+, JOINTS_2+ /
// WEIGHTS_2+, or a set with a gap such as TEXCOORD_1 without TEXCOORD_0),
// or with nothing to draw.
//
// Thread safety: shapes may be prepared in parallel; one shape is prepared
// by one caller at a time (the content build writes the facet id attribute
// of the Geometry, as every build does).
[[nodiscard]] auto prepare_primitive_for_export(Primitive_shape& shape) -> Prepare_primitive_result;

// The core to write: retained_core when set, otherwise core.
[[nodiscard]] auto get_export_core(const Prepared_primitive_export& prepared) -> const Triangle_soup&;

// The ERHE_mesh_optimized content_hash: 64-bit FNV-1a (erhe::hash::hash())
// over every index as 4 bytes little-endian, then every position as three
// float32 values, 4 bytes little-endian each, the bits as stored.
// positions_xyz holds three floats per vertex.
[[nodiscard]] auto compute_mesh_content_hash(std::span<const uint32_t> indices, std::span<const float> positions_xyz) -> uint64_t;

// The positions of a core soup (Prepared_primitive_export::core) as three
// floats per vertex. Empty when the soup has no vec3 float POSITION in its
// first stream.
[[nodiscard]] auto get_float_positions(const Triangle_soup& soup) -> std::vector<float>;

// Load side (doc/plans/gltf_fast_load.md "Loader"): the validated payloads
// of the ERHE_mesh_optimized, ERHE_mesh_topology and ERHE_raytrace_bvh
// extensions of one loaded soup primitive. It describes exactly the soup of
// the Primitive_shape that carries it (Primitive_shape::get_prepared_data())
// and is immutable once attached. Present only when ERHE_mesh_optimized was
// accepted; colocation and bvh are each present only when that extension was
// present and accepted as well.
class Prepared_primitive_data
{
public:
    // ERHE_mesh_optimized: the passes the core went through (weld is always
    // true), its content hash and bounding volumes. bounding_box comes from
    // the POSITION accessor min / max.
    Mesh_optimize_options options;
    uint64_t              content_hash{0};
    erhe::math::Aabb      bounding_box{};
    erhe::math::Sphere    bounding_sphere{};

    // ERHE_mesh_topology: the point of every core vertex, first-reference
    // order (every value is a point id; no Vertex_colocation::no_point).
    std::optional<Vertex_colocation> colocation;

    // ERHE_raytrace_bvh: the stored build for the running raytrace backend
    // and library tag, over the core triangles in index order.
    std::optional<erhe::raytrace::Prebuilt_bvh> bvh;
};

// The ERHE_mesh_optimized checks on the core content
// (doc/gltf_extensions/ERHE_mesh_optimized.md "Validation / fallback"): the
// index count is a multiple of 3, every index is below the vertex count
// (positions_xyz.size() / 3), every vertex is referenced, no triangle has two
// corners with bitwise-identical positions, and the content hash over
// indices and positions equals content_hash. Returns an empty string when
// the core passes, otherwise the rejection reason: a fixed phrase without
// numbers, so callers can count rejections by reason.
[[nodiscard]] auto validate_prepared_core(
    std::span<const uint32_t> indices,
    std::span<const float>    positions_xyz,
    uint64_t                  content_hash
) -> std::string;

// The ERHE_mesh_topology checks (doc/gltf_extensions/ERHE_mesh_topology.md
// "Validation / fallback") on a vertex_points payload already known to hold
// one value per core vertex: first-reference order, the distinct id count
// equals point_count, and every vertex has the bitwise-identical position of
// its point's first vertex. Returns an empty string when the payload passes,
// otherwise the rejection reason (a fixed phrase without numbers).
[[nodiscard]] auto validate_vertex_points(
    std::span<const uint32_t> vertex_points,
    uint32_t                  point_count,
    std::span<const float>    positions_xyz
) -> std::string;

} // namespace erhe::primitive
