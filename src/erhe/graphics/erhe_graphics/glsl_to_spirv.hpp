#pragma once

#include "erhe_graphics/enums.hpp"
#include "glslang/Public/ShaderLang.h"

#include <memory>
#include <span>
#include <unordered_map>
#include <unordered_set>

namespace glslang {
    class TShader;
    class TProgram;
}

namespace erhe::graphics {

class Device;
class Shader_stage;
class Shader_stages_prototype_impl;

class Glslang_shader_stages
{
public:
    Glslang_shader_stages(Shader_stages_prototype_impl& shader_stages_prototype);
    ~Glslang_shader_stages();

    auto link_program    () -> bool;
    auto compile_shader  (Device& device, const Shader_stage& shader) -> bool;
    auto get_spirv_binary(Shader_type type) const -> std::span<const unsigned int>;

private:
    Shader_stages_prototype_impl&                                        m_shader_stages_prototype;
    std::unordered_map<::EShLanguage, std::shared_ptr<glslang::TShader>> m_glslang_shaders;
    std::unordered_map<::EShLanguage, std::vector<unsigned int>>         m_spirv_shaders;
    std::unordered_set<::EShLanguage>                                    m_active_stages;
    std::shared_ptr<glslang::TProgram>                                   m_glslang_program;
};

} // namespace erhe::graphics
