#pragma once

#include "erhe_primitive/lod.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

namespace erhe::dataformat { class Vertex_format; }

namespace erhe::primitive {

class Buffer_info;
class Buffer_mesh;
class Element_mappings;
class Primitive_render_shape;
class Triangle_soup;

// Which meshoptimizer passes to run. Pass order is fixed by the implementation
// (weld, vertex cache, overdraw, vertex fetch); these only select which of them
// are applied. Turning every pass off still produces a faithful copy of the
// source soup rather than nothing, so do not do that expecting to disable the
// feature - leave optimization off in config instead.
class Mesh_optimize_options
{
public:
    bool  weld              {true};
    bool  vertex_cache      {true};
    bool  overdraw          {true};
    bool  vertex_fetch      {true};
    float overdraw_threshold{1.05f};
};

// Before/after measurements from meshopt_analyze*(), for logging. The "after"
// fields stay zero when no optimization ran (Mesh_optimize_result::triangle_soup
// null) - they are never seeded from the "before" values, so a consumer can tell
// "unchanged" from "not measured".
class Mesh_optimize_statistics
{
public:
    // False when the ACMR / overdraw / fetch figures below were never taken -
    // a cache hit replays the derivation without running meshopt_analyze*(),
    // which costs a pass of its own and exists only for the log line. Without
    // this flag a hit is indistinguishable from an optimization that achieved
    // nothing at all.
    bool measured{false};

    std::size_t vertex_count_before{0};
    std::size_t vertex_count_after {0};
    std::size_t triangle_count     {0};

    float acmr_before        {0.0f}; // meshopt_analyzeVertexCache().acmr
    float acmr_after         {0.0f};
    float overdraw_before    {0.0f}; // meshopt_analyzeOverdraw().overdraw
    float overdraw_after     {0.0f};
    // Both measured against the OPTIMIZED format's stride, so they isolate what
    // the passes did. The optimized build is also 4 bytes per vertex narrower
    // than the source one (it drops the facet id), and crediting the reordering
    // with that would overstate it - the format change is a separate, known
    // saving.
    std::size_t fetch_bytes_before{0}; // meshopt_analyzeVertexFetch().bytes_fetched
    std::size_t fetch_bytes_after {0};

    float elapsed_seconds{0.0f};
};

class Mesh_optimize_result
{
public:
    // A source vertex no triangle references. Welding drops it, so it has no
    // output slot; anything composing through vertex_remap must carry the
    // sentinel through rather than index with it.
    static constexpr uint32_t no_vertex = 0xffffffffu;

    // Optimized copy of the source soup. Null when optimization did not run.
    std::shared_ptr<Triangle_soup> triangle_soup;

    // Triangle permutation: for each output triangle, the source triangle index
    // it came from. Size == triangle count.
    std::vector<uint32_t> triangle_permutation;

    // Forward source -> output vertex remap. Size == source vertex count.
    // Weld-merged source vertices all map to the same output slot; this is
    // exactly what Element_mappings corner composition needs. The output ->
    // source gather table is derivable from this, not the other way around.
    std::vector<uint32_t> vertex_remap;

    Mesh_optimize_statistics statistics;
};

// One vertex stream taking part in an optimization: the packed per-vertex
// bytes of that stream and the stride they are packed at. Rewritten in place
// by optimize_indexed_mesh().
class Mesh_optimize_stream
{
public:
    std::vector<uint8_t> data;
    std::size_t          stride{0};
};

// The weld / vertex-cache / overdraw / vertex-fetch core. Both optimization
// entry points funnel through this: optimize_triangle_soup() calls it with the
// soup's single stream, and the geometry path calls it with one stream per sink
// vertex stream, on the bytes the build already staged.
//
// `streams` and `indices` are rewritten in place, and `streams` shrinks to the
// output vertex count. `vertex_format` is only read to locate the position
// attribute for the overdraw pass, so it must describe `streams` one for one -
// same count, same order, same strides.
//
// Returns false, leaving both arguments untouched, for an input the optimizer
// refuses: no streams, a stride of 0 or over 256 (meshoptimizer's limit),
// streams of disagreeing vertex counts, an index count that is not a multiple
// of 3, or an index out of range - the last is a real hazard rather than
// paranoia, because every meshopt entry point indexes vertex_count-sized
// scratch arrays BY THE INDEX VALUES.
//
// The returned result carries the remaps, the triangle permutation and the
// statistics. Its `triangle_soup` is always null: this function knows nothing
// about soups, and the caller wraps the output however it needs to.
[[nodiscard]] auto optimize_indexed_mesh(
    std::vector<Mesh_optimize_stream>&     streams,
    std::vector<uint32_t>&                 indices,
    const erhe::dataformat::Vertex_format& vertex_format,
    const Mesh_optimize_options&           options,
    Mesh_optimize_result&                  result
) -> bool;

// optimize_indexed_mesh() through a filesystem cache keyed by the source
// content (every stream's stride and bytes, the indices, the vertex format and
// the options). Optimizing a large primitive costs on the order of 100 ms, and
// an import re-does that on every load of the same asset, so the derivation is
// stored and replayed instead.
//
// What is stored is the two REMAPS, not the optimized streams: they fully
// determine them (scatter every source vertex to the slot the forward remap
// names, rebuild indices as permutation-ordered source triples mapped through
// it) and they are what Element_mappings composition needs anyway. A hit
// therefore rewrites `streams` and `indices` to the bytes optimize_indexed_mesh()
// would have produced and fills `result` with the same remaps, without calling
// meshoptimizer at all; the entry stays small - two uint32 tables rather than
// whole vertex data. A hit's statistics have measured == false and carry the
// replay's elapsed time.
//
// The cache never fails a build. A missing, corrupt, truncated, mismatched or
// unwritable entry degrades to a plain optimize (and a rewrite attempt).
// `cache_directory` is created if needed; pass an empty path to bypass the
// cache entirely. Input optimize_indexed_mesh() refuses is never looked up.
//
// Concurrency: loader workers can race on the same entry. Writes go to a
// temporary file that is renamed into place, so a reader sees either the old
// entry or the new one, never a half-written one.
[[nodiscard]] auto optimize_indexed_mesh_cached(
    std::vector<Mesh_optimize_stream>&     streams,
    std::vector<uint32_t>&                 indices,
    const erhe::dataformat::Vertex_format& vertex_format,
    const Mesh_optimize_options&           options,
    const std::filesystem::path&           cache_directory,
    Mesh_optimize_result&                  result
) -> bool;

[[nodiscard]] auto optimize_triangle_soup(
    const Triangle_soup&         source,
    const Mesh_optimize_options& options
) -> Mesh_optimize_result;

// optimize_triangle_soup() through the cache of optimize_indexed_mesh_cached(),
// with the soup's single stream as the one stream. Empty `cache_directory`
// bypasses the cache.
[[nodiscard]] auto optimize_triangle_soup_cached(
    const Triangle_soup&         source,
    const Mesh_optimize_options& options,
    const std::filesystem::path& cache_directory
) -> Mesh_optimize_result;

// Rewrites `source` - which describes the ORDER of the soup that was optimized -
// into the equivalent description of the optimized soup, by composing each of
// its members through the remaps in `optimization`.
//
// The two are order-dependent descriptions of the same mesh, so they can never
// be shared between the two builds: a triangle index means a different triangle
// and a vertex buffer index a different vertex after reordering. Composing is
// what lets the optimized build carry a correct description without re-deriving
// one from geometry.
//
//   triangle_to_mesh_facet            through triangle_permutation (an output
//                                     triangle's facet is its source triangle's;
//                                     degenerate NO_INDEX entries carry through)
//   mesh_corner_to_vertex_buffer_index  through vertex_remap (weld-merged source
//   mesh_vertex_to_vertex_buffer_index  vertices land on one output slot, which
//                                     is exactly what a corner needs)
//
// An empty member composes to an empty member: the soup import path leaves the
// mesh_vertex_ table empty, and leaves all of them empty until geometry is
// parsed. A source entry that no output slot exists for - a vertex no triangle
// references, which welding drops - becomes GEO::NO_INDEX rather than an index
// into nothing.
//
// Passing an `optimization` whose triangle_permutation does not cover `source`'s
// triangles, or whose vertex_remap does not cover its vertex indices, is a
// programming error and aborts: it would mean composing against a different
// soup than the one the mappings describe.
[[nodiscard]] auto compose_element_mappings(
    const Element_mappings&     source,
    const Mesh_optimize_result& optimization
) -> Element_mappings;

// Optimizes `source_shape`'s triangle soup and returns a COMPLETE variant shape
// for it - soup optimized, Element_mappings composed onto the new order, buffer
// mesh built - ready to be attached as Primitive::optimized_render_shape.
// Returns null when there is nothing to attach: no soup, an input the optimizer
// refuses (see optimize_triangle_soup), or a buffer mesh that would not build.
//
// It returns rather than attaches on purpose. An optimized shape is visible to
// renderers the instant it is attached, and one attached without a mesh would
// resolve as present and then draw nothing, so the shape is finished first and
// published in one step by the caller - the same publish-once discipline the
// shapes themselves use. That is also why there is no failure path to unwind:
// a variant that could not be finished was simply never attached.
//
// `source_mappings` MUST describe the source shape's triangle SOUP - values
// that index its vertices and triangles - because that is what the remaps
// compose through. Pass `{}` for anything else. This is deliberately a
// parameter rather than read off the shape: a shape that has had a
// geometry-path build carries builder-produced mappings, which index the built
// vertex buffer instead and would compose into nonsense. At the import call
// site the shape has had no such build, and its mappings are empty until the
// deferred geometry finalize anyway.
//
// `cache_directory` is forwarded to optimize_triangle_soup_cached(); empty
// bypasses the cache.
//
// `source_shape` is only read from. `name` appears in the statistics log line.
//
// A non-null `lod_settings` also builds the variant's LOD chain
// (build_buffer_mesh_from_triangle_soup() with Lod_settings).
[[nodiscard]] auto make_optimized_render_shape(
    const Primitive_render_shape& source_shape,
    const Element_mappings&       source_mappings,
    const Mesh_optimize_options&  options,
    const Buffer_info&            buffer_info,
    const std::filesystem::path&  cache_directory,
    std::string_view              name,
    const Lod_settings*           lod_settings = nullptr
) -> std::shared_ptr<Primitive_render_shape>;

// The geometry path's counterpart to make_optimized_render_shape(): builds the
// optimized variant out of the bytes a Primitive_builder run already staged,
// instead of out of a triangle soup.
//
// `streams` are the CORNER-VERTEX PREFIX of each vertex stream, already gathered
// into Buffer_info::optimized_vertex_format (the content format minus the
// per-corner facet id - see there for why), and `fill_indices` the fill triangle
// indices into it. Both
// are consumed. The centroid-point vertices the build appends after the corners
// are deliberately NOT included, and neither are the edge-line, corner-point,
// centroid-point or expanded-fill index streams: the optimized variant is fill
// triangles only, because welding merges corners across facets and every one of
// those other streams exists per-facet precisely to keep them apart. The
// original build has them all, and get_resolved_renderable_mesh() sends any
// non-fill mode there.
//
// `source_mappings` are the ones that build produced - they index its vertex
// buffer, which IS the corner prefix these remaps are over, so composing them
// is correct here. (The soup path must pass `{}` instead; see
// make_optimized_render_shape(). The two are not interchangeable.)
//
// `source_buffer_mesh` supplies the bounding volumes and the per-joint bounding
// boxes. Reordering and welding move no vertex, so they are the same for both
// builds and are copied rather than recomputed.
//
// The passes go through optimize_indexed_mesh_cached() with
// buffer_info.mesh_optimize_cache_directory (empty: uncached).
//
// With a non-null `lod_settings` the variant also gets a LOD chain
// (Buffer_mesh::lod_chain): generate_lod_chain() runs on the optimized
// vertices and fill indices, with `lod_vertices` (the LOD inputs of the same
// corner prefix as `streams`, see Build_context::take_optimizable_snapshot())
// carried to the output vertices through the forward remap, and the index
// allocation holds the levels after level 0. Null builds exactly what it
// builds without LOD.
//
// Returns null when there is nothing to attach: an input optimize_indexed_mesh()
// refuses, or an allocation that failed. There is no half-built state to unwind
// - a variant that could not be finished is simply never returned.
[[nodiscard]] auto make_optimized_render_shape_from_staged_build(
    std::vector<Mesh_optimize_stream>&& streams,
    std::vector<uint32_t>&&             fill_indices,
    const Element_mappings&             source_mappings,
    const Buffer_mesh&                  source_buffer_mesh,
    const Buffer_info&                  buffer_info,
    std::string_view                    name,
    const Lod_settings*                 lod_settings = nullptr,
    Lod_vertex_data&&                   lod_vertices = Lod_vertex_data{}
) -> std::shared_ptr<Primitive_render_shape>;

// Encodes one object-space tangent frame into the optimized variant's 8-byte
// TBN attribute (erhe::dataformat::Vertex_tbn_encoding::quaternion16).
//
// The frame is (tangent, bitangent, normal) with the tangent Gram-Schmidt'ed
// against the normal and bitangent = cross(normal, tangent), so it is
// orthonormal and right-handed by construction; the mirrored case is the
// separate handedness bit, taken from tangent.w.
//
// Layout, structurally meshoptimizer's quaternion filter but NOT bit compatible
// with it - and deliberately so. meshopt_encodeFilterQuat() canonicalizes on
// the largest component and DISCARDS the quaternion's double-cover sign, which
// is the bit an engine would normally hide handedness in; and its decode
// recovers its scale from the w lane with `| 3`, so the spare bits there are
// not spare either. This encoder therefore owns both ends:
//
//   lane 0..2 : the three non-largest components, cyclically swizzled from the
//               largest, multiplied by sqrt(2) (each is at most 1/sqrt(2), so
//               this uses the full snorm16 range) and quantized as snorm16.
//               The quaternion is negated first if its largest component is
//               negative, which costs nothing: q and -q are the same rotation.
//   lane 3    : bits 0..1 = index of the omitted largest component,
//               bit 2     = handedness (set = negative), rest zero.
//
// The decoder is res/shaders/erhe_vertex_tbn.glsl and must match exactly.
[[nodiscard]] auto encode_tbn_quaternion(const glm::vec3& normal, const glm::vec4& tangent) -> std::array<int16_t, 4>;

// Version of the vertex attribute encoders (encode_tbn_quaternion(), the
// implicit-sum joint weights, the position and texcoord affines and their
// roundings): bumped whenever one of them writes different bits for the same
// vertex format. GPU-ready vertex streams of a file are used as stored only
// when their ERHE_gpu_vertex_streams `encoding_version` equals it
// (doc/gltf_extensions/ERHE_gpu_vertex_streams.md).
static constexpr int vertex_encoding_version = 1;

// One vertex's skinning influences, sorted so the SMALLEST weight is last.
//
// That ordering is what the implicit-sum weight encoding needs - the dropped
// influence has to be the one whose reconstruction error matters least - and it
// has the side benefit of being canonical, so two corners with the same
// influences in different author order now compare equal to the geometry path's
// bitwise weld.
class Joint_influences
{
public:
    std::array<uint8_t, 4> indices{0, 0, 0, 0};
    glm::vec4              weights{1.0f, 0.0f, 0.0f, 0.0f};
};

// Normalizes the weights (glTF requires this of the asset, but does not make it
// true) and sorts descending. A vertex with no positive weight collapses to
// "fully influenced by the first joint", which is what an unskinned vertex of a
// skinned mesh means.
[[nodiscard]] auto sort_joint_influences(const glm::vec4& indices, const glm::vec4& weights) -> Joint_influences;

// The three stored weights of Vertex_joint_weights_encoding::unorm16x3_implicit_sum,
// from ALREADY SORTED influences. Quantized so that the three plus the implied
// fourth sum to exactly one unorm16 unit, which is what lets the shader
// reconstruct the fourth as 1 - (w0 + w1 + w2) without drift.
[[nodiscard]] auto encode_implicit_sum_joint_weights(const glm::vec4& sorted_weights) -> std::array<uint16_t, 3>;

// Running totals over every primitive log_mesh_optimize_statistics() has been
// called for since the last reset. Per-primitive lines answer "what did this
// mesh gain"; this answers "what did the session gain", which is the figure
// phase 7 measures and the one a 30-line log makes you sum by hand.
//
// The averages are TRIANGLE-WEIGHTED, not per-primitive: ACMR and overdraw are
// per-triangle costs, so a mean over primitives would let a 4-triangle gizmo
// count as much as a 92 000-triangle chessboard.
class Mesh_optimize_totals
{
public:
    std::size_t primitive_count      {0}; // primitives optimized
    std::size_t measured_count       {0}; // of those, ones that ran the analysis
    std::size_t replayed_count       {0}; // of those, cache hits (no figures)
    std::size_t vertex_count_before  {0};
    std::size_t vertex_count_after   {0};
    std::size_t triangle_count       {0}; // over measured primitives only
    std::size_t fetch_bytes_before   {0};
    std::size_t fetch_bytes_after    {0};
    double      acmr_before_weighted {0.0}; // sum of acmr * triangle_count
    double      acmr_after_weighted  {0.0};
    double      overdraw_before_weighted{0.0};
    double      overdraw_after_weighted {0.0};
    double      elapsed_seconds      {0.0};

    // Triangle-weighted means, 0 when nothing measured.
    [[nodiscard]] auto acmr_before    () const -> float;
    [[nodiscard]] auto acmr_after     () const -> float;
    [[nodiscard]] auto overdraw_before() const -> float;
    [[nodiscard]] auto overdraw_after () const -> float;
};

// Thread safe: primitives are optimized on loader workers.
[[nodiscard]] auto get_mesh_optimize_totals() -> Mesh_optimize_totals;
void reset_mesh_optimize_totals();
// One summary line for the totals above. Logs nothing when no primitive has
// been optimized, so a session with the feature off stays silent.
void log_mesh_optimize_totals();

// Also accumulates into the running totals - see get_mesh_optimize_totals().
void log_mesh_optimize_statistics(std::string_view name, const Mesh_optimize_statistics& statistics);

} // namespace erhe::primitive
