#pragma once

#include "erhe_primitive/buffer_range.hpp"
#include "erhe_primitive/index_range.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace erhe::dataformat { class Vertex_format; }

namespace erhe::primitive {

class Buffer_info;
class Buffer_mesh;

// Mesh level of detail (doc/erhe/meshoptimizer_integration.md "Level of
// detail"). A LOD chain is a list of index ranges into the optimized
// variant's vertex buffer: level 0 is the optimized fill range, level i > 0 a
// simplification of level i - 1 over the same vertices, stored in the same
// index allocation. A level i > 0 also owns its edge lines: edge-line
// vertices (and joint side data when skinned) built from its triangles, in
// the optimized variant's edge-line allocations. Level 0's edge lines are the
// original variant's.

// Level 0 included.
static constexpr std::size_t max_lod_levels = 8;

// Which vertex attributes the simplifier's error metric weighs besides the
// positions.
enum class Lod_attributes : unsigned int
{
    positions_only     = 0,
    normals_and_colors = 1
};

// Whether vertices on the topological border (edges with one triangle) may
// move. Locked keeps meshes split into several primitives from cracking apart
// at the split (meshopt_SimplifyLockBorder).
enum class Lod_border : unsigned int
{
    free   = 0,
    locked = 1
};

// Whether collapses may cross attribute discontinuities (texture seams,
// hard normal edges). Permissive is meshopt_SimplifyPermissive.
enum class Lod_seams : unsigned int
{
    keep       = 0,
    permissive = 1
};

// What a level that misses its target index count falls back to. Sloppy runs
// meshopt_simplifySloppy, which neither keeps topology nor seams.
enum class Lod_fallback : unsigned int
{
    none   = 0,
    sloppy = 1
};

// Inputs of generate_lod_chain(). Kept on the Primitive (Primitive::lod_settings)
// so the rebuild after an edit regenerates the chain it had.
class Lod_settings
{
public:
    // Total number of levels, level 0 included; clamped to [1, max_lod_levels].
    std::size_t    level_count       {6};
    // Target index count of level i is ratio times the index count of level i - 1.
    float          ratio             {0.5f};
    Lod_attributes attributes        {Lod_attributes::normals_and_colors};
    float          normal_weight     {1.0f};
    float          color_weight      {1.0f};
    Lod_border     border            {Lod_border::locked};
    Lod_seams      seams             {Lod_seams::keep};
    Lod_fallback   fallback          {Lod_fallback::none};
    // Degrees. Before simplifying, vertices that share a position and every
    // other attribute (texcoords, color, tangent sign, joints) are welded into
    // one simplification vertex when their normals lie within this angle.
    float          normal_merge_angle{20.0f};

    [[nodiscard]] auto operator==(const Lod_settings& other) const -> bool = default;
};

class Lod_level
{
public:
    Index_range  index_range{};
    // Object space error of this level (meshoptimizer's relative error times
    // meshopt_simplifyScale()). 0 for level 0; strictly increasing.
    float        error{0.0f};
    // The level's edge lines (levels > 0; build_lod_edge_lines()): two
    // edge-line vertices per edge, and the matching joint side data when the
    // mesh is skinned. Empty for level 0, whose edge lines are the original
    // variant's, and when the build had no edge-line stream.
    Buffer_range edge_line_vertex_range{};
    Buffer_range edge_line_joint_range {};
};

// The levels of one optimized Buffer_mesh. Empty (level_count == 0) when no
// chain was generated: the mesh then has one implicit level, its fill range.
class Lod_chain
{
public:
    [[nodiscard]] auto is_empty  () const -> bool { return level_count == 0; }
    [[nodiscard]] auto get_levels() const -> std::span<const Lod_level> { return std::span<const Lod_level>{levels.data(), level_count}; }
    // The level a draw asking for `level` uses: clamped to the chain's last
    // level; 0 for an empty chain.
    [[nodiscard]] auto get_clamped_level(const std::size_t level) const -> std::size_t
    {
        return (level_count == 0) ? 0 : std::min(level, level_count - 1);
    }

    std::array<Lod_level, max_lod_levels> levels{};
    std::size_t                           level_count{0};
    float                                 generation_seconds{0.0f}; // generate_lod_chain() time
};

// Per vertex inputs of generate_lod_chain(), all float. Spans that are empty
// are absent.
class Lod_vertex_input
{
public:
    std::size_t            vertex_count{0};
    std::span<const float> positions;             // xyz per vertex
    std::span<const float> normals;               // xyz per vertex
    std::span<const float> colors;                // rgba per vertex
    // seam_attribute_stride floats per vertex: every attribute two vertices
    // must share bitwise to be welded (texcoords, color, tangent sign, joints).
    std::span<const float> seam_attributes;
    std::size_t            seam_attribute_stride{0};
    // xyzw per vertex; present for skinned vertices only.
    std::span<const float> joint_indices;
    std::span<const float> joint_weights;
};

// Owning storage for a Lod_vertex_input.
class Lod_vertex_data
{
public:
    [[nodiscard]] auto get_input() const -> Lod_vertex_input;

    std::size_t        vertex_count{0};
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> colors;
    std::vector<float> seam_attributes;
    std::size_t        seam_attribute_stride{0};
    std::vector<float> joint_indices;
    std::vector<float> joint_weights;
};

// Reads the LOD inputs of `vertex_count` vertices from vertex streams laid
// out as `source_format` (one byte span per stream). The seam attributes are
// the attributes of `key_format` other than positions, normals, tangents and
// bitangents (a tangent contributes its sign), read from where
// `source_format` stores them; pass the format the vertices are drawn with,
// which leaves out what never reaches the GPU (the per-corner facet id).
// Attributes are converted with erhe::dataformat::convert(). Returns empty
// data (vertex_count 0) when the source has no float-convertible position.
[[nodiscard]] auto extract_lod_vertex_data(
    std::span<const std::span<const uint8_t>> streams,
    const erhe::dataformat::Vertex_format&    source_format,
    const erhe::dataformat::Vertex_format&    key_format,
    std::size_t                               vertex_count
) -> Lod_vertex_data;

// The same data for the vertices of an optimized build: output vertex o takes
// the data of the first source vertex the forward remap sends to o.
// `forward_remap` is Mesh_optimize_result::vertex_remap (source -> output,
// Mesh_optimize_result::no_vertex for dropped vertices).
[[nodiscard]] auto remap_lod_vertex_data(
    const Lod_vertex_data&    source,
    std::span<const uint32_t> forward_remap,
    std::size_t               output_vertex_count
) -> Lod_vertex_data;

// Result of generate_lod_chain(): the levels after level 0.
class Lod_chain_indices
{
public:
    class Level
    {
    public:
        std::size_t first_index{0}; // into `indices`
        std::size_t index_count{0};
        float       error      {0.0f};
        // Into edge_line_vertex_data / edge_line_joint_data, in endpoints
        // (two per edge); set by build_lod_edge_lines().
        std::size_t first_edge_line_vertex{0};
        std::size_t edge_line_vertex_count{0};
    };

    [[nodiscard]] auto is_empty() const -> bool { return levels.empty(); }

    std::vector<uint32_t> indices; // levels 1.., back to back, into the input vertices
    std::vector<Level>    levels;  // at most max_lod_levels - 1
    float                 elapsed_seconds{0.0f};

    // The levels' edge lines, back to back (build_lod_edge_lines()):
    // edge_line_vertex_byte_size bytes per endpoint, and
    // edge_line_joint_byte_size bytes per endpoint when the input is skinned
    // (empty otherwise).
    std::vector<uint8_t>  edge_line_vertex_data;
    std::vector<uint8_t>  edge_line_joint_data;
    [[nodiscard]] auto get_edge_line_vertex_count() const -> std::size_t;
};

// Generates levels 1.. of a LOD chain for the triangles `indices` over
// `vertices`. Each level is simplified from the previous one with
// meshopt_simplifyWithAttributes(), over a simplification-only vertex set in
// which vertices are welded per Lod_settings::normal_merge_angle; the result is
// mapped back to the real vertices, picking for each triangle corner the
// welded vertex whose normal is closest to the triangle's normal. A level's
// error is max(previous * 1.5, step error, floor) * meshopt_simplifyScale(),
// so errors grow strictly. Generation stops when a step keeps 75% or more of
// its input triangles, or once 24 or fewer indices remain. With joints
// present, vertices whose joint set differs from a neighbour's are locked, so
// a collapse never blends influences.
//
// Empty for an input it cannot simplify: no vertices, an index count that is
// not a multiple of 3 or an index out of range.
[[nodiscard]] auto generate_lod_chain(
    const Lod_vertex_input&   vertices,
    std::span<const uint32_t> indices,
    const Lod_settings&       settings
) -> Lod_chain_indices;

// Builds the edge lines of every level of `lod_indices` from the level's
// triangles over `vertices`, appending them to its edge_line_vertex_data
// (and edge_line_joint_data when `vertices` has joints). Every edge of the
// level's triangles is written once, with the edges keyed by the positions
// of their endpoints, so vertices split by attribute seams do not duplicate
// an edge; each endpoint pair is packed by write_edge_line_endpoints() from
// the edge's first two triangles (a boundary edge mirrors its one triangle).
// An endpoint's joints are those of the first corner at its position over
// the whole chain (vertices sharing a position share their joints). A
// zero-area triangle is not a face: its edges are carried by its neighbours.
void build_lod_edge_lines(const Lod_vertex_input& vertices, Lod_chain_indices& lod_indices);

// The edge-line allocations of an optimized variant built with a chain, made
// inside the variant's allocation transaction (the caller holds
// buffer_mesh_allocation_mutex()): `lod_indices`' edge-line vertices from
// buffer_info.edge_line_vertex_stream and, when it has joint data, its joint
// side data from buffer_info.edge_line_joint_stream, held by
// buffer_mesh.edge_line_vertex_allocation / edge_line_joint_allocation (an
// optimized variant has no edge lines of its own, so these fields are free;
// its edge_line_*_buffer_range stay empty). The whole allocations are
// returned in `out_vertices` / `out_joints` for make_lod_chain(); empty when
// there is nothing to allocate or no stream. False when an allocation fails.
[[nodiscard]] auto allocate_lod_edge_lines(
    Buffer_mesh&             buffer_mesh,
    const Buffer_info&       buffer_info,
    const Lod_chain_indices& lod_indices,
    Buffer_range&            out_vertices,
    Buffer_range&            out_joints
) -> bool;

// Moves `lod_indices`' edge-line data into the ranges allocate_lod_edge_lines()
// returned (empty ranges enqueue nothing).
void enqueue_lod_edge_lines(
    const Buffer_info&  buffer_info,
    Lod_chain_indices&  lod_indices,
    const Buffer_range& vertices,
    const Buffer_range& joints
);

// The chain of a Buffer_mesh whose index allocation holds level 0
// (`level_0`) followed directly by `lod_indices.indices`, and whose
// edge-line allocations `edge_line_vertices` / `edge_line_joints` (empty when
// not allocated) hold `lod_indices`' edge-line data back to back.
[[nodiscard]] auto make_lod_chain(
    const Index_range&       level_0,
    const Lod_chain_indices& lod_indices,
    const Buffer_range&      edge_line_vertices,
    const Buffer_range&      edge_line_joints
) -> Lod_chain;

// Running totals over every chain log_lod_chain() has been called for.
class Lod_totals
{
public:
    std::size_t primitive_count{0};
    std::size_t level_count    {0}; // levels > 0
    std::size_t index_count    {0}; // indices of levels > 0
    double      elapsed_seconds{0.0};
};

// Thread safe: chains are generated on loader workers.
[[nodiscard]] auto get_lod_totals() -> Lod_totals;
void reset_lod_totals();

// One line per primitive: triangles and error per level. Also accumulates the
// totals above.
void log_lod_chain(std::string_view name, const Lod_chain& chain);

} // namespace erhe::primitive
