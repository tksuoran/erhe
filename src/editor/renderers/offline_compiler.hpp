#pragma once

#include "renderers/shader_analytics.hpp"

#include "erhe_graphics/enums.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace editor {

// The Mali and PowerVR offline compilers (doc/editor/shader_analytics.md),
// found and version-queried once; analyze() runs them on one program.
// The executables one search found, in search order, and what it searched.
class Offline_compiler_candidates
{
public:
    std::vector<std::filesystem::path> paths;
    std::vector<std::string>           searched; // "settings '<entry>'", "ERHE_MALIOC '<entry>'", "PATH"
};

inline constexpr const char* c_powervr_executable = "ProfilingCompilerCLIWrapper";

class Offline_compiler_tools
{
public:
    std::vector<std::filesystem::path> malioc_candidates;
    std::vector<std::filesystem::path> powervr_candidates;
    std::filesystem::path malioc;               // empty: not found
    std::string           malioc_version;
    std::filesystem::path powervr;              // ProfilingCompilerCLIWrapper; empty: not found
    std::string           powervr_version;      // the compiler library's DDK build, "MAIN@6451761"
    std::string           powervr_bvnc;         // "35.3.408.101"
    std::string           mali_core;            // "Mali-G310"
    std::string           powervr_core;         // "PowerVR-BXM-8-256" or "PowerVR-<BVNC>"
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

class Offline_compiler_program_stage
{
public:
    erhe::graphics::Shader_type type;
    std::vector<unsigned int>   before; // unoptimized
    std::vector<unsigned int>   after;  // optimized; empty when the optimizer failed
};

// The full analytics of one program (any thread, blocks while the compilers
// run), as scripts/shader_analytics.py summarizes them (sidecar schema 2,
// doc/erhe/graphics.md "Mali analytics", "PowerVR analytics"): per analyzed core {"block": the core block's fields without
// "programs", "program": {stage: {"before": summary, "after": summary}}}.
// Raw reports are cached as analyze_program() caches them.
[[nodiscard]] auto analyze_program_stages(
    const Offline_compiler_tools&                   tools,
    std::span<const Offline_compiler_program_stage> stages,
    const std::filesystem::path&                    cache_directory
) -> nlohmann::ordered_json;

// Analyzes one program (any thread, blocks while the compilers run): Mali and
// PowerVR on the fragment module. Raw reports are cached
// under cache_directory by a hash of the modules, core and compiler version,
// so a module is compiled once per compiler version. NaN metrics: not reported.
[[nodiscard]] auto analyze_program(
    const Offline_compiler_tools&             tools,
    std::span<const Offline_compiler_stage>   stages,
    const std::filesystem::path&              cache_directory,
    std::string&                              error
) -> Shader_cost_metrics;

} // namespace editor
