#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/glsl_to_spirv.hpp"
#include "erhe_graphics/glsl_format_source.hpp"
#include "erhe_graphics/device.hpp"

#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
# include "erhe_graphics/gl/gl_shader_stages.hpp"
#endif
#if defined(ERHE_GRAPHICS_LIBRARY_VULKAN)
# include "erhe_graphics/vulkan/vulkan_shader_stages.hpp"
#endif

#include "erhe_verify/verify.hpp"

#include "glslang/Public/ResourceLimits.h"
#include "glslang/Public/ShaderLang.h"
#include <glslang/MachineIndependent/localintermediate.h>
#include <SPIRV/GlslangToSpv.h>

#include <algorithm>

namespace erhe::graphics {

[[nodiscard]] auto to_glslang(Shader_type shader_type) -> ::EShLanguage
{
    switch (shader_type) {
        case Shader_type::vertex_shader:   return EShLanguage::EShLangVertex;
        case Shader_type::fragment_shader: return EShLanguage::EShLangFragment;
        case Shader_type::geometry_shader: return EShLanguage::EShLangGeometry;
        case Shader_type::compute_shader:  return EShLanguage::EShLangCompute;
        default: {
            ERHE_FATAL("TODO");
        }
    }
}

#if 0 && defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
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
#endif

[[nodiscard]] auto get_built_in_resources(Device& graphics_device) -> ::TBuiltInResource
{
    ::TBuiltInResource resources = *::GetDefaultResources();

    size_t MaxFragmentUniformVectors = graphics_device.get_info().max_fragment_uniform_vectors;
    size_t MaxVertexUniformVectors   = graphics_device.get_info().max_vertex_uniform_vectors;

    resources.maxVertexUniformVectors      = static_cast<int>(MaxVertexUniformVectors);
    resources.maxVertexUniformComponents   = static_cast<int>(MaxVertexUniformVectors * 4);
    resources.maxFragmentUniformVectors    = static_cast<int>(MaxFragmentUniformVectors);
    resources.maxFragmentUniformComponents = static_cast<int>(MaxFragmentUniformVectors * 4);
    return resources;
}

Glslang_shader_stages::Glslang_shader_stages(Shader_stages_prototype_impl& shader_stages_prototype)
    : m_shader_stages_prototype{shader_stages_prototype}
{
}

Glslang_shader_stages::~Glslang_shader_stages() noexcept = default;

auto Glslang_shader_stages::compile_shader(Device& device, const Shader_stage& shader) -> bool
{
    std::string source   = m_shader_stages_prototype.get_final_source(shader, std::nullopt);
    std::string name     = !shader.paths.empty() ? shader.paths.front().string() : std::string{};
    EShLanguage language = to_glslang(shader.type);

    //std::shared_ptr<glslang::TShader> glslang_shader = std::make_shared<glslang::TShader>(language);
    m_glslang_shaders[language].reset();
    m_glslang_shaders[language] = std::make_shared<glslang::TShader>(language);
    glslang::TShader& glslang_shader = *m_glslang_shaders[language].get();

    const char* source_string = source.data();
    const int   source_length = static_cast<int>(source.size());
    if (!name.empty()) {
        const char* source_name = name.c_str();
        glslang_shader.setStringsWithLengthsAndNames(&source_string, &source_length, &source_name, 1);
    } else {
        glslang_shader.setStringsWithLengths(&source_string, &source_length, 1);
    }

    //glslang_shader->setEnvInput(glslang::EShSource::EShSourceGlsl, language, glslang::EShClient::EShClientVulkan, 100);
    glslang_shader.setEnvInput(glslang::EShSource::EShSourceGlsl, language, glslang::EShClient::EShClientOpenGL, 100);
    glslang_shader.setEnvClient(glslang::EShClient::EShClientOpenGL, glslang::EshTargetClientVersion::EShTargetOpenGL_450);
    glslang_shader.setEnvTarget(glslang::EShTargetLanguage::EShTargetSpv, glslang::EShTargetSpv_1_6);

    unsigned int messages{0};
    //messages = messages | EShMsgAST;              // print the AST intermediate representation
    messages = messages | EShMsgSpvRules;           // issue messages for SPIR-V generation
    messages = messages | EShMsgDebugInfo;          // save debug information
    messages = messages | EShMsgBuiltinSymbolTable; // print the builtin symbol table
    messages = messages | EShMsgEnhanced;           // enhanced message readability
    messages = messages | EShMsgDisplayErrorColumn; // Display error message column aswell as line

    ::TBuiltInResource glslang_built_in_resources = get_built_in_resources(device);
    const bool parse_ok = glslang_shader.parse(&glslang_built_in_resources, 100, true, static_cast<const EShMessages>(messages));
    const char* info_log = glslang_shader.getInfoLog();
    if (!parse_ok) {
        const std::string formatted_source = format_source(source);
        log_glsl->error("glslang parse error in shader = {}\n{}\n{}\n{}\n", name, info_log, formatted_source, info_log);
        //log_glsl->error("glslang parse error in shader = {}\n{}\n{}\n{}\n", name, info_log, source, info_log);
        return false;
        //m_state = state_fail;
    } else if (info_log[0] != '\0') {
        log_glsl->info("\n{}\n", info_log);
    }
    
    return true; //glslang_shader;
}

auto Glslang_shader_stages::link_program() -> bool
{
    if (m_glslang_shaders.empty()) {
        return false;
    }

    m_glslang_program = std::make_shared<glslang::TProgram>();

    m_active_stages.clear();
    for (const auto& shader : m_glslang_shaders) {
        m_active_stages.insert(shader.first);
        m_glslang_program->addShader(shader.second.get());
    }
    ERHE_VERIFY(!m_active_stages.empty());

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
        //// m_state = state_fail;
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

    for (::EShLanguage stage : m_active_stages) {
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

auto Glslang_shader_stages::get_spirv_binary(Shader_type type) const -> std::span<const unsigned int>
{
    EShLanguage language = to_glslang(type);
    auto i = m_spirv_shaders.find(language);
    if (i == m_spirv_shaders.end()) {
        return {};
    }
    const unsigned int* data = i->second.data();
    std::size_t         size = i->second.size();
    return std::span<const unsigned int>(data, size);
}

} // namespace erhe::graphics
