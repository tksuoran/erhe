// Filesystem cache for optimize_triangle_soup(); see
// doc/erhe/meshoptimizer_integration.md, "The filesystem cache (soup path only)".
//
// Soup path only. The geometry path runs its passes uncached at
// build/finalize time - a deliberate cost decision recorded in that doc - so
// there is no cache lookup there to keep in sync with this one.

#include "erhe_primitive/mesh_optimizer.hpp"

#include "erhe_primitive/primitive_log.hpp"
#include "erhe_primitive/triangle_soup.hpp"
#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_file/file.hpp"
#include "erhe_hash/hash.hpp"
#include "erhe_profile/profile.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace erhe::primitive {

namespace {

constexpr uint32_t c_magic = 0x434f4d45u; // 'EMOC' little-endian

// Bump whenever the meaning of a stored entry changes - a different pass order,
// a different remap convention, a meshoptimizer upgrade that alters output.
// Old entries then miss and are rewritten rather than being replayed wrongly.
// 2: position quantization moved from the content formats into the optimized
//    variant only (base float3, optimized snorm16).
constexpr uint32_t c_format_version = 2;

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
    uint32_t reserved        {0}; // keeps the header 8-byte aligned
};

// Everything that changes the OUTPUT for a given input, so an entry written
// under different settings misses instead of being replayed.
[[nodiscard]] auto hash_settings(const Mesh_optimize_options& options) -> uint64_t
{
    uint64_t h = erhe::hash::hash(&c_format_version, sizeof(c_format_version));
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
// stride the weld compares over: identical bytes under a different layout are
// a different mesh as far as the optimizer is concerned.
[[nodiscard]] auto hash_source(const Triangle_soup& source) -> uint64_t
{
    uint64_t h = erhe::hash::hash(source.vertex_data.data(), source.vertex_data.size());
    h = erhe::hash::hash(source.index_data.data(), source.index_data.size() * sizeof(uint32_t), h);
    const uint64_t format_hash    = source.vertex_format.get_hash();
    const uint32_t primitive_type = static_cast<uint32_t>(source.primitive_type);
    h = erhe::hash::hash(&format_hash,    sizeof(format_hash),    h);
    h = erhe::hash::hash(&primitive_type, sizeof(primitive_type), h);
    return h;
}

[[nodiscard]] auto entry_path(const std::filesystem::path& directory, const uint64_t key) -> std::filesystem::path
{
    char name[32];
    std::snprintf(name, sizeof(name), "%016llx.emoc", static_cast<unsigned long long>(key));
    return directory / name;
}

// Rebuilds the optimized soup from the source plus the two remaps. This is the
// whole reason the entry can be small: the remaps determine the output, so a
// hit does no meshoptimizer work.
[[nodiscard]] auto replay(
    const Triangle_soup&         source,
    std::vector<uint32_t>&&      triangle_permutation,
    std::vector<uint32_t>&&      vertex_remap,
    const uint32_t               vertex_count_out
) -> Mesh_optimize_result
{
    Mesh_optimize_result result;
    const std::size_t stride         = source.vertex_format.streams.front().stride;
    const std::size_t triangle_count = triangle_permutation.size();

    std::shared_ptr<Triangle_soup> out = std::make_shared<Triangle_soup>();
    out->vertex_format  = source.vertex_format;
    out->primitive_type = source.primitive_type;
    out->vertex_data.assign(static_cast<std::size_t>(vertex_count_out) * stride, uint8_t{0});
    out->index_data.resize(triangle_count * 3);

    // Gather: the forward remap says where each source vertex went, so writing
    // each source vertex to its output slot fills every slot that survived.
    // Weld-merged sources land on the same slot with equal bytes by
    // construction, so the write order between them does not matter.
    for (std::size_t vertex = 0, end = source.get_vertex_count(); vertex < end; ++vertex) {
        const uint32_t target = vertex_remap[vertex];
        if (target == Mesh_optimize_result::no_vertex) {
            continue;
        }
        std::memcpy(
            out->vertex_data.data() + static_cast<std::size_t>(target) * stride,
            source.vertex_data.data() + vertex * stride,
            stride
        );
    }

    for (std::size_t triangle = 0; triangle < triangle_count; ++triangle) {
        const std::size_t source_triangle = triangle_permutation[triangle];
        for (std::size_t corner = 0; corner < 3; ++corner) {
            out->index_data[triangle * 3 + corner] =
                vertex_remap[source.index_data[source_triangle * 3 + corner]];
        }
    }

    result.triangle_soup        = std::move(out);
    result.triangle_permutation = std::move(triangle_permutation);
    result.vertex_remap         = std::move(vertex_remap);
    result.statistics.vertex_count_before = source.get_vertex_count();
    result.statistics.vertex_count_after  = vertex_count_out;
    result.statistics.triangle_count      = triangle_count;
    // The analyze* measurements are deliberately NOT stored or recomputed: they
    // cost a pass of their own and exist for the log line, not for correctness.
    // A cache hit therefore reports zeroed before/after figures, which is the
    // documented "not measured" state rather than a false "unchanged".
    return result;
}

// Returns a replayed result on a clean hit, an empty result otherwise. Every
// failure path is a miss - the caller re-optimizes.
[[nodiscard]] auto try_load(
    const std::filesystem::path& path,
    const Triangle_soup&         source,
    const uint64_t               settings_hash,
    const uint64_t               source_hash
) -> Mesh_optimize_result
{
    std::ifstream file{path, std::ios::binary};
    if (!file.is_open()) {
        return {};
    }

    Header header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file || (header.magic != c_magic) || (header.format_version != c_format_version)) {
        return {};
    }
    if ((header.settings_hash != settings_hash) || (header.source_hash != source_hash)) {
        // A key collision, or a stale entry under a reused name. Either way the
        // stored derivation does not describe this soup.
        return {};
    }
    // The header must agree with the soup in front of us before its counts are
    // used to size anything.
    if (
        (header.vertex_count_in != source.get_vertex_count()) ||
        (header.triangle_count  != source.index_data.size() / 3) ||
        (header.vertex_count_out > header.vertex_count_in)
    ) {
        return {};
    }

    std::vector<uint32_t> triangle_permutation(header.triangle_count);
    std::vector<uint32_t> vertex_remap(header.vertex_count_in);
    file.read(reinterpret_cast<char*>(triangle_permutation.data()), static_cast<std::streamsize>(triangle_permutation.size() * sizeof(uint32_t)));
    file.read(reinterpret_cast<char*>(vertex_remap.data()),         static_cast<std::streamsize>(vertex_remap.size()         * sizeof(uint32_t)));
    if (!file) {
        return {}; // truncated
    }

    // Validate before indexing with any of it: a corrupt table would otherwise
    // read out of bounds during replay.
    for (const uint32_t source_triangle : triangle_permutation) {
        if (source_triangle >= header.triangle_count) {
            return {};
        }
    }
    for (const uint32_t target : vertex_remap) {
        if ((target != Mesh_optimize_result::no_vertex) && (target >= header.vertex_count_out)) {
            return {};
        }
    }

    return replay(source, std::move(triangle_permutation), std::move(vertex_remap), header.vertex_count_out);
}

void try_store(
    const std::filesystem::path& directory,
    const std::filesystem::path& path,
    const Mesh_optimize_result&  result,
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
    header.vertex_count_out = static_cast<uint32_t>(result.triangle_soup->get_vertex_count());
    header.triangle_count   = static_cast<uint32_t>(result.triangle_permutation.size());

    // Write aside and rename: two loader workers may be storing the same entry
    // at once, and a reader must never see a partial file. The temporary name
    // carries the source hash so concurrent writers do not collide either.
    std::filesystem::path temp_path = path;
    temp_path += std::filesystem::path{".tmp"};
    {
        std::ofstream file{temp_path, std::ios::binary | std::ios::trunc};
        if (!file.is_open()) {
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

auto optimize_triangle_soup_cached(
    const Triangle_soup&         source,
    const Mesh_optimize_options& options,
    const std::filesystem::path& cache_directory
) -> Mesh_optimize_result
{
    ERHE_PROFILE_FUNCTION();

    if (cache_directory.empty()) {
        return optimize_triangle_soup(source, options);
    }
    // Same rejections optimize_triangle_soup() makes, checked before any
    // hashing: hashing a soup it would refuse is wasted work, and the replay
    // path assumes a single stream.
    if (source.vertex_format.streams.size() != 1) {
        return optimize_triangle_soup(source, options);
    }

    const uint64_t              settings_hash = hash_settings(options);
    const uint64_t              source_hash   = hash_source(source);
    const std::filesystem::path path          = entry_path(cache_directory, source_hash ^ settings_hash);

    Mesh_optimize_result cached = try_load(path, source, settings_hash, source_hash);
    if (cached.triangle_soup) {
        log_primitive->trace("mesh optimize cache hit: {}", erhe::file::to_string(path));
        return cached;
    }

    Mesh_optimize_result result = optimize_triangle_soup(source, options);
    if (result.triangle_soup) {
        try_store(cache_directory, path, result, settings_hash, source_hash);
    }
    return result;
}

} // namespace erhe::primitive
