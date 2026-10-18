// Filesystem cache for the mesh optimizer; see
// doc/erhe/meshoptimizer_integration.md, "The filesystem cache".
//
// One cache serves both entry points: optimize_indexed_mesh_cached() is the
// cached form of the multi-stream core, which the geometry path calls with the
// bytes a build staged and optimize_triangle_soup_cached() calls with a soup's
// single stream.

#include "erhe_primitive/mesh_optimizer.hpp"

#include "erhe_primitive/primitive_log.hpp"
#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_file/file.hpp"
#include "erhe_hash/hash.hpp"
#include "erhe_profile/profile.hpp"

#include <meshoptimizer.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <thread>

namespace erhe::primitive {

namespace {

constexpr uint32_t c_magic = 0x434f4d45u; // 'EMOC' little-endian

// Bump whenever the meaning of a stored entry changes - a different pass order,
// a different remap convention, a meshoptimizer upgrade that alters output.
// Old entries then miss and are rewritten rather than being replayed wrongly.
// 2: position quantization moved from the content formats into the optimized
//    variant only (base float3, optimized snorm16).
// 3: the key covers the streams of optimize_indexed_mesh() (one per vertex
//    stream) instead of a triangle soup, so the geometry path is cached too.
constexpr uint32_t c_format_version = 3;

class Header
{
public:
    uint32_t magic           {0};
    uint32_t format_version  {0};
    uint64_t settings_hash   {0};
    uint64_t source_hash     {0};
    uint32_t vertex_count_in {0};
    uint32_t vertex_count_out{0};
    uint32_t triangle_count  {0};
    uint32_t stream_count    {0};
};

// Everything that changes the OUTPUT for a given input, so an entry written
// under different settings misses instead of being replayed.
[[nodiscard]] auto hash_settings(const Mesh_optimize_options& options) -> uint64_t
{
    uint64_t h = erhe::hash::hash(&c_format_version, sizeof(c_format_version));
    // A meshoptimizer upgrade can change what the passes produce; entries
    // written by another version miss instead of replaying its derivation.
    const uint32_t meshoptimizer_version = MESHOPTIMIZER_VERSION;
    h = erhe::hash::hash(&meshoptimizer_version, sizeof(meshoptimizer_version), h);
    const uint8_t flags = static_cast<uint8_t>(
        (options.weld         ? 1u : 0u) |
        (options.vertex_cache ? 2u : 0u) |
        (options.overdraw     ? 4u : 0u) |
        (options.vertex_fetch ? 8u : 0u)
    );
    h = erhe::hash::hash(&flags, sizeof(flags), h);
    h = erhe::hash::hash(&options.overdraw_threshold, sizeof(options.overdraw_threshold), h);
    return h;
}

// The source content. The vertex format is included because it decides the
// strides the weld compares over and where the overdraw pass reads positions:
// identical bytes under a different layout are a different mesh as far as the
// optimizer is concerned.
[[nodiscard]] auto hash_source(
    const std::vector<Mesh_optimize_stream>& streams,
    const std::vector<uint32_t>&             indices,
    const erhe::dataformat::Vertex_format&   vertex_format
) -> uint64_t
{
    const uint64_t format_hash  = vertex_format.get_hash();
    const uint64_t stream_count = streams.size();
    uint64_t h = erhe::hash::hash(&format_hash, sizeof(format_hash));
    h = erhe::hash::hash(&stream_count, sizeof(stream_count), h);
    for (const Mesh_optimize_stream& stream : streams) {
        const uint64_t stride = stream.stride;
        h = erhe::hash::hash(&stride, sizeof(stride), h);
        h = erhe::hash::hash(stream.data.data(), stream.data.size(), h);
    }
    h = erhe::hash::hash(indices.data(), indices.size() * sizeof(uint32_t), h);
    return h;
}

[[nodiscard]] auto entry_path(const std::filesystem::path& directory, const uint64_t key) -> std::filesystem::path
{
    char name[32];
    std::snprintf(name, sizeof(name), "%016llx.emoc", static_cast<unsigned long long>(key));
    return directory / name;
}

// The vertex count the streams agree on, or 0 when they do not describe one
// mesh in `vertex_format`'s layout. Such input bypasses the cache and goes
// straight to optimize_indexed_mesh(), which owns the rules and refuses it.
[[nodiscard]] auto get_cacheable_vertex_count(
    const std::vector<Mesh_optimize_stream>& streams,
    const erhe::dataformat::Vertex_format&   vertex_format
) -> std::size_t
{
    if (streams.empty() || (streams.size() != vertex_format.streams.size())) {
        return 0;
    }
    std::size_t vertex_count = 0;
    for (std::size_t stream_index = 0, end = streams.size(); stream_index < end; ++stream_index) {
        const Mesh_optimize_stream& stream = streams[stream_index];
        if ((stream.stride == 0) || (stream.stride != vertex_format.streams[stream_index].stride) || ((stream.data.size() % stream.stride) != 0)) {
            return 0;
        }
        const std::size_t stream_vertex_count = stream.data.size() / stream.stride;
        if (stream_index == 0) {
            vertex_count = stream_vertex_count;
        } else if (stream_vertex_count != vertex_count) {
            return 0;
        }
    }
    return vertex_count;
}

// Rewrites `streams` and `indices` into the optimized mesh from the two stored
// remaps. This is the whole reason the entry can be small: the remaps determine
// the output, so a hit does no meshoptimizer work.
//
//   streams: every source vertex is written to the output slot the forward
//            remap names. Weld-merged sources land on the same slot with equal
//            bytes by construction (the weld compares every stride byte of
//            every stream), so the write order between them does not matter.
//   indices: output triangle t is source triangle permutation[t], its corners
//            mapped through the forward remap. The reordering passes move
//            triangles without rotating them, which is what makes this the
//            exact output (see Triangle_key in mesh_optimizer.cpp).
//
// Returns false, touching nothing, when the tables do not describe this input
// (an index out of range, or a referenced vertex the remap drops).
[[nodiscard]] auto replay(
    std::vector<Mesh_optimize_stream>& streams,
    std::vector<uint32_t>&             indices,
    const std::vector<uint32_t>&       triangle_permutation,
    const std::vector<uint32_t>&       vertex_remap,
    const uint32_t                     vertex_count_out
) -> bool
{
    const std::size_t vertex_count_in = vertex_remap.size();
    const std::size_t triangle_count  = triangle_permutation.size();

    std::vector<uint32_t> output_indices(triangle_count * 3);
    for (std::size_t triangle = 0; triangle < triangle_count; ++triangle) {
        const std::size_t source_triangle = triangle_permutation[triangle];
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const uint32_t source_vertex = indices[source_triangle * 3 + corner];
            if (source_vertex >= vertex_count_in) {
                return false;
            }
            const uint32_t output_vertex = vertex_remap[source_vertex];
            if (output_vertex == Mesh_optimize_result::no_vertex) {
                return false;
            }
            output_indices[triangle * 3 + corner] = output_vertex;
        }
    }

    for (Mesh_optimize_stream& stream : streams) {
        const std::size_t    stride = stream.stride;
        std::vector<uint8_t> output(static_cast<std::size_t>(vertex_count_out) * stride, uint8_t{0});
        for (std::size_t vertex = 0; vertex < vertex_count_in; ++vertex) {
            const uint32_t target = vertex_remap[vertex];
            if (target == Mesh_optimize_result::no_vertex) {
                continue;
            }
            std::memcpy(output.data() + static_cast<std::size_t>(target) * stride, stream.data.data() + vertex * stride, stride);
        }
        stream.data = std::move(output);
    }
    indices = std::move(output_indices);
    return true;
}

// Fills `result` and rewrites the input on a clean hit. Every failure path is a
// miss that leaves the input untouched - the caller optimizes.
[[nodiscard]] auto try_load(
    const std::filesystem::path&       path,
    std::vector<Mesh_optimize_stream>& streams,
    std::vector<uint32_t>&             indices,
    const std::size_t                  vertex_count_in,
    const uint64_t                     settings_hash,
    const uint64_t                     source_hash,
    Mesh_optimize_result&              result
) -> bool
{
    std::ifstream file{path, std::ios::binary};
    if (!file.is_open()) {
        return false;
    }

    Header header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file || (header.magic != c_magic) || (header.format_version != c_format_version)) {
        return false;
    }
    if ((header.settings_hash != settings_hash) || (header.source_hash != source_hash)) {
        // A stale entry under a reused name. (The file name is derived from
        // these same two hashes, so this does not detect a hash collision;
        // one with matching counts would replay a wrong derivation. At
        // 2 x 64 bits that is accepted, not guarded.)
        return false;
    }
    // The header must agree with the mesh in front of us before its counts are
    // used to size anything.
    if (
        (header.vertex_count_in  != vertex_count_in)     ||
        (header.triangle_count   != (indices.size() / 3)) ||
        (header.stream_count     != streams.size())      ||
        (header.vertex_count_out >  header.vertex_count_in)
    ) {
        return false;
    }

    std::vector<uint32_t> triangle_permutation(header.triangle_count);
    std::vector<uint32_t> vertex_remap(header.vertex_count_in);
    file.read(reinterpret_cast<char*>(triangle_permutation.data()), static_cast<std::streamsize>(triangle_permutation.size() * sizeof(uint32_t)));
    file.read(reinterpret_cast<char*>(vertex_remap.data()),         static_cast<std::streamsize>(vertex_remap.size()         * sizeof(uint32_t)));
    if (!file) {
        return false; // truncated
    }

    // Validate before indexing with any of it: a corrupt table would otherwise
    // read or write out of bounds during replay.
    for (const uint32_t source_triangle : triangle_permutation) {
        if (source_triangle >= header.triangle_count) {
            return false;
        }
    }
    for (const uint32_t target : vertex_remap) {
        if ((target != Mesh_optimize_result::no_vertex) && (target >= header.vertex_count_out)) {
            return false;
        }
    }

    if (!replay(streams, indices, triangle_permutation, vertex_remap, header.vertex_count_out)) {
        return false;
    }

    result.triangle_permutation           = std::move(triangle_permutation);
    result.vertex_remap                   = std::move(vertex_remap);
    result.statistics.vertex_count_before = header.vertex_count_in;
    result.statistics.vertex_count_after  = header.vertex_count_out;
    result.statistics.triangle_count      = header.triangle_count;
    // The analyze* measurements are deliberately NOT stored or recomputed: they
    // cost a pass of their own and exist for the log line, not for correctness.
    // A cache hit therefore reports measured == false, the documented "not
    // measured" state rather than a false "unchanged".
    result.statistics.measured = false;
    return true;
}

void try_store(
    const std::filesystem::path& directory,
    const std::filesystem::path& path,
    const Mesh_optimize_result&  result,
    const std::size_t            stream_count,
    const uint64_t               settings_hash,
    const uint64_t               source_hash
)
{
    if (!erhe::file::ensure_directory_exists(directory)) {
        return;
    }

    Header header{};
    header.magic            = c_magic;
    header.format_version   = c_format_version;
    header.settings_hash    = settings_hash;
    header.source_hash      = source_hash;
    header.vertex_count_in  = static_cast<uint32_t>(result.vertex_remap.size());
    header.vertex_count_out = static_cast<uint32_t>(result.statistics.vertex_count_after);
    header.triangle_count   = static_cast<uint32_t>(result.triangle_permutation.size());
    header.stream_count     = static_cast<uint32_t>(stream_count);

    // Write aside and rename: two loader workers may be storing the same entry
    // at once (content addressing makes identical meshes of one asset share
    // an entry, and they are optimized concurrently), and a reader must never
    // see a partial file. The temporary name is unique per writing thread: with
    // one shared name the two writers open the same file, and on Windows the
    // rename of a file another writer still holds open fails, so both writers
    // gave up and the entry was never stored.
    char temp_suffix[48];
    std::snprintf(
        temp_suffix,
        sizeof(temp_suffix),
        ".%016llx.tmp",
        static_cast<unsigned long long>(std::hash<std::thread::id>{}(std::this_thread::get_id()))
    );
    std::filesystem::path temp_path = path;
    temp_path += std::filesystem::path{temp_suffix};
    {
        std::ofstream file{temp_path, std::ios::binary | std::ios::trunc};
        if (!file.is_open()) {
            log_primitive->warn("mesh optimize cache: cannot create '{}'", temp_path.string());
            return;
        }
        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        file.write(reinterpret_cast<const char*>(result.triangle_permutation.data()), static_cast<std::streamsize>(result.triangle_permutation.size() * sizeof(uint32_t)));
        file.write(reinterpret_cast<const char*>(result.vertex_remap.data()),         static_cast<std::streamsize>(result.vertex_remap.size()         * sizeof(uint32_t)));
        if (!file) {
            file.close();
            std::error_code discard;
            std::filesystem::remove(temp_path, discard);
            return;
        }
    }

    std::error_code error_code;
    std::filesystem::rename(temp_path, path, error_code);
    if (error_code) {
        // Losing the race to another writer is fine - the entry is there either
        // way. Remove our temporary so it does not accumulate.
        std::error_code discard;
        std::filesystem::remove(temp_path, discard);
    }
}

} // anonymous namespace

auto optimize_indexed_mesh_cached(
    std::vector<Mesh_optimize_stream>&     streams,
    std::vector<uint32_t>&                 indices,
    const erhe::dataformat::Vertex_format& vertex_format,
    const Mesh_optimize_options&           options,
    const std::filesystem::path&           cache_directory,
    Mesh_optimize_result&                  result
) -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (cache_directory.empty()) {
        return optimize_indexed_mesh(streams, indices, vertex_format, options, result);
    }
    const std::size_t vertex_count_in = get_cacheable_vertex_count(streams, vertex_format);
    if ((vertex_count_in == 0) || indices.empty() || ((indices.size() % 3) != 0)) {
        return optimize_indexed_mesh(streams, indices, vertex_format, options, result);
    }

    const std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();

    const uint64_t              settings_hash = hash_settings(options);
    const uint64_t              source_hash   = hash_source(streams, indices, vertex_format);
    const std::filesystem::path path          = entry_path(cache_directory, source_hash ^ settings_hash);

    if (try_load(path, streams, indices, vertex_count_in, settings_hash, source_hash, result)) {
        result.statistics.elapsed_seconds = std::chrono::duration<float>{std::chrono::steady_clock::now() - start_time}.count();
        log_primitive->trace("mesh optimize cache hit: {}", erhe::file::to_string(path));
        return true;
    }

    const std::size_t stream_count = streams.size();
    if (!optimize_indexed_mesh(streams, indices, vertex_format, options, result)) {
        return false;
    }
    try_store(cache_directory, path, result, stream_count, settings_hash, source_hash);
    return true;
}

auto optimize_triangle_soup_cached(
    const Triangle_soup&         source,
    const Mesh_optimize_options& options,
    const std::filesystem::path& cache_directory
) -> Mesh_optimize_result
{
    ERHE_PROFILE_FUNCTION();

    // Same rejections optimize_triangle_soup() makes: a soup has exactly one
    // stream, bound at 0, and only triangles are optimized.
    const erhe::dataformat::Vertex_stream* stream = source.vertex_format.get_stream(0);
    if ((source.vertex_format.streams.size() != 1) || (stream == nullptr) || (source.primitive_type != Primitive_type::triangles)) {
        return Mesh_optimize_result{};
    }

    Mesh_optimize_result              result{};
    std::vector<Mesh_optimize_stream> streams;
    streams.push_back(Mesh_optimize_stream{.data = source.vertex_data, .stride = stream->stride});
    std::vector<uint32_t> indices = source.index_data;
    if (!optimize_indexed_mesh_cached(streams, indices, source.vertex_format, options, cache_directory, result)) {
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

} // namespace erhe::primitive
