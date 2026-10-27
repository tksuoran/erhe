#pragma once

#include "erhe_graphics/enums.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace erhe::graphics {

class Shader_stages_create_info;

// One stage of a program compiled by Glslang_shader_stages, as the shader
// variant browser shows it: the sources handed to glslang and the SPIR-V
// before and after the SPIRV-Tools optimizer.
class Shader_variant_report_stage
{
public:
    Shader_type                type{Shader_type::vertex_shader};
    std::string                main_name;       // the top-level source file (or the program name for inline source)
    std::string                main_source;     // as parsed: #version, the preamble include, the file
    std::string                preamble;        // the synthesized erhe_preamble.glsl (defines, declarations)
    std::vector<unsigned int>  spirv;           // glslang output, unoptimized
    std::vector<unsigned int>  optimized_spirv; // empty when the optimizer failed or its output did not validate
};

// The shader variant browser (Graphics_config::shader_variant_browser_directory,
// doc/erhe/graphics.md "Shader variant browser"): an HTML report of every
// program Glslang_shader_stages compiles in this process. Each add() writes
// the program's detail page (sources, SPIR-V disassembly before and after
// optimization) and the raw .spv files under variants/, and rewrites
// index.html, whose table lists every program recorded so far with its
// SPIR-V sizes and instruction counts. Programs served from the Spirv_cache
// are not compiled and so not recorded; delete the cache directory to record
// every program. Thread-safe: programs compile on any thread.
class Shader_variant_report
{
public:
    explicit Shader_variant_report(const std::filesystem::path& directory);

    void add(
        const Shader_stages_create_info&                create_info,
        std::span<const Shader_variant_report_stage>    stages,
        std::span<const std::filesystem::path>          include_paths
    );

private:
    class Stage_row
    {
    public:
        Shader_type type;
        std::size_t spirv_bytes;
        std::size_t optimized_spirv_bytes;
        std::size_t instruction_count;
        std::size_t optimized_instruction_count;
    };
    class Row
    {
    public:
        std::size_t                                       id;
        std::string                                       name;
        std::vector<std::pair<std::string, std::string>> defines;
        std::vector<Stage_row>                           stages;
    };

    void write_index() const;

    std::filesystem::path m_directory;
    std::mutex            m_mutex;
    std::vector<Row>      m_rows;
};

} // namespace erhe::graphics
