#pragma once

#include "erhe_graphics/enums.hpp"
#include "glslang/Public/ShaderLang.h"

#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace glslang {
    class TShader;
    class TProgram;
}

namespace erhe::graphics {

class Device;
class Shader_stage;
class Shader_stages_prototype_impl;
class Spirv_cache;

class Glslang_shader_stages
{
public:
    Glslang_shader_stages(Shader_stages_prototype_impl& shader_stages_prototype, Spirv_cache* cache = nullptr);
    ~Glslang_shader_stages() noexcept;

    auto link_program          (Device& device) -> bool;
    auto compile_shader        (Device& device, const Shader_stage& shader) -> bool;
    auto get_spirv_binary      (Shader_type type) const -> std::span<const unsigned int>;
    auto get_last_compile_log  () const -> const std::string&;
    auto get_last_link_log     () const -> const std::string&;

    // Check if all stages can be loaded from cache, populating m_spirv_shaders directly
    auto try_load_all_from_cache(Device& device) -> bool;

private:
    Shader_stages_prototype_impl&                                        m_shader_stages_prototype;
    Spirv_cache*                                                         m_cache{nullptr};
    std::unordered_map<::EShLanguage, std::shared_ptr<glslang::TShader>> m_glslang_shaders;
    std::unordered_map<::EShLanguage, std::vector<unsigned int>>         m_spirv_shaders;
    std::unordered_set<::EShLanguage>                                    m_active_stages;
    std::shared_ptr<glslang::TProgram>                                   m_glslang_program;
    std::string                                                          m_last_compile_log;
    std::string                                                          m_last_link_log;

    // The sources of each compiled stage, kept only while the device records
    // the shader variant browser (Device::get_shader_variant_report()).
    class Report_source
    {
    public:
        std::string main_name;
        std::string main_source;
        std::string preamble;
    };
    std::unordered_map<::EShLanguage, Report_source>                     m_report_sources;
};

} // namespace erhe::graphics
