#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_verify/verify.hpp"

#include "glslang/Public/ShaderLang.h"
#include <glslang/MachineIndependent/localintermediate.h>
#include <SPIRV/GlslangToSpv.h>

#include <algorithm>

namespace erhe::graphics
{

namespace
{

[[nodiscard]] auto format_source(const std::string& source) -> std::string
{
    int         line{1};
    const char* head = source.c_str();

    std::stringstream sb;
    sb << fmt::format("{:>3}: ", line);

    for (;;) {
        char c = *head;
        ++head;
        if (c == '\r') {
            continue;
        }
        if (c == 0) {
            break;
        }

        if (c == '\n') {
            ++line;
            sb << fmt::format("\n{:>3}: ", line);
            continue;
        }
        sb << c;
    }
    return sb.str();
}

} // anonymous namespace

[[nodiscard]] auto Shader_stages_prototype::compile(const Shader_stage& shader) -> std::shared_ptr<glslang::TShader>
{
    EShLanguage language = [](igl::ShaderStage gl_shader_type) -> EShLanguage {
        switch (gl_shader_type) {
            case igl::ShaderStage::Vertex:   return EShLanguage::EShLangVertex;
            case igl::ShaderStage::Fragment: return EShLanguage::EShLangFragment;
            case igl::ShaderStage::Compute:  return EShLanguage::EShLangCompute;
            default:
                ERHE_FATAL("TODO");
        }
    }(shader.type);

    std::shared_ptr<glslang::TShader> glslang_shader = std::make_shared<glslang::TShader>(language);

    const char* source_string = shader.source.data();
    const int   source_length = static_cast<int>(shader.source.size());
    if (!shader.path.empty()) {
        const char* source_name = shader.path.string().data();
        glslang_shader->setStringsWithLengthsAndNames(&source_string, &source_length, &source_name, 1);
    } else {
        glslang_shader->setStringsWithLengths(&source_string, &source_length, 1);
    }

    glslang_shader->setEnvInput(glslang::EShSource::EShSourceGlsl, language, glslang::EShClient::EShClientVulkan, 100);
    glslang_shader->setEnvClient(glslang::EShClient::EShClientVulkan, glslang::EshTargetClientVersion::EShTargetVulkan_1_1);
    glslang_shader->setEnvTarget(glslang::EShTargetLanguage::EShTargetSpv, glslang::EShTargetSpv_1_4);

    const TBuiltInResource built_in_resources{
        .maxLights                                 = 32,
        .maxClipPlanes                             = 6,
        .maxTextureUnits                           = 32,
        .maxTextureCoords                          = 32,
        .maxVertexAttribs                          = 64,
        .maxVertexUniformComponents                = 4096,
        .maxVaryingFloats                          = 64,
        .maxVertexTextureImageUnits                = 32,
        .maxCombinedTextureImageUnits              = 80,
        .maxTextureImageUnits                      = 32,
        .maxFragmentUniformComponents              = 4096,
        .maxDrawBuffers                            = 32,
        .maxVertexUniformVectors                   = 128,
        .maxVaryingVectors                         = 8,
        .maxFragmentUniformVectors                 = 16,
        .maxVertexOutputVectors                    = 16,
        .maxFragmentInputVectors                   = 15,
        .minProgramTexelOffset                     = -8,
        .maxProgramTexelOffset                     = 7,
        .maxClipDistances                          = 8,
        .maxComputeWorkGroupCountX                 = 65535,
        .maxComputeWorkGroupCountY                 = 65535,
        .maxComputeWorkGroupCountZ                 = 65535,
        .maxComputeWorkGroupSizeX                  = 1024,
        .maxComputeWorkGroupSizeY                  = 1024,
        .maxComputeWorkGroupSizeZ                  = 64,
        .maxComputeUniformComponents               = 1024,
        .maxComputeTextureImageUnits               = 16,
        .maxComputeImageUniforms                   = 8,
        .maxComputeAtomicCounters                  = 8,
        .maxComputeAtomicCounterBuffers            = 1,
        .maxVaryingComponents                      = 60,
        .maxVertexOutputComponents                 = 64,
        .maxGeometryInputComponents                = 64,
        .maxGeometryOutputComponents               = 128,
        .maxFragmentInputComponents                = 128,
        .maxImageUnits                             = 8,
        .maxCombinedImageUnitsAndFragmentOutputs   = 8,
        .maxCombinedShaderOutputResources          = 8,
        .maxImageSamples                           = 0,
        .maxVertexImageUniforms                    = 0,
        .maxTessControlImageUniforms               = 0,
        .maxTessEvaluationImageUniforms            = 0,
        .maxGeometryImageUniforms                  = 0,
        .maxFragmentImageUniforms                  = 8,
        .maxCombinedImageUniforms                  = 8,
        .maxGeometryTextureImageUnits              = 16,
        .maxGeometryOutputVertices                 = 256,
        .maxGeometryTotalOutputComponents          = 1024,
        .maxGeometryUniformComponents              = 1024,
        .maxGeometryVaryingComponents              = 64,
        .maxTessControlInputComponents             = 128,
        .maxTessControlOutputComponents            = 128,
        .maxTessControlTextureImageUnits           = 16,
        .maxTessControlUniformComponents           = 1024,
        .maxTessControlTotalOutputComponents       = 4096,
        .maxTessEvaluationInputComponents          = 128,
        .maxTessEvaluationOutputComponents         = 128,
        .maxTessEvaluationTextureImageUnits        = 16,
        .maxTessEvaluationUniformComponents        = 1024,
        .maxTessPatchComponents                    = 120,
        .maxPatchVertices                          = 32,
        .maxTessGenLevel                           = 64,
        .maxViewports                              = 16,
        .maxVertexAtomicCounters                   = 0,
        .maxTessControlAtomicCounters              = 0,
        .maxTessEvaluationAtomicCounters           = 0,
        .maxGeometryAtomicCounters                 = 0,
        .maxFragmentAtomicCounters                 = 8,
        .maxCombinedAtomicCounters                 = 8,
        .maxAtomicCounterBindings                  = 1,
        .maxVertexAtomicCounterBuffers             = 0,
        .maxTessControlAtomicCounterBuffers        = 0,
        .maxTessEvaluationAtomicCounterBuffers     = 0,
        .maxGeometryAtomicCounterBuffers           = 0,
        .maxFragmentAtomicCounterBuffers           = 1,
        .maxCombinedAtomicCounterBuffers           = 1,
        .maxAtomicCounterBufferSize                = 16384,
        .maxTransformFeedbackBuffers               = 4,
        .maxTransformFeedbackInterleavedComponents = 64,
        .maxCullDistances                          = 8,
        .maxCombinedClipAndCullDistances           = 8,
        .maxSamples                                = 4,
        .maxMeshOutputVerticesNV                   = 256,
        .maxMeshOutputPrimitivesNV                 = 512,
        .maxMeshWorkGroupSizeX_NV                  = 32,
        .maxMeshWorkGroupSizeY_NV                  = 1,
        .maxMeshWorkGroupSizeZ_NV                  = 1,
        .maxTaskWorkGroupSizeX_NV                  = 32,
        .maxTaskWorkGroupSizeY_NV                  = 1,
        .maxTaskWorkGroupSizeZ_NV                  = 1,
        .maxMeshViewCountNV                        = 4,
        .maxMeshOutputVerticesEXT                  = 256,
        .maxMeshOutputPrimitivesEXT                = 256,
        .maxMeshWorkGroupSizeX_EXT                 = 128,
        .maxMeshWorkGroupSizeY_EXT                 = 128,
        .maxMeshWorkGroupSizeZ_EXT                 = 128,
        .maxTaskWorkGroupSizeX_EXT                 = 128,
        .maxTaskWorkGroupSizeY_EXT                 = 128,
        .maxTaskWorkGroupSizeZ_EXT                 = 128,
        .maxMeshViewCountEXT                       = 4,
        .maxDualSourceDrawBuffersEXT               = 1,
        .limits = {
            .nonInductiveForLoops                 = 1,
            .whileLoops                           = 1,
            .doWhileLoops                         = 1,
            .generalUniformIndexing               = 1,
            .generalAttributeMatrixVectorIndexing = 1,
            .generalVaryingIndexing               = 1,
            .generalSamplerIndexing               = 1,
            .generalVariableIndexing              = 1,
            .generalConstantMatrixVectorIndexing  = 1
        }
    };

    unsigned int messages{0};
    //messages = messages | EShMsgAST;                // print the AST intermediate representation
    messages = messages | EShMsgSpvRules;           // issue messages for SPIR-V generation
    messages = messages | EShMsgDebugInfo;          // save debug information
    messages = messages | EShMsgBuiltinSymbolTable; // print the builtin symbol table
    messages = messages | EShMsgEnhanced;           // enhanced message readability

    const bool parse_ok = glslang_shader->parse(&built_in_resources, 100, true, static_cast<const EShMessages>(messages));
    if (!parse_ok) {
        log_glsl->error("glslang shader parse error");
    }
    const char* info_log = glslang_shader->getInfoLog();

    log_glsl->info("glslang shader parse info log:\n{}\n", info_log);

    return glslang_shader;
}

Shader_stages_prototype::Shader_stages_prototype(
    igl::IDevice&               device,
    Shader_stages_create_info&& create_info
)
    : m_device     {device}
    , m_create_info{create_info}
{
    if (create_info.build) {
        link_program();
    }
}

Shader_stages_prototype::Shader_stages_prototype(
    igl::IDevice&                    device,
    const Shader_stages_create_info& create_info
)
    : m_device     {device}
    , m_create_info{create_info}
{
    if (create_info.build) {
        link_program();
    }
}

void Shader_stages_prototype::compile_shaders()
{
    ERHE_VERIFY(m_state == state_init);
    for (const auto& shader : m_create_info.shaders) {
        auto glslang_shader = compile(shader); // TODO WIP
        if (m_state == state_fail) {
            m_state = state_fail;
            break;
        }
        m_compiled_shaders_pre_link.push_back(glslang_shader);
        unsigned int stage = static_cast<unsigned int>(glslang_shader->getStage());
        m_used_stages.insert(stage);
    }
}

auto Shader_stages_prototype::link_program() -> bool
{
    if (m_state == state_fail) {
        return false;
    }

    if (m_state == state_init) {
        compile_shaders();
    }

    if (m_state == state_fail) {
        return false;
    }

    ERHE_VERIFY(m_state == state_shader_compilation_started);

    std::shared_ptr<glslang::TProgram> glslang_program = std::make_shared<glslang::TProgram>();

    for (const auto& shader : m_compiled_shaders_pre_link) {
        glslang_program->addShader(shader.get());
    }

    unsigned int messages{0};
    //messages = messages | EShMsgAST;                // print the AST intermediate representation
    messages = messages | EShMsgSpvRules;           // issue messages for SPIR-V generation
    messages = messages | EShMsgDebugInfo;          // save debug information
    messages = messages | EShMsgBuiltinSymbolTable; // print the builtin symbol table
    messages = messages | EShMsgEnhanced;           // enhanced message readability
    

    const bool link_ok = glslang_program->link(static_cast<const EShMessages>(messages));
    if (!link_ok) {
        log_program->error("glslang program link failed");
    }

    const char* info_log = glslang_program->getInfoLog();
    log_program->info("glslang program link info log:\n{}\n", info_log);

    glslang::SpvOptions spirv_options
    {
        .generateDebugInfo                = true,
        .stripDebugInfo                   = false,
        .disableOptimizer                 = true,
        .optimizeSize                     = false,
        .disassemble                      = false,
        .validate                         = true,
        .emitNonSemanticShaderDebugInfo   = true,
        .emitNonSemanticShaderDebugSource = true
    };

    for (auto stage_i : m_used_stages) {
        spv::SpvBuildLogger logger;
        const auto stage = static_cast<EShLanguage>(stage_i);
        auto* intermediate = glslang_program->getIntermediate(stage);
        std::vector<uint32_t> spirv;
        GlslangToSpv(*intermediate, spirv, &logger, &spirv_options);
        std::string spv_messages = logger.getAllMessages();
        log_glsl->info("SPIR_V messages::\n{}\n", spv_messages);
    }

    if (!link_ok) {
        m_state = state_fail;
        log_program->error("Shader_stages linking failed:");
        for (const auto& s : m_create_info.shaders) {
            const std::string f_source = format_source(m_create_info.final_source(s));
            log_glsl->error("\n{}", f_source);
        }
        return false;
    } else {
        m_state = state_ready;
        log_program->trace("Shader_stages linking succeeded:");
        for (const auto& s : m_create_info.shaders) {
            const std::string f_source = format_source(m_create_info.final_source(s));
            log_glsl->trace("\n{}", f_source);
        }
        if (m_create_info.dump_interface) {
            const std::string f_source = format_source(m_create_info.interface_source());
            log_glsl->info("\n{}", f_source);
        }
        if (m_create_info.dump_final_source) {
            for (const auto& s : m_create_info.shaders) {
                const std::string f_source = format_source(m_create_info.final_source(s));
                log_glsl->info("\n{}", f_source);
            }
        }
    }

    m_state = state_ready;
    return true;
}

[[nodiscard]] auto Shader_stages_prototype::is_valid() -> bool
{
    if ((m_state != state_ready) && (m_state != state_fail)) {
        link_program();
    }
    if (m_state == state_ready) {
        return true;
    }
    //if (m_state == state_fail)
    {
        return false;
    }
}

auto is_array_and_nonzero(const std::string& name)
{
    const std::size_t open_bracket_pos = name.find_first_of('[');
    if (open_bracket_pos == std::string::npos) {
        return false;
    }

    const std::size_t digit_pos = name.find_first_of("0123456789", open_bracket_pos + 1);
    if (digit_pos != open_bracket_pos + 1) {
        return false;
    }

    const std::size_t non_digit_pos = name.find_first_not_of("0123456789", digit_pos + 1);
    if (non_digit_pos == std::string::npos) {
        return false;
    }

    if (name.at(non_digit_pos) != ']') {
        return false;
    }

    const std::size_t close_bracket_pos = non_digit_pos;
    const char        digit             = name.at(digit_pos);

    if (
        (close_bracket_pos == (open_bracket_pos + 2)) &&
        (
            (digit == '0') ||
            (digit == '1')
        )
    ) {
        return false;
    }

    return true;
}

auto Shader_stages_prototype::create_info() const -> const Shader_stages_create_info&
{
    return m_create_info;
}

auto Shader_stages_prototype::name() const -> const std::string&
{
    return m_create_info.name;
}

} // namespace erhe::graphics
