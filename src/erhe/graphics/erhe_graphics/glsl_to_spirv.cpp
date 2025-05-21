#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/glsl_format_source.hpp"
#include "erhe_graphics/instance.hpp"
#include "erhe_gl/wrapper_enums.hpp"
#include "erhe_verify/verify.hpp"

#include "glslang/Public/ResourceLimits.h"
#include "glslang/Public/ShaderLang.h"
#include <glslang/MachineIndependent/localintermediate.h>
#include <SPIRV/GlslangToSpv.h>

#include <algorithm>
#include <unordered_set>

namespace erhe::graphics {

[[nodiscard]] auto to_gl(const ::EShLanguage glslang_stage) -> gl::Shader_type
{
    switch (glslang_stage) {
        case EShLanguage::EShLangVertex:   return gl::Shader_type::vertex_shader;
        case EShLanguage::EShLangFragment: return gl::Shader_type::fragment_shader;
        case EShLanguage::EShLangGeometry: return gl::Shader_type::geometry_shader;
        case EShLanguage::EShLangCompute:  return gl::Shader_type::compute_shader;
        default: {
            ERHE_FATAL("TODO");
        }
    }
}

[[nodiscard]] auto to_glslang(gl::Shader_type shader_type) -> ::EShLanguage
{
    switch (shader_type) {
        case gl::Shader_type::vertex_shader:   return EShLanguage::EShLangVertex;
        case gl::Shader_type::fragment_shader: return EShLanguage::EShLangFragment;
        case gl::Shader_type::geometry_shader: return EShLanguage::EShLangGeometry;
        case gl::Shader_type::compute_shader:  return EShLanguage::EShLangCompute;
        default: {
            ERHE_FATAL("TODO");
        }
    }
}

[[nodiscard]] auto get_built_in_resources(Instance& graphics_instance) -> ::TBuiltInResource
{
    ::TBuiltInResource resources = *::GetDefaultResources();

    size_t MaxFragmentUniformVectors = graphics_instance.limits.max_fragment_uniform_vectors;
    size_t MaxVertexUniformVectors   = graphics_instance.limits.max_vertex_uniform_vectors;

    resources.maxVertexUniformVectors      = static_cast<int>(MaxVertexUniformVectors);
    resources.maxVertexUniformComponents   = static_cast<int>(MaxVertexUniformVectors * 4);
    resources.maxFragmentUniformVectors    = static_cast<int>(MaxFragmentUniformVectors);
    resources.maxFragmentUniformComponents = static_cast<int>(MaxFragmentUniformVectors * 4);
    return resources;
}

auto Shader_stages_prototype::compile_glslang(const Shader_stage& shader) -> std::shared_ptr<glslang::TShader>
{
    gl::Shader_type gl_shader_type = shader.type;
    std::string     source         = get_final_source(shader, std::nullopt);
    std::string     name           = !shader.paths.empty() ? shader.paths.front().string() : std::string{};
    EShLanguage     language       = to_glslang(gl_shader_type);

    std::shared_ptr<glslang::TShader> glslang_shader = std::make_shared<glslang::TShader>(language);

    const char* source_string = source.data();
    const int   source_length = static_cast<int>(source.size());
    if (!name.empty()) {
        const char* source_name = name.c_str();
        glslang_shader->setStringsWithLengthsAndNames(&source_string, &source_length, &source_name, 1);
    } else {
        glslang_shader->setStringsWithLengths(&source_string, &source_length, 1);
    }

    //glslang_shader->setEnvInput(glslang::EShSource::EShSourceGlsl, language, glslang::EShClient::EShClientVulkan, 100);
    glslang_shader->setEnvInput(glslang::EShSource::EShSourceGlsl, language, glslang::EShClient::EShClientOpenGL, 100);
    glslang_shader->setEnvClient(glslang::EShClient::EShClientOpenGL, glslang::EshTargetClientVersion::EShTargetOpenGL_450);
    glslang_shader->setEnvTarget(glslang::EShTargetLanguage::EShTargetSpv, glslang::EShTargetSpv_1_6);

    unsigned int messages{0};
    //messages = messages | EShMsgAST;              // print the AST intermediate representation
    messages = messages | EShMsgSpvRules;           // issue messages for SPIR-V generation
    messages = messages | EShMsgDebugInfo;          // save debug information
    messages = messages | EShMsgBuiltinSymbolTable; // print the builtin symbol table
    messages = messages | EShMsgEnhanced;           // enhanced message readability
    messages = messages | EShMsgDisplayErrorColumn; // Display error message column aswell as line

    ::TBuiltInResource glslang_built_in_resources = get_built_in_resources(m_graphics_instance);
    const bool parse_ok = glslang_shader->parse(&glslang_built_in_resources, 100, true, static_cast<const EShMessages>(messages));
    const char* info_log = glslang_shader->getInfoLog();
    if (!parse_ok) {
        std::string formatted_source = format_source(source);
        log_glsl->error("glslang parse error in shader = {}\n{}\n{}\n{}\n", name, info_log, formatted_source, info_log);
        //log_glsl->error("glslang parse error in shader = {}\n{}\n{}\n{}\n", name, info_log, source, info_log);
        m_state = state_fail;
    } else if (info_log[0] != '\0') {
        log_glsl->info("\n{}\n", info_log);
    }
    
    return glslang_shader;
}

auto Shader_stages_prototype::link_glslang_program() -> bool
{
    if (m_glslang_shaders.empty()) {
        return false;
    }

    m_glslang_program = std::make_shared<glslang::TProgram>();

    std::unordered_set<::EShLanguage> active_stages;
    for (const auto& shader : m_glslang_shaders) {
        active_stages.insert(shader->getStage());
        m_glslang_program->addShader(shader.get());
    }
    ERHE_VERIFY(!active_stages.empty());

    unsigned int messages{0};
    //messages = messages | EShMsgAST;                // print the AST intermediate representation
    messages = messages | EShMsgSpvRules;           // issue messages for SPIR-V generation
    messages = messages | EShMsgDebugInfo;          // save debug information
    messages = messages | EShMsgBuiltinSymbolTable; // print the builtin symbol table
    messages = messages | EShMsgEnhanced;           // enhanced message readability
    messages = messages | EShMsgAbsolutePath;       // Output Absolute path for messages
    messages = messages | EShMsgDisplayErrorColumn; // Display error message column aswell as line

    bool link_ok = m_glslang_program->link(static_cast<const EShMessages>(messages));
    const char* info_log = m_glslang_program->getInfoLog();
    if ((info_log != nullptr) && info_log[0] != '\0') {
        log_program->info("glslang program link info log:\n{}\n", info_log);
    }
    if (!link_ok) {
        log_program->error("glslang program link failed");
        m_state = state_fail;
        return false;
    }

    glslang::SpvOptions spirv_options{
        .generateDebugInfo                = true,
        .stripDebugInfo                   = false,
        .disableOptimizer                 = true,
        .optimizeSize                     = false,
        .disassemble                      = false,
        .validate                         = true,
        .emitNonSemanticShaderDebugInfo   = true,
        .emitNonSemanticShaderDebugSource = true,
        .compileOnly                      = false,
        .optimizerAllowExpandedIDBound    = false
    };

    for (::EShLanguage stage : active_stages) {
        spv::SpvBuildLogger       logger;
        auto*                     intermediate  = m_glslang_program->getIntermediate(stage);
        std::vector<unsigned int> spirv_binary;
        GlslangToSpv(*intermediate, spirv_binary, &logger, &spirv_options);
        std::string spv_messages = logger.getAllMessages();
        if (!spv_messages.empty()) {
            log_glsl->info("SPIR_V messages::\n{}\n", spv_messages);
        }
        m_spirv_shaders[stage] = std::move(spirv_binary);
    }

    return true;
}

} // namespace erhe::graphics
