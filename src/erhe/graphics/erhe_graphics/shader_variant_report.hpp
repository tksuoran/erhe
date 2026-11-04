#pragma once

#include "erhe_graphics/enums.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string_view>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace erhe::graphics {

class Shader_stages_create_info;

[[nodiscard]] auto count_spirv_instructions(std::span<const unsigned int> spirv) -> std::size_t;
// Indented, friendly names; "(none)" for an empty module.
[[nodiscard]] auto disassemble_spirv       (std::span<const unsigned int> spirv) -> std::string;
// "vert", "frag", "comp", ...: the stage part of the browser's file names.
[[nodiscard]] auto get_shader_variant_stage_short_name(Shader_type type) -> const char*;

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

// What produced the recorded modules, written to variants/manifest.json for
// scripts/shader_analytics.py: the same for every program of a process.
class Shader_variant_report_tools
{
public:
    std::string api;                  // "vulkan", "opengl", "metal"
    std::string glslang_version;
    std::string spirv_tools_version;  // spvSoftwareVersionString()
    std::string spirv_target_env;     // the optimizer's target environment, "vulkan1.3" / "opengl4.5"
    bool        spirv_optimization;   // Graphics_config::spirv_optimization: which module the device uses
    std::string texture_heap_path;    // Device_info::texture_heap_path
    uint32_t    texture_heap_size;    // Device_info::texture_heap_size
};

// The shader variant browser (doc/erhe/graphics.md "Shader variant browser").
// Thread-safe: programs compile on any thread.
class Shader_variant_report
{
public:
    explicit Shader_variant_report(const std::filesystem::path& directory);

    void add(
        const Shader_stages_create_info&                create_info,
        std::span<const Shader_variant_report_stage>    stages,
        std::span<const std::filesystem::path>          include_paths,
        const Shader_variant_report_tools&              tools
    );

    // Called at the end of each add(), on the compiling thread, under the report's mutex.
    void set_add_callback(std::function<void()> callback);
    [[nodiscard]] auto get_directory() const -> const std::filesystem::path&;

    // The id (variants/<id>.html) of the recorded program with this name and defines.
    [[nodiscard]] auto find_program_id(
        std::string_view                                        name,
        const std::vector<std::pair<std::string, std::string>>& defines
    ) -> std::optional<std::string>;

private:
    class Stage_row
    {
    public:
        Shader_type type;
        std::size_t spirv_bytes;
        std::size_t optimized_spirv_bytes;
        std::size_t instruction_count;
        std::size_t optimized_instruction_count;
        std::string main_name;
    };
    class Row
    {
    public:
        std::string                                       id;    // stable: from name and defines
        std::size_t                                       index; // recording order
        std::string                                       name;
        std::vector<std::pair<std::string, std::string>> defines;
        std::vector<std::pair<std::string, std::string>> define_value_names; // (define, get_define_value_expression())
        std::vector<Stage_row>                           stages;
        std::vector<std::string>                         includes; // besides the stages' main files
    };

    void write_index   () const;
    void write_manifest() const;

    std::filesystem::path       m_directory;
    std::mutex                  m_mutex;
    std::vector<Row>            m_rows;
    Shader_variant_report_tools m_tools;
    std::function<void()>       m_add_callback;
};

} // namespace erhe::graphics
