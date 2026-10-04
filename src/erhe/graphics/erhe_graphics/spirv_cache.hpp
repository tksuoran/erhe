#pragma once

#include "erhe_graphics/enums.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace erhe::graphics {

// On-disk cache of SPIR-V binaries, one file per (stage, compile settings,
// final GLSL source). compile_settings_hash is the caller's hash of every
// glslang setting that changes the SPIR-V output for the same source
// (SpvOptions, message masks, client / target environment, glslang
// version; Glslang_shader_stages computes it once per process), so a
// settings change changes the key and no manual version bump is needed.
// Entries are written atomically (temporary file + rename), so a crash
// during put() never leaves a partial .spv file in the cache directory.
class Spirv_cache
{
public:
    explicit Spirv_cache(const std::filesystem::path& cache_directory);

    [[nodiscard]] auto get(const std::string& source, Shader_type stage, uint64_t compile_settings_hash) const -> std::vector<unsigned int>;
    void               put(const std::string& source, Shader_type stage, uint64_t compile_settings_hash, const std::vector<unsigned int>& spirv);

private:
    [[nodiscard]] auto compute_hash(const std::string& source, Shader_type stage, uint64_t compile_settings_hash) const -> std::string;
    [[nodiscard]] auto cache_path  (const std::string& hash) const -> std::filesystem::path;

    std::filesystem::path m_cache_directory;
};

} // namespace erhe::graphics
