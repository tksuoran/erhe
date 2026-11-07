#include "erhe_graphics/spirv_cache.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_file/file.hpp"
#include "erhe_hash/hash.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

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

auto Spirv_cache::get_key(const std::string& source, Shader_type stage, const uint64_t compile_settings_hash) const -> std::string
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
    const std::string hash = get_key(source, stage, compile_settings_hash);
    std::vector<unsigned int> spirv = get_module(hash);
    if (!spirv.empty()) {
        log_program->debug("SPIR-V cache hit: {} {}", shader_type_string(stage), hash);
    }
    return spirv;
}

auto Spirv_cache::get_module(const std::string& hash) const -> std::vector<unsigned int>
{
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

    return spirv;
}

void Spirv_cache::put(const std::string& source, Shader_type stage, const uint64_t compile_settings_hash, const std::vector<unsigned int>& spirv)
{
    if (spirv.empty()) {
        return;
    }

    const std::string           hash           = get_key(source, stage, compile_settings_hash);
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

auto Spirv_cache::record_path(const std::string& key) const -> std::filesystem::path
{
    return m_cache_directory / (key + ".record.json");
}

auto Spirv_cache::stats_path(const std::string& key) const -> std::filesystem::path
{
    return m_cache_directory / (key + ".stats.json");
}

auto Spirv_cache::get_module_stats(const std::string& key) const -> std::optional<Spirv_cache_module_stats>
{
    std::error_code ec;
    if (!std::filesystem::is_regular_file(stats_path(key), ec)) {
        return std::nullopt;
    }
    const std::optional<std::string> text = erhe::file::read("Spirv_cache module stats", stats_path(key));
    if (!text.has_value()) {
        return std::nullopt;
    }
    const nlohmann::json json = nlohmann::json::parse(text.value(), nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        return std::nullopt;
    }
    return Spirv_cache_module_stats{
        .spirv_tools_version      = json.value("spirv_tools_version",      std::string{}),
        .unoptimized_bytes        = json.value("unoptimized_bytes",        std::size_t{0}),
        .unoptimized_instructions = json.value("unoptimized_instructions", std::size_t{0}),
        .optimized_bytes          = json.value("optimized_bytes",          std::size_t{0}),
        .optimized_instructions   = json.value("optimized_instructions",   std::size_t{0})
    };
}

void Spirv_cache::put_module_stats(const std::string& key, const Spirv_cache_module_stats& stats)
{
    const nlohmann::json json{
        {"spirv_tools_version",      stats.spirv_tools_version},
        {"unoptimized_bytes",        stats.unoptimized_bytes},
        {"unoptimized_instructions", stats.unoptimized_instructions},
        {"optimized_bytes",          stats.optimized_bytes},
        {"optimized_instructions",   stats.optimized_instructions}
    };
    if (!erhe::file::write_file_via_temp(stats_path(key), json.dump(1))) {
        log_program->warn("Failed to store SPIR-V cache module stats {}", key);
    }
}

// Texts by content (FNV-1a 64): the top-level files repeat across every variant of a program.
auto Spirv_cache::put_blob(const std::string& text) const -> std::string
{
    const std::string id = fmt::format("{:016x}", erhe::hash::hash(text.data(), text.size()));
    const std::filesystem::path path = m_cache_directory / "blobs" / (id + ".glsl");
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (!erhe::file::write_file_via_temp(path, text)) {
            return {};
        }
    }
    return id;
}

auto Spirv_cache::get_blob(const std::string& id) const -> std::optional<std::string>
{
    const std::filesystem::path path = m_cache_directory / "blobs" / (id + ".glsl");
    std::error_code ec;
    if (id.empty() || !std::filesystem::is_regular_file(path, ec)) {
        return std::nullopt;
    }
    return erhe::file::read("Spirv_cache record blob", path);
}

auto Spirv_cache::has_record(const std::string& key) const -> bool
{
    std::error_code ec;
    return std::filesystem::is_regular_file(record_path(key), ec);
}

auto Spirv_cache::get_record(const std::string& key) const -> std::optional<Spirv_cache_record>
{
    if (!has_record(key)) {
        return std::nullopt;
    }
    const std::optional<std::string> text = erhe::file::read("Spirv_cache record", record_path(key));
    if (!text.has_value()) {
        return std::nullopt;
    }
    const nlohmann::json json = nlohmann::json::parse(text.value(), nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        log_program->warn("SPIR-V cache record {} is not valid JSON", key);
        return std::nullopt;
    }
    const std::optional<std::string> main_source = get_blob(json.value("main_source", std::string{}));
    const std::optional<std::string> preamble    = get_blob(json.value("preamble", std::string{}));
    if (!main_source.has_value() || !preamble.has_value()) {
        log_program->warn("SPIR-V cache record {} names a missing source blob", key);
        return std::nullopt;
    }
    Spirv_cache_record record{
        .main_name         = json.value("main_name", std::string{}),
        .main_source       = main_source.value(),
        .preamble          = preamble.value(),
        .include_paths     = {},
        .unoptimized_spirv = {}
    };
    for (const nlohmann::json& include : json.value("include_paths", nlohmann::json::array())) {
        if (include.is_string()) {
            record.include_paths.push_back(include.get<std::string>());
        }
    }
    if (json.value("unoptimized", false)) {
        record.unoptimized_spirv = get_module(key + ".unoptimized");
        if (record.unoptimized_spirv.empty()) {
            log_program->warn("SPIR-V cache record {} names a missing unoptimized module", key);
            return std::nullopt;
        }
    }
    return record;
}

void Spirv_cache::put_record(const std::string& key, const Spirv_cache_record& record)
{
    const std::string main_source_id = put_blob(record.main_source);
    const std::string preamble_id    = put_blob(record.preamble);
    if (main_source_id.empty() || preamble_id.empty()) {
        log_program->warn("Failed to store the source blobs of SPIR-V cache record {}", key);
        return;
    }
    const bool unoptimized = !record.unoptimized_spirv.empty();
    if (unoptimized) {
        const std::string_view bytes{
            reinterpret_cast<const char*>(record.unoptimized_spirv.data()),
            record.unoptimized_spirv.size() * sizeof(unsigned int)
        };
        if (!erhe::file::write_file_via_temp(cache_path(key + ".unoptimized"), bytes)) {
            return;
        }
    }
    // Written last: a record names only files that exist.
    const nlohmann::json json{
        {"main_name",     record.main_name},
        {"main_source",   main_source_id},
        {"preamble",      preamble_id},
        {"include_paths", record.include_paths},
        {"unoptimized",   unoptimized}
    };
    if (!erhe::file::write_file_via_temp(record_path(key), json.dump(1))) {
        log_program->warn("Failed to store SPIR-V cache record {}", key);
    }
}

} // namespace erhe::graphics
