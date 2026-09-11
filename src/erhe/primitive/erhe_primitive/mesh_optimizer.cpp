#include "erhe_primitive/mesh_optimizer.hpp"

#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/buffer_sink.hpp"
#include "erhe_primitive/index_range.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/primitive_log.hpp"
#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <meshoptimizer.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <numeric>
#include <unordered_map>

namespace erhe::primitive {

namespace {

// The vertex cache model meshopt_analyzeVertexCache() reports against. These
// are the generic values meshoptimizer's own demo uses; the documentation also
// lists vendor-specific models (NVidia 32/32/32, AMD 14/64/128, Intel 128/0/0).
// They affect only the reported ACMR, never any optimization.
constexpr unsigned int cache_size     = 16;
constexpr unsigned int warp_size      = 0;
constexpr unsigned int primgroup_size = 0;

// meshopt_generateVertexRemap() and meshopt_analyzeVertexFetch() both assert
// vertex_size <= 256.
constexpr std::size_t max_vertex_size = 256;

// meshopt_generateVertexRemapMulti() takes at most this many streams. erhe's
// sink formats are two or three, so this is a contract check, not a limit that
// bites.
constexpr std::size_t max_stream_count = 16;

// An ordered triangle triple, used to recover which source triangle each
// output triangle came from. The cache and overdraw passes reorder triangles
// but never rotate or rewrite a triple - vertex cache copies (a, b, c)
// positionally and overdraw memcpy's whole clusters - so an exact ordered
// match is the right key.
class Triangle_key
{
public:
    uint32_t a{0};
    uint32_t b{0};
    uint32_t c{0};

    [[nodiscard]] auto operator==(const Triangle_key& other) const -> bool
    {
        return (a == other.a) && (b == other.b) && (c == other.c);
    }
};

class Triangle_key_hash
{
public:
    [[nodiscard]] auto operator()(const Triangle_key& key) const -> std::size_t
    {
        // FNV-1a over the three indices.
        uint64_t hash = 1469598103934665603ull;
        const uint32_t values[3] = { key.a, key.b, key.c };
        for (const uint32_t value : values) {
            for (int byte = 0; byte < 4; ++byte) {
                hash ^= static_cast<uint64_t>((value >> (byte * 8)) & 0xffu);
                hash *= 1099511628211ull;
            }
        }
        return static_cast<std::size_t>(hash);
    }
};

// erhe::dataformat::convert() covers about half of the Format enum and its
// default branches are fatal, so anything outside this list must skip the
// overdraw pass rather than abort the process. These are the position formats
// erhe actually produces (glTF restricts POSITION to float, and the AABB
// quantization adds snorm16x3).
[[nodiscard]] auto is_convertible_position_format(const erhe::dataformat::Format format) -> bool
{
    switch (format) {
        case erhe::dataformat::Format::format_32_vec2_float:
        case erhe::dataformat::Format::format_32_vec3_float:
        case erhe::dataformat::Format::format_32_vec4_float:
        case erhe::dataformat::Format::format_16_vec2_snorm:
        case erhe::dataformat::Format::format_16_vec3_snorm:
        case erhe::dataformat::Format::format_16_vec4_snorm: return true;
        default:                                             return false;
    }
}

// Positions as tightly packed float3, which meshopt_optimizeOverdraw() and
// meshopt_analyzeOverdraw() require. Returns an empty vector when the vertex
// format has no position attribute or one this cannot read, in which case the
// overdraw pass is skipped - it is an ordering heuristic, so losing it costs
// quality, never correctness.
[[nodiscard]] auto make_float_positions(
    const erhe::dataformat::Vertex_format&   vertex_format,
    const std::vector<Mesh_optimize_stream>& streams,
    const std::size_t                        vertex_count
) -> std::vector<float>
{
    const erhe::dataformat::Attribute_stream position = vertex_format.find_attribute(
        erhe::dataformat::Vertex_attribute_usage::position, 0
    );
    if ((position.attribute == nullptr) || !is_convertible_position_format(position.attribute->format)) {
        return {};
    }
    // Which stream the position lives in. find_attribute() hands back a pointer
    // into vertex_format.streams, and `streams` describes those one for one, so
    // the index is that pointer's offset into the array.
    const std::size_t stream_index = static_cast<std::size_t>(position.stream - vertex_format.streams.data());
    if (stream_index >= streams.size()) {
        return {};
    }
    const Mesh_optimize_stream& stream = streams[stream_index];
    const std::size_t           stride = stream.stride;
    if (stream.data.size() < vertex_count * stride) {
        return {};
    }

    std::vector<float> positions(vertex_count * 3, 0.0f);
    const uint8_t* const base = stream.data.data() + position.attribute->offset;
    for (std::size_t vertex = 0; vertex < vertex_count; ++vertex) {
        float value[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        erhe::dataformat::convert(
            base + vertex * stride,
            position.attribute->format,
            &value[0],
            erhe::dataformat::Format::format_32_vec4_float,
            1.0f
        );
        positions[vertex * 3 + 0] = value[0];
        positions[vertex * 3 + 1] = value[1];
        positions[vertex * 3 + 2] = value[2];
    }
    return positions;
}

} // anonymous namespace

auto optimize_indexed_mesh(
    std::vector<Mesh_optimize_stream>&     streams,
    std::vector<uint32_t>&                 indices,
    const erhe::dataformat::Vertex_format& vertex_format,
    const Mesh_optimize_options&           options,
    Mesh_optimize_result&                  result
) -> bool
{
    ERHE_PROFILE_FUNCTION();

    const std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();

    // --- Validate ---------------------------------------------------------
    // Everything below indexes vertex_count-sized scratch arrays BY THE INDEX
    // VALUES, so the input is checked up front rather than trusted; erhe does
    // not validate glTF indices anywhere on the import path. Every rejection
    // returns before a single byte of `streams` or `indices` is touched.
    if (streams.empty() || (streams.size() != vertex_format.streams.size()) || (streams.size() > max_stream_count)) {
        return false;
    }
    std::size_t total_stride    = 0;
    std::size_t vertex_count_in = 0;
    for (std::size_t stream_index = 0, stream_end = streams.size(); stream_index < stream_end; ++stream_index) {
        const Mesh_optimize_stream& stream = streams[stream_index];
        if (
            (stream.stride == 0) ||
            (stream.stride > max_vertex_size) ||
            (stream.stride != vertex_format.streams[stream_index].stride) ||
            ((stream.data.size() % stream.stride) != 0)
        ) {
            return false;
        }
        const std::size_t stream_vertex_count = stream.data.size() / stream.stride;
        if (stream_index == 0) {
            vertex_count_in = stream_vertex_count;
        } else if (stream_vertex_count != vertex_count_in) {
            // Streams are written in lockstep by every producer; disagreeing
            // counts mean one of them was built from something else.
            return false;
        }
        total_stride += stream.stride;
    }
    if ((vertex_count_in == 0) || indices.empty() || ((indices.size() % 3) != 0)) {
        return false;
    }
    const std::size_t index_count    = indices.size();
    const std::size_t triangle_count = index_count / 3;
    const uint32_t    max_index      = *std::max_element(indices.begin(), indices.end());
    if (static_cast<std::size_t>(max_index) >= vertex_count_in) {
        return false;
    }

    result.statistics.vertex_count_before = vertex_count_in;
    result.statistics.triangle_count      = triangle_count;

    {
        const meshopt_VertexCacheStatistics before_cache = meshopt_analyzeVertexCache(
            indices.data(), index_count, vertex_count_in, cache_size, warp_size, primgroup_size
        );
        const meshopt_VertexFetchStatistics before_fetch = meshopt_analyzeVertexFetch(
            indices.data(), index_count, vertex_count_in, total_stride
        );
        result.statistics.acmr_before        = before_cache.acmr;
        result.statistics.fetch_bytes_before = before_fetch.bytes_fetched;

        const std::vector<float> before_positions = make_float_positions(vertex_format, streams, vertex_count_in);
        if (!before_positions.empty()) {
            const meshopt_OverdrawStatistics before_overdraw = meshopt_analyzeOverdraw(
                indices.data(), index_count, before_positions.data(), vertex_count_in, 3 * sizeof(float)
            );
            result.statistics.overdraw_before = before_overdraw.overdraw;
        }
    }

    // --- Weld -------------------------------------------------------------
    // Bitwise equality over every stream's whole per-vertex bytes, so joints
    // and weights take part in the compare and skinned meshes are safe by
    // construction. Unreferenced source vertices come back as no_vertex and are
    // dropped.
    //
    // Precondition: the compare covers every stride byte INCLUDING inter-attribute
    // and tail padding, so a producer that leaves padding uninitialized simply
    // welds fewer vertices - a silent loss of the optimization, never a wrong
    // result. The glTF importer and the primitive builder both zero-fill
    // (resize() on a fresh vector) and write only attribute bytes.
    std::vector<uint32_t> weld_remap(vertex_count_in, Mesh_optimize_result::no_vertex);
    std::size_t           vertex_count_welded = vertex_count_in;

    if (options.weld) {
        std::vector<meshopt_Stream> meshopt_streams;
        meshopt_streams.reserve(streams.size());
        for (const Mesh_optimize_stream& stream : streams) {
            // size == stride: compare the whole per-vertex byte range of this
            // stream, padding included.
            meshopt_streams.push_back(meshopt_Stream{stream.data.data(), stream.stride, stream.stride});
        }
        vertex_count_welded = meshopt_generateVertexRemapMulti(
            weld_remap.data(), indices.data(), index_count, vertex_count_in, meshopt_streams.data(), meshopt_streams.size()
        );
        // In-place: unlike its neighbours meshopt_remapIndexBuffer() makes no
        // documented in-place promise, but its implementation is an element-wise
        // forward loop (indexgenerator.cpp), verified against the pinned v1.2.
        // Re-check on a version bump.
        meshopt_remapIndexBuffer(indices.data(), indices.data(), index_count, weld_remap.data());
        for (Mesh_optimize_stream& stream : streams) {
            std::vector<uint8_t> welded(vertex_count_welded * stream.stride);
            meshopt_remapVertexBuffer(welded.data(), stream.data.data(), vertex_count_in, stream.stride, weld_remap.data());
            stream.data = std::move(welded);
        }
    } else {
        std::iota(weld_remap.begin(), weld_remap.end(), 0u);
    }

    // The triangle triples as they stand after welding, which is still source
    // triangle order - remapIndexBuffer rewrites indices without reordering.
    // This is the table the permutation is recovered against.
    //
    // One map plus two flat arrays rather than a map of vectors: on a large
    // asset the per-triangle vector allocation dominated the whole pass.
    // `head` holds the first source triangle for a triple and doubles as the
    // consumption cursor; `next_same_triple` chains the rest in source order.
    std::unordered_map<Triangle_key, uint32_t, Triangle_key_hash> head;
    std::vector<uint32_t> next_same_triple(triangle_count, Mesh_optimize_result::no_vertex);
    head.reserve(triangle_count);
    {
        std::vector<uint32_t> tail;
        tail.resize(triangle_count, Mesh_optimize_result::no_vertex);
        std::size_t collision_count = 0;
        for (std::size_t triangle = 0; triangle < triangle_count; ++triangle) {
            const Triangle_key key{indices[triangle * 3 + 0], indices[triangle * 3 + 1], indices[triangle * 3 + 2]};
            const std::pair<std::unordered_map<Triangle_key, uint32_t, Triangle_key_hash>::iterator, bool> inserted =
                head.emplace(key, static_cast<uint32_t>(triangle));
            if (inserted.second) {
                tail[triangle] = static_cast<uint32_t>(triangle);
            } else {
                // Append, so candidates are consumed in source order.
                const uint32_t first = inserted.first->second;
                next_same_triple[tail[first]] = static_cast<uint32_t>(triangle);
                tail[first] = static_cast<uint32_t>(triangle);
                ++collision_count;
            }
        }
        if (collision_count > 0) {
            // Two distinct source facets can produce a bitwise-identical triple
            // once welding merges coincident geometry. Either candidate gives
            // the same rendering, but triangle_to_mesh_facet composes through
            // this permutation, so facet IDENTITY is ambiguous for them and
            // picking may report the other facet. Geometrically indistinguishable,
            // but worth knowing whether real assets hit it.
            log_primitive->trace(
                "mesh optimize: {} of {} triangles share a triple with an earlier one; facet identity is ambiguous for those",
                collision_count,
                triangle_count
            );
        }
    }

    // --- Vertex cache order ----------------------------------------------
    if (options.vertex_cache) {
        meshopt_optimizeVertexCache(indices.data(), indices.data(), index_count, vertex_count_welded);
    }

    // --- Overdraw order ---------------------------------------------------
    if (options.overdraw) {
        const std::vector<float> positions = make_float_positions(vertex_format, streams, vertex_count_welded);
        if (!positions.empty()) {
            meshopt_optimizeOverdraw(
                indices.data(), indices.data(), index_count, positions.data(), vertex_count_welded, 3 * sizeof(float), options.overdraw_threshold
            );
        }
    }

    // --- Recover the triangle permutation ---------------------------------
    // Both passes above permute triangles; neither rewrites a triple. Consume
    // matches in emit order, which is deterministic - a collision means two
    // bitwise-identical (duplicate or degenerate) triangles, and picking either
    // is equally correct.
    result.triangle_permutation.resize(triangle_count);
    for (std::size_t triangle = 0; triangle < triangle_count; ++triangle) {
        const Triangle_key key{indices[triangle * 3 + 0], indices[triangle * 3 + 1], indices[triangle * 3 + 2]};
        const std::unordered_map<Triangle_key, uint32_t, Triangle_key_hash>::iterator i = head.find(key);
        // Unreachable: both passes preserve the multiset of triples exactly, so
        // every emitted triple still has an unconsumed source triangle.
        ERHE_VERIFY(i != head.end());
        ERHE_VERIFY(i->second != Mesh_optimize_result::no_vertex);
        const uint32_t source_triangle = i->second;
        result.triangle_permutation[triangle] = source_triangle;
        // Advance the cursor to the next triangle sharing this triple.
        i->second = next_same_triple[source_triangle];
    }

    // --- Vertex fetch order -----------------------------------------------
    // The Remap form, not optimizeVertexFetch() proper: that one is single
    // stream by contract, and the remap is needed anyway to compose the forward
    // source -> output vertex mapping below.
    std::vector<uint32_t> fetch_remap(vertex_count_welded, Mesh_optimize_result::no_vertex);
    std::size_t           vertex_count_out = vertex_count_welded;
    if (options.vertex_fetch) {
        vertex_count_out = meshopt_optimizeVertexFetchRemap(fetch_remap.data(), indices.data(), index_count, vertex_count_welded);
        meshopt_remapIndexBuffer(indices.data(), indices.data(), index_count, fetch_remap.data());
        for (Mesh_optimize_stream& stream : streams) {
            std::vector<uint8_t> fetched(vertex_count_out * stream.stride);
            meshopt_remapVertexBuffer(fetched.data(), stream.data.data(), vertex_count_welded, stream.stride, fetch_remap.data());
            stream.data = std::move(fetched);
        }
    } else {
        std::iota(fetch_remap.begin(), fetch_remap.end(), 0u);
        // The weld branch already shrank the streams; this one did not run, so
        // trim them to the count the output is described by.
        for (Mesh_optimize_stream& stream : streams) {
            stream.data.resize(vertex_count_out * stream.stride);
        }
    }

    // --- Compose the forward source -> output vertex remap ------------------
    // Weld-merged source vertices land on the same output slot by construction,
    // which is exactly what per-corner Element_mappings composition needs.
    result.vertex_remap.resize(vertex_count_in);
    for (std::size_t vertex = 0; vertex < vertex_count_in; ++vertex) {
        const uint32_t welded = weld_remap[vertex];
        result.vertex_remap[vertex] = (welded == Mesh_optimize_result::no_vertex)
            ? Mesh_optimize_result::no_vertex
            : fetch_remap[welded];
    }

    result.statistics.vertex_count_after = vertex_count_out;
    {
        const meshopt_VertexCacheStatistics after_cache = meshopt_analyzeVertexCache(
            indices.data(), index_count, vertex_count_out, cache_size, warp_size, primgroup_size
        );
        const meshopt_VertexFetchStatistics after_fetch = meshopt_analyzeVertexFetch(
            indices.data(), index_count, vertex_count_out, total_stride
        );
        result.statistics.acmr_after        = after_cache.acmr;
        result.statistics.fetch_bytes_after = after_fetch.bytes_fetched;

        const std::vector<float> after_positions = make_float_positions(vertex_format, streams, vertex_count_out);
        if (!after_positions.empty()) {
            const meshopt_OverdrawStatistics after_overdraw = meshopt_analyzeOverdraw(
                indices.data(), index_count, after_positions.data(), vertex_count_out, 3 * sizeof(float)
            );
            result.statistics.overdraw_after = after_overdraw.overdraw;
        }
    }

    result.statistics.measured        = true;
    result.statistics.elapsed_seconds = std::chrono::duration<float>{std::chrono::steady_clock::now() - start_time}.count();
    return true;
}

auto optimize_triangle_soup(const Triangle_soup& source, const Mesh_optimize_options& options) -> Mesh_optimize_result
{
    ERHE_PROFILE_FUNCTION();

    Mesh_optimize_result result{};

    // Match Triangle_soup::get_vertex_count() and mesh_from_triangle_soup(),
    // which both assume the soup has exactly one stream, bound at 0.
    const erhe::dataformat::Vertex_stream* stream = source.vertex_format.get_stream(0);
    if ((source.vertex_format.streams.size() != 1) || (stream == nullptr)) {
        return result;
    }
    if (source.primitive_type != Primitive_type::triangles) {
        return result;
    }

    // Everything else the optimizer refuses - stride, vertex counts, index
    // range - is checked by optimize_indexed_mesh(), which is the one place
    // those rules live.
    std::vector<Mesh_optimize_stream> streams;
    streams.push_back(Mesh_optimize_stream{.data = source.vertex_data, .stride = stream->stride});
    std::vector<uint32_t> indices = source.index_data;
    if (!optimize_indexed_mesh(streams, indices, source.vertex_format, options, result)) {
        return Mesh_optimize_result{};
    }

    std::shared_ptr<Triangle_soup> optimized = std::make_shared<Triangle_soup>();
    optimized->vertex_format  = source.vertex_format;
    optimized->primitive_type = source.primitive_type;
    optimized->vertex_data    = std::move(streams.front().data);
    optimized->index_data     = std::move(indices);
    result.triangle_soup      = std::move(optimized);
    return result;
}

namespace {

// One member of an Element_mappings that indexes VERTEX BUFFER slots, composed
// through the source -> output vertex remap. A source slot that welding dropped
// - nothing referenced it - has no output slot, so it becomes NO_INDEX rather
// than an index into nothing.
[[nodiscard]] auto compose_vertex_buffer_indices(
    const std::vector<uint32_t>& source,
    const std::vector<uint32_t>& vertex_remap
) -> std::vector<uint32_t>
{
    std::vector<uint32_t> composed;
    composed.resize(source.size());
    for (std::size_t i = 0, end = source.size(); i < end; ++i) {
        const uint32_t source_vertex = source[i];
        if (source_vertex == GEO::NO_INDEX) {
            composed[i] = GEO::NO_INDEX;
            continue;
        }
        // Composing against a remap that does not cover this index would mean
        // the mappings describe a different soup than the one optimized.
        ERHE_VERIFY(source_vertex < vertex_remap.size());
        const uint32_t output_vertex = vertex_remap[source_vertex];
        composed[i] = (output_vertex == Mesh_optimize_result::no_vertex) ? GEO::NO_INDEX : output_vertex;
    }
    return composed;
}

} // anonymous namespace

auto compose_element_mappings(
    const Element_mappings&     source,
    const Mesh_optimize_result& optimization
) -> Element_mappings
{
    ERHE_PROFILE_FUNCTION();

    Element_mappings composed;

    if (!source.triangle_to_mesh_facet.empty()) {
        // The permutation names, for each OUTPUT triangle, the source triangle
        // it came from - so the output table is indexed by output triangle and
        // reads the source table at that source triangle.
        const std::vector<uint32_t>& permutation = optimization.triangle_permutation;
        composed.triangle_to_mesh_facet.resize(permutation.size());
        for (std::size_t triangle = 0, end = permutation.size(); triangle < end; ++triangle) {
            const uint32_t source_triangle = permutation[triangle];
            ERHE_VERIFY(source_triangle < source.triangle_to_mesh_facet.size());
            composed.triangle_to_mesh_facet[triangle] = source.triangle_to_mesh_facet[source_triangle];
        }
    }

    composed.mesh_corner_to_vertex_buffer_index = compose_vertex_buffer_indices(
        source.mesh_corner_to_vertex_buffer_index,
        optimization.vertex_remap
    );
    composed.mesh_vertex_to_vertex_buffer_index = compose_vertex_buffer_indices(
        source.mesh_vertex_to_vertex_buffer_index,
        optimization.vertex_remap
    );

    return composed;
}

auto make_optimized_render_shape(
    const Primitive_render_shape& source_shape,
    const Element_mappings&       source_mappings,
    const Mesh_optimize_options&  options,
    const Buffer_info&            buffer_info,
    const std::filesystem::path&  cache_directory,
    const std::string_view        name
) -> std::shared_ptr<Primitive_render_shape>
{
    ERHE_PROFILE_FUNCTION();

    const std::shared_ptr<Triangle_soup>& source_soup = source_shape.get_triangle_soup();
    if (!source_soup) {
        // Procedural primitives are built from Geometry and carry no soup.
        // Optimizing those is the geometry path, not this one.
        return {};
    }
    if (buffer_info.optimized_vertex_format == nullptr) {
        return {};
    }

    // Built in the optimized format, exactly like the geometry path's variant:
    // "an optimized variant never carries a facet id" has to hold on BOTH paths
    // or it is not an invariant, just a habit. The soup carries no facet id to
    // begin with, so this costs nothing here beyond the narrower stride.
    const Buffer_info optimized_buffer_info{
        .normal_style       = buffer_info.normal_style,
        .index_type         = buffer_info.index_type,
        .vertex_format      = *buffer_info.optimized_vertex_format,
        .vertex_buffer_sink = buffer_info.vertex_buffer_sink,
        .index_buffer_sink  = buffer_info.index_buffer_sink,
        .vertex_input_key   = buffer_info.optimized_vertex_input_key
        // The edge-line streams and the expanded format are deliberately left
        // null: this build is fill triangles only, and a soup build reads
        // none of them.
    };

    Mesh_optimize_result optimization = optimize_triangle_soup_cached(*source_soup.get(), options, cache_directory);
    if (!optimization.triangle_soup) {
        return {};
    }
    log_mesh_optimize_statistics(name, optimization.statistics);

    // The caller's mappings describe the source order; the variant needs the
    // same correspondence expressed in the optimized order. Empty composes to
    // empty, which is what the import path passes - see the note on the
    // declaration for why these are not read off the shape.
    Element_mappings composed = compose_element_mappings(source_mappings, optimization);

    std::shared_ptr<Primitive_render_shape> shape = std::make_shared<Primitive_render_shape>(
        optimization.triangle_soup,
        std::move(composed)
    );
    if (!shape->make_buffer_mesh(optimized_buffer_info)) {
        log_primitive->warn("mesh optimize {}: the optimized buffer mesh did not build; rendering the source build", name);
        return {};
    }
    return shape;
}

namespace {

// The fill indices packed into the sink's index format. 32-bit is what
// Mesh_memory uses, so the narrower cases exist for the CPU-buffer sinks.
[[nodiscard]] auto pack_indices(
    const std::vector<uint32_t>&   indices,
    const erhe::dataformat::Format index_type
) -> std::vector<uint8_t>
{
    const std::size_t    element_size = erhe::dataformat::get_format_size_bytes(index_type);
    std::vector<uint8_t> packed(indices.size() * element_size, uint8_t{0});
    for (std::size_t i = 0, end = indices.size(); i < end; ++i) {
        uint8_t* const destination = packed.data() + i * element_size;
        switch (index_type) {
            case erhe::dataformat::Format::format_8_scalar_uint:  *destination = static_cast<uint8_t>(indices[i]); break;
            case erhe::dataformat::Format::format_16_scalar_uint: {
                const uint16_t value = static_cast<uint16_t>(indices[i]);
                std::memcpy(destination, &value, sizeof(value));
                break;
            }
            case erhe::dataformat::Format::format_32_scalar_uint: {
                const uint32_t value = indices[i];
                std::memcpy(destination, &value, sizeof(value));
                break;
            }
            default: ERHE_FATAL("bad index type");
        }
    }
    return packed;
}

} // anonymous namespace

auto make_optimized_render_shape_from_staged_build(
    std::vector<Mesh_optimize_stream>&& streams_in,
    std::vector<uint32_t>&&             fill_indices_in,
    const Element_mappings&             source_mappings,
    const Buffer_mesh&                  source_buffer_mesh,
    const Buffer_info&                  buffer_info,
    const std::string_view              name
) -> std::shared_ptr<Primitive_render_shape>
{
    ERHE_PROFILE_FUNCTION();

    std::vector<Mesh_optimize_stream> streams      = std::move(streams_in);
    std::vector<uint32_t>             fill_indices = std::move(fill_indices_in);

    // Everything below is in the OPTIMIZED format: the caller gathered the
    // staged bytes into it, the weld compares its strides, and the allocation
    // and vertex input key come from it.
    if (buffer_info.optimized_vertex_format == nullptr) {
        return {};
    }
    const erhe::dataformat::Vertex_format& vertex_format = *buffer_info.optimized_vertex_format;

    Mesh_optimize_result optimization{};
    if (!optimize_indexed_mesh(streams, fill_indices, vertex_format, buffer_info.mesh_optimize_options, optimization)) {
        return {};
    }
    log_mesh_optimize_statistics(name, optimization.statistics);

    const std::size_t vertex_count = optimization.statistics.vertex_count_after;
    const std::size_t index_count  = fill_indices.size();
    if ((vertex_count == 0) || (index_count == 0)) {
        return {};
    }

    // The source mappings index the source build's vertex buffer, which is the
    // corner prefix these remaps are over, so this composes onto the optimized
    // order directly.
    Element_mappings composed = compose_element_mappings(source_mappings, optimization);

    Buffer_mesh buffer_mesh;
    {
        // One atomic multi-pool allocation transaction per mesh - see
        // buffer_mesh_allocation_mutex(). Same transaction shape as
        // Build_context_root::allocate_buffers() and
        // build_buffer_mesh_from_triangle_soup().
        const std::lock_guard<std::mutex> allocation_lock{buffer_mesh_allocation_mutex()};

        Buffer_sink_allocation index_allocation = buffer_info.index_buffer_sink.allocate_index_buffer_range(buffer_info.index_type, index_count);
        if (index_allocation.range.count == 0) {
            log_primitive->warn("mesh optimize {}: out of index memory for the optimized variant; rendering the source build", name);
            return {};
        }
        buffer_mesh.index_buffer_range = index_allocation.range;
        buffer_mesh.index_allocation   = std::move(index_allocation.allocation);

        for (const erhe::dataformat::Vertex_stream& sink_stream : vertex_format.streams) {
            Buffer_sink_allocation vertex_allocation = buffer_info.vertex_buffer_sink.allocate_vertex_buffer_range(sink_stream, vertex_count);
            if (vertex_allocation.range.count == 0) {
                log_primitive->warn("mesh optimize {}: out of vertex memory for the optimized variant; rendering the source build", name);
                return {};
            }
            buffer_mesh.vertex_buffer_ranges.emplace_back(vertex_allocation.range);
            buffer_mesh.vertex_allocations.emplace_back(std::move(vertex_allocation.allocation));
        }
    }

    // Lockstep invariant: one indirect-draw vertexOffset (from stream 0) is
    // applied to every binding, so byte_offset / stride and the block index
    // must match across all streams. Drop the variant rather than render
    // non-position attributes from the wrong offsets - the source build is
    // still there and correct.
    if (!buffer_mesh.vertex_buffer_ranges.empty()) {
        const Buffer_range& range_0     = buffer_mesh.vertex_buffer_ranges.front();
        const std::size_t   base_vertex = range_0.byte_offset / range_0.element_size;
        for (std::size_t stream = 1, stream_end = buffer_mesh.vertex_buffer_ranges.size(); stream < stream_end; ++stream) {
            const Buffer_range& range = buffer_mesh.vertex_buffer_ranges[stream];
            if (
                ((range.byte_offset % range.element_size) != 0)           ||
                ((range.byte_offset / range.element_size) != base_vertex) ||
                (range.buffer_id != range_0.buffer_id)
            ) {
                log_primitive->error(
                    "mesh optimize {}: optimized variant vertex stream allocations out of lockstep - variant dropped",
                    name
                );
                return {};
            }
        }
    }

    buffer_info.index_buffer_sink.enqueue_index_data(buffer_mesh.index_buffer_range, pack_indices(fill_indices, buffer_info.index_type));
    for (std::size_t stream = 0, stream_end = streams.size(); stream < stream_end; ++stream) {
        buffer_info.vertex_buffer_sink.enqueue_vertex_data(buffer_mesh.vertex_buffer_ranges[stream], std::move(streams[stream].data));
    }

    buffer_mesh.triangle_fill_indices = Index_range{
        .primitive_type = Primitive_type::triangles,
        .first_index    = 0,
        .index_count    = index_count
    };
    buffer_mesh.vertex_input_key = buffer_info.optimized_vertex_input_key;
    // Welding and reordering move no vertex, so the source build's volumes
    // describe this one exactly.
    buffer_mesh.bounding_box        = source_buffer_mesh.bounding_box;
    buffer_mesh.bounding_sphere     = source_buffer_mesh.bounding_sphere;
    buffer_mesh.joint_bounding_boxes = source_buffer_mesh.joint_bounding_boxes;
    // The UV ranges the caller encoded against. They must travel with the mesh:
    // the per-primitive record writer derives the decode affine from THIS
    // Buffer_mesh, and it has to be bit-identical to the one the encode used.
    buffer_mesh.texcoord_ranges     = source_buffer_mesh.texcoord_ranges;
    buffer_mesh.has_vertex_colors   = source_buffer_mesh.has_vertex_colors;

    return std::make_shared<Primitive_render_shape>(std::move(buffer_mesh), std::move(composed));
}

auto encode_tbn_quaternion(const glm::vec3& normal_in, const glm::vec4& tangent_in) -> std::array<int16_t, 4>
{
    constexpr float epsilon = 1e-8f;

    // Normal. A degenerate one cannot produce a frame at all; pick a fixed axis
    // rather than emit NaN - the source build is unaffected and still correct.
    const float normal_length = glm::length(normal_in);
    const glm::vec3 n = (normal_length > epsilon)
        ? (normal_in / normal_length)
        : glm::vec3{0.0f, 0.0f, 1.0f};

    // Tangent, orthogonalized. erhe does not guarantee the stored tangent is
    // perpendicular to the normal, and a quaternion can only carry an
    // orthonormal frame, so this Gram-Schmidt is part of the encoding, not an
    // optimization.
    glm::vec3 t = glm::vec3{tangent_in.x, tangent_in.y, tangent_in.z};
    t = t - n * glm::dot(n, t);
    const float tangent_length = glm::length(t);
    if (tangent_length > epsilon) {
        t = t / tangent_length;
    } else {
        // No usable tangent (absent, zero, or parallel to the normal): any
        // vector orthogonal to n will do. Materials that actually need a
        // tangent frame carry a real tangent.
        const glm::vec3 axis = (std::abs(n.x) < 0.9f) ? glm::vec3{1.0f, 0.0f, 0.0f} : glm::vec3{0.0f, 1.0f, 0.0f};
        t = glm::normalize(glm::cross(axis, n));
    }
    const glm::vec3 b = glm::cross(n, t);

    // Columns (t, b, n): orthonormal, and right handed because
    // cross(t, cross(n, t)) == n for unit orthogonal t and n. The handedness of
    // the ORIGINAL frame is not in here - it rides the separate bit below.
    const glm::quat q = glm::normalize(glm::quat_cast(glm::mat3{t, b, n}));

    const float components[4] = { q.x, q.y, q.z, q.w };
    int largest = 0;
    for (int i = 1; i < 4; ++i) {
        if (std::abs(components[i]) > std::abs(components[largest])) {
            largest = i;
        }
    }
    const float sign   = (components[largest] < 0.0f) ? -1.0f : 1.0f;
    const float scaler = 1.4142135623730951f; // sqrt(2)

    std::array<int16_t, 4> encoded{};
    for (int i = 0; i < 3; ++i) {
        // The clamp keeps float_to_snorm16() inside the interval where it and
        // meshopt_quantizeSnorm() agree bit for bit; only rounding of a value
        // already at the limit can reach it.
        const float value = std::clamp(components[(largest + 1 + i) & 3] * scaler * sign, -1.0f, 1.0f);
        encoded[static_cast<std::size_t>(i)] = erhe::dataformat::float_to_snorm16(value);
    }
    const bool negative_handedness = (tangent_in.w < 0.0f);
    encoded[3] = static_cast<int16_t>(largest | (negative_handedness ? 4 : 0));
    return encoded;
}

auto sort_joint_influences(const glm::vec4& indices, const glm::vec4& weights) -> Joint_influences
{
    struct Influence
    {
        uint8_t index;
        float   weight;
    };
    Influence influences[4] = {
        Influence{static_cast<uint8_t>(indices[0]), weights[0]},
        Influence{static_cast<uint8_t>(indices[1]), weights[1]},
        Influence{static_cast<uint8_t>(indices[2]), weights[2]},
        Influence{static_cast<uint8_t>(indices[3]), weights[3]}
    };

    float total = 0.0f;
    for (const Influence& influence : influences) {
        // A negative or non-finite weight is not a weight. Treat it as absent
        // rather than letting it poison the normalization.
        total += (std::isfinite(influence.weight) && (influence.weight > 0.0f)) ? influence.weight : 0.0f;
    }
    if (!(total > 0.0f)) {
        return Joint_influences{};
    }
    for (Influence& influence : influences) {
        const float weight = (std::isfinite(influence.weight) && (influence.weight > 0.0f)) ? influence.weight : 0.0f;
        influence.weight = weight / total;
    }

    // Descending, so the smallest - the one the implicit-sum encoding drops -
    // ends up last. Stable, so equal weights keep their author order and the
    // result stays deterministic across builds of the same mesh.
    std::stable_sort(
        std::begin(influences),
        std::end(influences),
        [](const Influence& lhs, const Influence& rhs) { return lhs.weight > rhs.weight; }
    );

    Joint_influences result{};
    for (std::size_t i = 0; i < 4; ++i) {
        result.indices[i] = influences[i].index;
        result.weights[static_cast<glm::length_t>(i)] = influences[i].weight;
    }
    return result;
}

auto encode_implicit_sum_joint_weights(const glm::vec4& sorted_weights) -> std::array<uint16_t, 3>
{
    constexpr int32_t one = 65535;

    int32_t quantized[3] = {
        static_cast<int32_t>(sorted_weights[0] * static_cast<float>(one) + 0.5f),
        static_cast<int32_t>(sorted_weights[1] * static_cast<float>(one) + 0.5f),
        static_cast<int32_t>(sorted_weights[2] * static_cast<float>(one) + 0.5f)
    };
    for (int32_t& value : quantized) {
        value = std::clamp(value, int32_t{0}, one);
    }
    // The decoder reconstructs the fourth weight as 1 - (w0 + w1 + w2), so the
    // three stored units must not sum past one unit - independent rounding can
    // push them over. Take the excess off the largest, where it is the smallest
    // relative error; the weights are sorted, so that is the first.
    const int32_t sum = quantized[0] + quantized[1] + quantized[2];
    if (sum > one) {
        quantized[0] = std::max(int32_t{0}, quantized[0] - (sum - one));
    }
    return std::array<uint16_t, 3>{
        static_cast<uint16_t>(quantized[0]),
        static_cast<uint16_t>(quantized[1]),
        static_cast<uint16_t>(quantized[2])
    };
}

auto Mesh_optimize_totals::acmr_before() const -> float
{
    return (triangle_count > 0) ? static_cast<float>(acmr_before_weighted / static_cast<double>(triangle_count)) : 0.0f;
}

auto Mesh_optimize_totals::acmr_after() const -> float
{
    return (triangle_count > 0) ? static_cast<float>(acmr_after_weighted / static_cast<double>(triangle_count)) : 0.0f;
}

auto Mesh_optimize_totals::overdraw_before() const -> float
{
    return (triangle_count > 0) ? static_cast<float>(overdraw_before_weighted / static_cast<double>(triangle_count)) : 0.0f;
}

auto Mesh_optimize_totals::overdraw_after() const -> float
{
    return (triangle_count > 0) ? static_cast<float>(overdraw_after_weighted / static_cast<double>(triangle_count)) : 0.0f;
}

namespace {

// Process-wide, because that is the scope the question is asked at: primitives
// are optimized from loader workers, from the deferred finalize tasks and from
// the main thread, and no one of those owns the answer.
std::mutex           g_totals_mutex;
Mesh_optimize_totals g_totals;

} // anonymous namespace

auto get_mesh_optimize_totals() -> Mesh_optimize_totals
{
    const std::lock_guard<std::mutex> lock{g_totals_mutex};
    return g_totals;
}

void reset_mesh_optimize_totals()
{
    const std::lock_guard<std::mutex> lock{g_totals_mutex};
    g_totals = Mesh_optimize_totals{};
}

void log_mesh_optimize_totals()
{
    const Mesh_optimize_totals totals = get_mesh_optimize_totals();
    if (totals.primitive_count == 0) {
        return;
    }
    log_primitive->info(
        "mesh optimize totals: {} primitives ({} measured, {} replayed from cache), "
        "vertices {} -> {} ({:+.1f}%), {} triangles, ACMR {:.3f} -> {:.3f}, overdraw {:.3f} -> {:.3f}, "
        "fetch {} -> {} bytes ({:+.1f}%), {:.1f} ms total",
        totals.primitive_count,
        totals.measured_count,
        totals.replayed_count,
        totals.vertex_count_before,
        totals.vertex_count_after,
        (totals.vertex_count_before > 0)
            ? 100.0f * (static_cast<float>(totals.vertex_count_after) - static_cast<float>(totals.vertex_count_before)) / static_cast<float>(totals.vertex_count_before)
            : 0.0f,
        totals.triangle_count,
        totals.acmr_before(),
        totals.acmr_after(),
        totals.overdraw_before(),
        totals.overdraw_after(),
        totals.fetch_bytes_before,
        totals.fetch_bytes_after,
        (totals.fetch_bytes_before > 0)
            ? 100.0f * (static_cast<float>(totals.fetch_bytes_after) - static_cast<float>(totals.fetch_bytes_before)) / static_cast<float>(totals.fetch_bytes_before)
            : 0.0f,
        1000.0 * totals.elapsed_seconds
    );
}

void log_mesh_optimize_statistics(const std::string_view name, const Mesh_optimize_statistics& statistics)
{
    {
        const std::lock_guard<std::mutex> lock{g_totals_mutex};
        ++g_totals.primitive_count;
        g_totals.vertex_count_before += statistics.vertex_count_before;
        g_totals.vertex_count_after  += statistics.vertex_count_after;
        if (statistics.measured) {
            // A cache hit contributes its vertex counts (the derivation is the
            // same one) but no ACMR / overdraw / fetch figures - it never ran
            // meshopt_analyze*(). Folding zeroes in would drag the weighted
            // means towards nothing.
            ++g_totals.measured_count;
            g_totals.triangle_count           += statistics.triangle_count;
            g_totals.fetch_bytes_before       += statistics.fetch_bytes_before;
            g_totals.fetch_bytes_after        += statistics.fetch_bytes_after;
            const double triangles = static_cast<double>(statistics.triangle_count);
            g_totals.acmr_before_weighted     += static_cast<double>(statistics.acmr_before)     * triangles;
            g_totals.acmr_after_weighted      += static_cast<double>(statistics.acmr_after)      * triangles;
            g_totals.overdraw_before_weighted += static_cast<double>(statistics.overdraw_before) * triangles;
            g_totals.overdraw_after_weighted  += static_cast<double>(statistics.overdraw_after)  * triangles;
            g_totals.elapsed_seconds          += static_cast<double>(statistics.elapsed_seconds);
        } else {
            ++g_totals.replayed_count;
        }
    }

    if (!statistics.measured) {
        // Cache hit: the derivation was replayed, so there are no before/after
        // figures to report. Saying so beats printing zeroes that read like an
        // optimization that did nothing.
        log_primitive->info(
            "mesh optimize {}: {} triangles, vertices {} -> {} ({:+.1f}%), replayed from cache",
            name,
            statistics.triangle_count,
            statistics.vertex_count_before,
            statistics.vertex_count_after,
            (statistics.vertex_count_before > 0)
                ? 100.0f * (static_cast<float>(statistics.vertex_count_after) - static_cast<float>(statistics.vertex_count_before)) / static_cast<float>(statistics.vertex_count_before)
                : 0.0f
        );
        return;
    }
    log_primitive->info(
        "mesh optimize {}: {} triangles, vertices {} -> {} ({:+.1f}%), ACMR {:.3f} -> {:.3f}, "
        "overdraw {:.3f} -> {:.3f}, fetch {} -> {} bytes, {:.1f} ms",
        name,
        statistics.triangle_count,
        statistics.vertex_count_before,
        statistics.vertex_count_after,
        (statistics.vertex_count_before > 0)
            ? 100.0f * (static_cast<float>(statistics.vertex_count_after) - static_cast<float>(statistics.vertex_count_before)) / static_cast<float>(statistics.vertex_count_before)
            : 0.0f,
        statistics.acmr_before,
        statistics.acmr_after,
        statistics.overdraw_before,
        statistics.overdraw_after,
        statistics.fetch_bytes_before,
        statistics.fetch_bytes_after,
        1000.0f * statistics.elapsed_seconds
    );
}

} // namespace erhe::primitive
