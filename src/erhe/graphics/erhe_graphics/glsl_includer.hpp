#pragma once

#include "glslang/Public/ShaderLang.h"

#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace erhe::graphics {

// glslang #include resolver used by the Vulkan/Metal SPIR-V compile path.
// Keeps every original source unit as its own named source string so that
// emitted SPIR-V debug info (OpString / DebugSource) references the real
// per-file contents instead of one flattened blob.
//
// One virtual include name (preamble_include_name) is resolved to an
// in-memory preamble string supplied by the caller. Everything else is
// resolved against primary_dir first, then each entry of
// extra_include_paths, using the same fallback order as Glsl_file_loader.
class Glsl_includer final : public glslang::TShader::Includer
{
public:
    Glsl_includer(
        std::filesystem::path               primary_dir,
        std::vector<std::filesystem::path>  extra_include_paths,
        std::string                         preamble,
        std::string                         preamble_include_name,
        std::vector<std::filesystem::path>* dependencies_out
    );
    ~Glsl_includer() override;

    IncludeResult* includeSystem(const char* header_name, const char* includer_name, size_t inclusion_depth) override;
    IncludeResult* includeLocal (const char* header_name, const char* includer_name, size_t inclusion_depth) override;
    void           releaseInclude(IncludeResult* result) override;

    // The synthesized preamble served for preamble_include_name; exposed so
    // compile-error diagnostics can dump it next to the main source.
    [[nodiscard]] auto get_preamble() const -> const std::string& { return m_preamble; }

private:
    IncludeResult* resolve(const char* header_name);

    std::filesystem::path                m_primary_dir;
    std::vector<std::filesystem::path>   m_extra_include_paths;
    std::string                          m_preamble;
    std::string                          m_preamble_include_name;
    std::vector<std::filesystem::path>*  m_dependencies_out{nullptr};
    std::set<std::filesystem::path>      m_active_paths;
};

} // namespace erhe::graphics
