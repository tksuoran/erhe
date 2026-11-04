#pragma once

#include "renderers/shader_analytics.hpp"

#include "erhe_graphics/enums.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace editor {

// The Mali offline compiler (doc/editor/shader_analytics.md),
// found and version-queried once; analyze() runs it on one program.
// The executables one search found, in search order, and what it searched.
class Offline_compiler_candidates
{
public:
    std::vector<std::filesystem::path> paths;
    std::vector<std::string>           searched; // "settings '<entry>'", "ERHE_MALIOC '<entry>'", "PATH"
};

class Offline_compiler_tools
{
public:
    std::vector<std::filesystem::path> malioc_candidates;
    std::filesystem::path malioc;               // empty: not found
    std::string           malioc_version;
    std::string           mali_core;            // "Mali-G310"
};

// Every executable found through the configured entries, the env_name path
// list and PATH (each entry a file or a directory; $NAME, ${NAME}, ~ expand).
[[nodiscard]] auto find_offline_compiler_candidates(std::span<const std::string> configured_paths, const char* env_name, const char* exe_name) -> Offline_compiler_candidates;

// Queries every candidate's version and selects the newest per compiler (logged).
void query_offline_compiler_versions(Offline_compiler_tools& tools);

class Offline_compiler_stage
{
public:
    erhe::graphics::Shader_type type;
    std::vector<unsigned int>   spirv;
};

// Analyzes one program (any thread, blocks while the compilers run): Mali on
// the fragment module. Raw reports are cached
// under cache_directory by a hash of the modules, core and compiler version,
// so a module is compiled once per compiler version. NaN metrics: not reported.
[[nodiscard]] auto analyze_program(
    const Offline_compiler_tools&             tools,
    std::span<const Offline_compiler_stage>   stages,
    const std::filesystem::path&              cache_directory,
    std::string&                              error
) -> Shader_cost_metrics;

} // namespace editor
