#include "erhe_graphics/spirv_cache.hpp"
#include "erhe_graphics/graphics_log.hpp"

#include <fstream>
#include <functional>
#include <sstream>
#include <iomanip>

#if defined(_WIN32)
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   ifndef NOMINMAX
#       define NOMINMAX
#   endif
#   include <windows.h>
#else
#   include <unistd.h>
#endif

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

// Process-unique suffix for the temporary file put() writes before the
// atomic rename, so two processes storing the same entry never share a
// temporary file.
auto process_suffix() -> std::string
{
#if defined(_WIN32)
    return std::to_string(static_cast<unsigned long>(GetCurrentProcessId()));
#else
    return std::to_string(static_cast<long>(getpid()));
#endif
}

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

auto Spirv_cache::compute_hash(const std::string& source, Shader_type stage, const uint64_t compile_settings_hash) const -> std::string
{
    // Combine stage + compile settings + source into a single string and hash it
    std::string key;
    key.reserve(source.size() + 64);
    key.append(shader_type_string(stage));
    key.push_back(':');
    {
        std::ostringstream settings;
        settings << std::hex << std::setfill('0') << std::setw(16) << compile_settings_hash;
        key.append(settings.str());
    }
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

auto Spirv_cache::get(const std::string& source, Shader_type stage, const uint64_t compile_settings_hash) const -> std::vector<unsigned int>
{
    const std::string hash = compute_hash(source, stage, compile_settings_hash);
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

void Spirv_cache::put(const std::string& source, Shader_type stage, const uint64_t compile_settings_hash, const std::vector<unsigned int>& spirv)
{
    if (spirv.empty()) {
        return;
    }

    const std::string           hash           = compute_hash(source, stage, compile_settings_hash);
    const std::filesystem::path path           = cache_path(hash);
    const std::filesystem::path temporary_path = m_cache_directory / (hash + ".spv.tmp." + process_suffix());

    {
        std::ofstream file{temporary_path, std::ios::binary | std::ios::trunc};
        if (!file.is_open()) {
            log_program->warn("Failed to write SPIR-V cache file: {}", temporary_path.string());
            return;
        }
        file.write(
            reinterpret_cast<const char*>(spirv.data()),
            static_cast<std::streamsize>(spirv.size() * sizeof(unsigned int))
        );
        if (!file.good()) {
            log_program->warn("Failed to write SPIR-V cache file: {}", temporary_path.string());
            file.close();
            std::error_code remove_ec;
            std::filesystem::remove(temporary_path, remove_ec);
            return;
        }
    }

    // rename() replaces an existing file atomically on POSIX and on NTFS,
    // so a reader never sees a partially written entry.
    std::error_code ec;
    std::filesystem::rename(temporary_path, path, ec);
    if (ec) {
        log_program->warn("Failed to store SPIR-V cache file {}: {}", path.string(), ec.message());
        std::error_code remove_ec;
        std::filesystem::remove(temporary_path, remove_ec);
        return;
    }

    log_program->info("SPIR-V cache store: {} {}", shader_type_string(stage), hash);
}

} // namespace erhe::graphics
