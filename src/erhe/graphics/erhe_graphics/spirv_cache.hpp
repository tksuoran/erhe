#pragma once

#include "erhe_graphics/enums.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace erhe::graphics {

// What the shader variant analysis needs of a cached stage beyond its module
// (doc/erhe/graphics.md "SPIR-V cache records").
class Spirv_cache_record
{
public:
    std::string               main_name;         // the top-level source file (or the program name for inline source)
    std::string               main_source;       // as parsed: #version, the preamble include, the file
    std::string               preamble;          // the synthesized erhe_preamble.glsl
    std::vector<std::string>  include_paths;     // the files this stage included, as its parse resolved them
    std::vector<unsigned int> unoptimized_spirv; // stored when the cached module is the optimized one; else empty
};

// Sizes of a cached stage's modules before and after the SPIRV-Tools
// optimizer, for the shader variant tables (doc/erhe/graphics.md "SPIR-V
// cache records"); written by whoever computed them.
class Spirv_cache_module_stats
{
public:
    std::string spirv_tools_version;      // that computed the optimized module
    std::size_t unoptimized_bytes{0};
    std::size_t unoptimized_instructions{0};
    std::size_t optimized_bytes{0};        // 0: the optimizer failed or its output did not validate
    std::size_t optimized_instructions{0};
};

// On-disk cache of SPIR-V binaries, one file per (stage, compile settings,
// final GLSL source). compile_settings_hash is the caller's hash of every
// glslang setting that changes the SPIR-V output for the same source
// (SpvOptions, message masks, client / target environment, glslang
// version; Glslang_shader_stages computes it once per process), so a
// settings change changes the key and no manual version bump is needed.
// Entries are written atomically (temporary file + rename), so a crash
// during put() never leaves a partial .spv file in the cache directory.
// Thread-safe: every call reads or writes its own files.
class Spirv_cache
{
public:
    explicit Spirv_cache(const std::filesystem::path& cache_directory);

    // The entry key: 16 hex digits.
    [[nodiscard]] auto get_key(const std::string& source, Shader_type stage, uint64_t compile_settings_hash) const -> std::string;

    [[nodiscard]] auto get(const std::string& source, Shader_type stage, uint64_t compile_settings_hash) const -> std::vector<unsigned int>;
    void               put(const std::string& source, Shader_type stage, uint64_t compile_settings_hash, const std::vector<unsigned int>& spirv);
    // The module of an entry by key; empty when absent or invalid.
    [[nodiscard]] auto get_module(const std::string& key) const -> std::vector<unsigned int>;

    [[nodiscard]] auto has_record(const std::string& key) const -> bool;
    [[nodiscard]] auto get_record(const std::string& key) const -> std::optional<Spirv_cache_record>;
    void               put_record(const std::string& key, const Spirv_cache_record& record);

    [[nodiscard]] auto get_module_stats(const std::string& key) const -> std::optional<Spirv_cache_module_stats>;
    void               put_module_stats(const std::string& key, const Spirv_cache_module_stats& stats);

private:
    [[nodiscard]] auto cache_path (const std::string& key) const -> std::filesystem::path;
    [[nodiscard]] auto record_path(const std::string& key) const -> std::filesystem::path;
    [[nodiscard]] auto stats_path (const std::string& key) const -> std::filesystem::path;
    [[nodiscard]] auto put_blob   (const std::string& text) const -> std::string;
    [[nodiscard]] auto get_blob   (const std::string& id) const -> std::optional<std::string>;

    std::filesystem::path m_cache_directory;
};

} // namespace erhe::graphics
