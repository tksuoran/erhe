#include "erhe_graphics/shader_stages_prototype.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_verify/verify.hpp"

#include "glslang/Public/ResourceLimits.h"
#include "glslang/Public/ShaderLang.h"
#include <glslang/MachineIndependent/localintermediate.h>
#include <SPIRV/GlslangToSpv.h>

#include <algorithm>

namespace erhe::graphics
{

namespace
{

[[nodiscard]] auto to_igl(const ::EShLanguage glslang_stage) -> igl::ShaderStage
{
    switch (glslang_stage) {
        case EShLangVertex:   return igl::ShaderStage::Vertex;
        case EShLangFragment: return igl::ShaderStage::Fragment;
        case EShLangCompute:  return igl::ShaderStage::Compute;
        default:              ERHE_FATAL("TODO");
    }
}

[[nodiscard]] auto to_glslang(igl::ShaderStage igl_stage) -> ::EShLanguage
{
    switch (igl_stage) {
        case igl::ShaderStage::Vertex:   return EShLanguage::EShLangVertex;
        case igl::ShaderStage::Fragment: return EShLanguage::EShLangFragment;
        case igl::ShaderStage::Compute:  return EShLanguage::EShLangCompute;
        default: {
            ERHE_FATAL("TODO");
        }
    }
}

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

[[nodiscard]] auto get_built_in_resources(igl::IDevice& device) -> ::TBuiltInResource
{
    ::TBuiltInResource resources = *::GetDefaultResources();

    size_t MaxFragmentUniformVectors = resources.maxFragmentUniformVectors;
    size_t MaxVertexUniformVectors   = resources.maxVertexUniformVectors;

    device.getFeatureLimits(igl::DeviceFeatureLimits::MaxFragmentUniformVectors, MaxFragmentUniformVectors);
    device.getFeatureLimits(igl::DeviceFeatureLimits::MaxVertexUniformVectors,   MaxVertexUniformVectors);
    resources.maxVertexUniformVectors      = static_cast<int>(MaxVertexUniformVectors);
    resources.maxVertexUniformComponents   = static_cast<int>(MaxVertexUniformVectors * 4);
    resources.maxFragmentUniformVectors    = static_cast<int>(MaxFragmentUniformVectors);
    resources.maxFragmentUniformComponents = static_cast<int>(MaxFragmentUniformVectors * 4);
    return resources;
}

[[nodiscard]] auto Shader_stages_prototype::compile(const Shader_stage_create_info& stage_create_info) -> std::shared_ptr<glslang::TShader>
{
    EShLanguage language = to_glslang(stage_create_info.stage);

    std::shared_ptr<glslang::TShader> glslang_shader = std::make_shared<glslang::TShader>(language);

    const char* source_string = stage_create_info.source.c_str();
    const int   source_length = static_cast<int>(stage_create_info.source.size());
    if (!stage_create_info.path.empty()) {
        std::string path_string = stage_create_info.path.string();
        const char* source_name = path_string.c_str();
        glslang_shader->setStringsWithLengthsAndNames(&source_string, &source_length, &source_name, 1);
    } else {
        glslang_shader->setStringsWithLengths(&source_string, &source_length, 1);
    }

    glslang_shader->setEnvInput(glslang::EShSource::EShSourceGlsl, language, glslang::EShClient::EShClientVulkan, 100);
    glslang_shader->setEnvClient(glslang::EShClient::EShClientVulkan, glslang::EshTargetClientVersion::EShTargetVulkan_1_1);
    glslang_shader->setEnvTarget(glslang::EShTargetLanguage::EShTargetSpv, glslang::EShTargetSpv_1_4);

    unsigned int messages{0};
    //messages = messages | EShMsgAST;                // print the AST intermediate representation
    messages = messages | EShMsgSpvRules;           // issue messages for SPIR-V generation
    messages = messages | EShMsgDebugInfo;          // save debug information
    messages = messages | EShMsgBuiltinSymbolTable; // print the builtin symbol table
    messages = messages | EShMsgEnhanced;           // enhanced message readability

    ::TBuiltInResource glslang_built_in_resources = get_built_in_resources(m_device);
    const bool parse_ok = glslang_shader->parse(&glslang_built_in_resources, 100, true, static_cast<const EShMessages>(messages));
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
    ERHE_VERIFY(m_state == State::init);
    m_state = State::shader_compilation_started;
    m_stages.reserve(m_create_info.stage_create_infos.size());
    for (const auto& shader : m_create_info.stage_create_infos) {
        auto glslang_shader = compile(shader);
        if (m_state == State::fail) {
            break;
        }
        m_stages.emplace_back(glslang_shader);
    }
    if (m_state == State::fail) {
        m_stages.clear();
    }
}

auto Shader_stages_prototype::link_program() -> bool
{
    if (m_state == State::fail) {
        return false;
    }

    if (m_state == State::init) {
        compile_shaders();
    }

    if (m_stages.size() != m_create_info.stage_create_infos.size()) {
        m_state = State::fail;
    }

    if (m_state == State::fail) {
        return false;
    }

    ERHE_VERIFY(m_state == State::shader_compilation_started);

    std::shared_ptr<glslang::TProgram> glslang_program = std::make_shared<glslang::TProgram>();

    for (const auto& stage : m_stages) {
        glslang_program->addShader(stage.glslang_shader.get());
    }

    unsigned int messages{0};
    //messages = messages | EShMsgAST;                // print the AST intermediate representation
    messages = messages | EShMsgSpvRules;           // issue messages for SPIR-V generation
    messages = messages | EShMsgDebugInfo;          // save debug information
    messages = messages | EShMsgBuiltinSymbolTable; // print the builtin symbol table
    messages = messages | EShMsgEnhanced;           // enhanced message readability
    

    bool link_ok = glslang_program->link(static_cast<const EShMessages>(messages));
    const char* info_log = glslang_program->getInfoLog();
    if ((info_log != nullptr) && info_log[0] != '\0') {
        log_program->info("glslang program link info log:\n{}\n", info_log);
    }
    if (!link_ok) {
        log_program->error("glslang program link failed");
        m_state = State::fail;
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
        .emitNonSemanticShaderDebugSource = true
    };

    for (size_t i = 0, end = m_stages.size(); i < end; ++i) {
        auto&                     stage = m_stages[i];
        Shader_stage_create_info& stage_create_info = m_create_info.stage_create_infos[i];
        spv::SpvBuildLogger       logger;
        ::EShLanguage             glslang_stage = to_glslang(stage_create_info.stage);
        auto*                     intermediate = glslang_program->getIntermediate(glslang_stage);
        GlslangToSpv(*intermediate, stage.spv_binary, &logger, &spirv_options);
        const igl::ShaderModuleDesc shader_module_desc{
            .info = igl::ShaderModuleInfo{
                .stage      = stage_create_info.stage,
                .entryPoint = stage_create_info.entrypoint
            },
            .input = {
                .data   = stage.spv_binary.data(),
                .length = sizeof(uint32_t) * stage.spv_binary.size(),
                .type   = igl::ShaderInputType::Binary
            }
        };
        igl::Result createShaderModuleResult{};
        stage.module = m_device.createShaderModule(shader_module_desc, &createShaderModuleResult);

        std::string spv_messages = logger.getAllMessages();
        if (!spv_messages.empty()) {
            log_glsl->info("SPIR_V messages::\n{}\n", spv_messages);
        }

        if (!stage.module) {
            m_state = State::fail;
            log_program->error("igl shader module create failed: {}", createShaderModuleResult.message);
            const std::string f_source = format_source(m_create_info.final_source(stage_create_info));
            log_glsl->error("\n{}", f_source);
        }
    }

    if (m_state == State::fail) {
        return false;
    }

    igl::ShaderStagesDesc desc;
    for (size_t i = 0, end = m_stages.size(); i < end; ++i) {
        auto&                     stage = m_stages[i];
        Shader_stage_create_info& stage_create_info = m_create_info.stage_create_infos[i];
        switch (stage_create_info.stage) {
            case igl::ShaderStage::Vertex: {
                desc.vertexModule = stage.module;
                desc.type         = igl::ShaderStagesType::Render;
                break;
            }
            case igl::ShaderStage::Fragment: {
                desc.fragmentModule = stage.module;
                desc.type           = igl::ShaderStagesType::Render;
                break;
            }
            case igl::ShaderStage::Compute: {
                desc.computeModule = stage.module;
                desc.type          = igl::ShaderStagesType::Compute;
                break;
            }
            default: {
                ERHE_FATAL("Unsupported shader stage");
            }
        }
    }

    igl::Result createShaderStagesResult{};
    m_shader_stages = m_device.createShaderStages(desc, &createShaderStagesResult);
    if (!m_shader_stages) {
        m_state = State::fail;
        log_program->error("igl shader stage create failed: {}", createShaderStagesResult.message);
        return false;
    }

    log_program->trace("Shader_stages linking succeeded:");
    for (const auto& stage : m_create_info.stage_create_infos) {
        const std::string f_source = format_source(m_create_info.final_source(stage));
        log_glsl->trace("\n{}", f_source);
    }
    if (m_create_info.dump_interface) {
        const std::string f_source = format_source(m_create_info.interface_source());
        log_glsl->info("\n{}", f_source);
    }
    if (m_create_info.dump_final_source) {
        for (const auto& stage : m_create_info.stage_create_infos) {
            const std::string f_source = format_source(m_create_info.final_source(stage));
            log_glsl->info("\n{}", f_source);
        }
    }

    m_state = State::ready;
    return true;
}

[[nodiscard]] auto Shader_stages_prototype::is_valid() -> bool
{
    if ((m_state != State::ready) && (m_state != State::fail)) {
        link_program();
    }

    return (m_state == State::ready);
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
