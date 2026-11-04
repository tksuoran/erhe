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
class Offline_compiler_tools
{
public:
    std::filesystem::path malioc;               // empty: not found
    std::string           malioc_version;
    std::string           mali_core;            // "Mali-G310"
};

// configured path, else the environment variable, else PATH; empty when none is a file.
[[nodiscard]] auto find_offline_compiler(const std::string& configured, const char* env_name, const char* exe_name) -> std::filesystem::path;

// Runs the compilers' version queries; a tool whose query fails is dropped (logged).
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
