#include "erhe_graphics/spirv_cache.hpp"
#include "erhe_graphics/graphics_log.hpp"

#include <glslang/build_info.h>

#include <fmt/format.h>

#include <fstream>
#include <functional>
#include <sstream>
#include <iomanip>

namespace erhe::graphics {

namespace {

auto shader_type_string(Shader_type type) -> const char*
{
    switch (type) {
        case Shader_type::vertex_shader:   return "vertex";
        case Shader_type::fragment_shader: return "fragment";
        case Shader_type::geometry_shader: return "geometry";
        case Shader_type::compute_shader:  return "compute";
        default:                           return "unknown";
    }
}

// Compilation settings salt. Embeds the linked glslang version
// (GLSLANG_VERSION_*) so cache entries are auto-invalidated when the
// glslang dependency is bumped - the same source can compile to
// different SPIR-V across glslang versions and we never want to
// load a stale binary built by an earlier compiler.
//
// Bump the trailing "vN" tag whenever SpvOptions, the includer behaviour
// or the target environment changes (since those affect SPIR-V output
// without changing the glslang version number).
auto make_settings_salt() -> std::string
{
    return fmt::format(
        "vulkan_1_1:spv_1_6:debug:noopt:validate:includer:glslang{}.{}.{}{}:v7",
        GLSLANG_VERSION_MAJOR,
        GLSLANG_VERSION_MINOR,
        GLSLANG_VERSION_PATCH,
        GLSLANG_VERSION_FLAVOR
    );
}
static const std::string c_settings_salt = make_settings_salt();

} // anonymous namespace

Spirv_cache::Spirv_cache(const std::filesystem::path& cache_directory)
    : m_cache_directory{cache_directory}
{
    std::error_code ec;
    std::filesystem::create_directories(m_cache_directory, ec);
    if (ec) {
        log_program->warn("Failed to create SPIR-V cache directory '{}': {}", m_cache_directory.string(), ec.message());
    }
}

auto Spirv_cache::compute_hash(const std::string& source, Shader_type stage) const -> std::string
{
    // Combine source + stage + settings into a single string and hash it
    std::string key;
    key.reserve(source.size() + 128);
    key.append(shader_type_string(stage));
    key.push_back(':');
    key.append(c_settings_salt);
    key.push_back(':');
    key.append(source);

    // Use std::hash for speed - collision risk is acceptable for a cache
    std::size_t hash_value = std::hash<std::string>{}(key);

    // Convert to hex string
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << hash_value;
    return oss.str();
}

auto Spirv_cache::cache_path(const std::string& hash) const -> std::filesystem::path
{
    return m_cache_directory / (hash + ".spv");
}

auto Spirv_cache::get(const std::string& source, Shader_type stage) const -> std::vector<unsigned int>
{
    const std::string hash = compute_hash(source, stage);
    const std::filesystem::path path = cache_path(hash);

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return {};
    }

    const std::uintmax_t file_size = std::filesystem::file_size(path, ec);
    if (ec || (file_size == 0) || ((file_size % sizeof(unsigned int)) != 0)) {
        return {};
    }

    std::ifstream file{path, std::ios::binary};
    if (!file.is_open()) {
        return {};
    }

    const std::size_t word_count = static_cast<std::size_t>(file_size) / sizeof(unsigned int);
    std::vector<unsigned int> spirv(word_count);
    file.read(reinterpret_cast<char*>(spirv.data()), static_cast<std::streamsize>(file_size));
    if (!file.good()) {
        return {};
    }

    // Basic SPIR-V magic number validation
    if ((spirv.size() >= 1) && (spirv[0] != 0x07230203)) {
        return {};
    }

    log_program->debug("SPIR-V cache hit: {} {}", shader_type_string(stage), hash);
    return spirv;
}

void Spirv_cache::put(const std::string& source, Shader_type stage, const std::vector<unsigned int>& spirv)
{
    if (spirv.empty()) {
        return;
    }

    const std::string hash = compute_hash(source, stage);
    const std::filesystem::path path = cache_path(hash);

    std::ofstream file{path, std::ios::binary};
    if (!file.is_open()) {
        log_program->warn("Failed to write SPIR-V cache file: {}", path.string());
        return;
    }

    file.write(
        reinterpret_cast<const char*>(spirv.data()),
        static_cast<std::streamsize>(spirv.size() * sizeof(unsigned int))
    );

    log_program->info("SPIR-V cache store: {} {}", shader_type_string(stage), hash);
}

} // namespace erhe::graphics
