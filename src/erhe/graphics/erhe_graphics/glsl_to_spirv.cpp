#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/glsl_to_spirv.hpp"
#include "erhe_graphics/glsl_format_source.hpp"
#include "erhe_graphics/glsl_includer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/spirv_cache.hpp"
#include "erhe_file/file.hpp"

#if defined(ERHE_GRAPHICS_API_OPENGL)
# include "erhe_graphics/gl/gl_shader_stages.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_VULKAN)
# include "erhe_graphics/vulkan/vulkan_shader_stages.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_METAL)
# include "erhe_graphics/metal/metal_shader_stages.hpp"
#endif

#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include "glslang/Public/ResourceLimits.h"
#include "glslang/Public/ShaderLang.h"
#include <glslang/MachineIndependent/localintermediate.h>
#include <glslang/build_info.h>
#include <SPIRV/GlslangToSpv.h>
#include <SPIRV/disassemble.h>

#include <algorithm>
#include <mutex>
#include <sstream>

namespace erhe::graphics {

[[nodiscard]] auto to_glslang(const Shader_type shader_type) -> ::EShLanguage
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

[[nodiscard]] auto glslang_stage_name(::EShLanguage language) -> const char*
{
    switch (language) {
        case EShLanguage::EShLangVertex:   return "vertex";
        case EShLanguage::EShLangFragment: return "fragment";
        case EShLanguage::EShLangGeometry: return "geometry";
        case EShLanguage::EShLangCompute:  return "compute";
        default:                           return "unknown";
    }
}

#if 0 && defined(ERHE_GRAPHICS_API_OPENGL)
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

[[nodiscard]] auto to_glslang(const gl::Shader_type shader_type) -> ::EShLanguage
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

    const size_t MaxFragmentUniformVectors = graphics_device.get_info().max_fragment_uniform_vectors;
    const size_t MaxVertexUniformVectors   = graphics_device.get_info().max_vertex_uniform_vectors;

    resources.maxVertexUniformVectors      = static_cast<int>(MaxVertexUniformVectors);
    resources.maxVertexUniformComponents   = static_cast<int>(MaxVertexUniformVectors * 4);
    resources.maxFragmentUniformVectors    = static_cast<int>(MaxFragmentUniformVectors);
    resources.maxFragmentUniformComponents = static_cast<int>(MaxFragmentUniformVectors * 4);
    return resources;
}

Glslang_shader_stages::Glslang_shader_stages(Shader_stages_prototype_impl& shader_stages_prototype, Spirv_cache* cache)
    : m_shader_stages_prototype{shader_stages_prototype}
    , m_cache{cache}
{
    // Log the linked glslang version once per process so we can confirm
    // the SPIR-V we ship was produced by the version we pinned in
    // CMakeLists.txt (and not, for example, served from a stale cache
    // entry built by an earlier glslang).
    static std::once_flag logged_glslang_version;
    std::call_once(logged_glslang_version, []() {
        log_program->info(
            "glslang version: {}.{}.{}{}",
            GLSLANG_VERSION_MAJOR,
            GLSLANG_VERSION_MINOR,
            GLSLANG_VERSION_PATCH,
            GLSLANG_VERSION_FLAVOR
        );
    });
}

Glslang_shader_stages::~Glslang_shader_stages() noexcept = default;

auto Glslang_shader_stages::compile_shader(Device& device, const Shader_stage& shader) -> bool
{
    const EShLanguage language = to_glslang(shader.type);

    const Shader_stages_create_info& create_info = m_shader_stages_prototype.create_info();

    // Build the synthesized preamble by calling final_source() with both
    // the shader paths AND inline source cleared, so neither the file-loader
    // branch nor the inline-source append branch in final_source() emits
    // the shader body. Otherwise the body would end up in both the preamble
    // (served via #include) and main_source below, causing redefinition
    // errors for any top-of-shader declarations (varyings, etc.).
    std::string preamble;
    {
        Shader_stage preamble_shader = shader;
        preamble_shader.paths.clear();
        preamble_shader.source.clear();
        preamble = create_info.final_source(device, preamble_shader, nullptr, std::nullopt);
    }

    // #version must be the first non-comment line of a GLSL translation unit,
    // so it has to live at the top of the main string we hand to glslang, not
    // inside the preamble served by the includer. Split the first line off
    // the preamble; everything after it becomes the preamble body.
    std::string version_line;
    std::string preamble_body;
    {
        const std::size_t nl = preamble.find('\n');
        if (nl == std::string::npos) {
            version_line = preamble;
            preamble.clear();
        } else {
            version_line = preamble.substr(0, nl + 1);
            preamble_body = preamble.substr(nl + 1);
        }
    }

    static constexpr const char* c_preamble_include_name = "erhe_preamble.glsl";

    // Read the top-level shader file raw — do NOT pre-expand #includes. The
    // glslang Includer resolves them during parse so each one becomes its own
    // named source string (and thus its own DebugSource with Text).
    std::string main_source;
    std::filesystem::path main_path;
    std::filesystem::path primary_dir;
    if (!shader.paths.empty()) {
        main_path = shader.paths.front();
        // Resolve against create_info.extra_include_paths when the path
        // does not exist as-is. Matches the fallback order in
        // Glsl_file_loader::read_shader_source_file so callers that pass
        // a bare filename (e.g. Tile_renderer with "tile.vert") resolve
        // the same way on the glslang path as on the legacy load path.
        std::error_code error_code;
        if (!std::filesystem::exists(main_path, error_code)) {
            for (const std::filesystem::path& extra : create_info.extra_include_paths) {
                const std::filesystem::path candidate = extra / main_path.filename();
                if (std::filesystem::exists(candidate, error_code)) {
                    main_path = candidate;
                    break;
                }
            }
        }
        primary_dir = main_path.parent_path();
        auto file_source = erhe::file::read("Glslang_shader_stages::compile_shader", main_path);
        if (file_source.has_value()) {
            main_source = std::move(file_source.value());
        } else {
            m_last_compile_log = fmt::format("Failed to read shader source file {}", main_path.string());
            log_glsl->error("{}", m_last_compile_log);
            return false;
        }
    } else if (!shader.source.empty()) {
        main_source = shader.source;
    }

    // Track the top-level file as a monitor dependency.
    if (!main_path.empty()) {
        std::vector<std::filesystem::path>& deps = m_shader_stages_prototype.get_dependency_paths();
        const auto i = std::find(deps.begin(), deps.end(), main_path);
        if (i == deps.end()) {
            deps.push_back(main_path);
        }
    }

    std::string main_with_preamble;
    main_with_preamble.reserve(version_line.size() + main_source.size() + 128);
    main_with_preamble += version_line;
    main_with_preamble += "#extension GL_GOOGLE_include_directive : require\n";
    main_with_preamble += "#include \"";
    main_with_preamble += c_preamble_include_name;
    main_with_preamble += "\"\n";
    main_with_preamble += main_source;

    const std::string main_name = !main_path.empty() ? main_path.generic_string() : create_info.name;

    m_glslang_shaders[language].reset();
    m_glslang_shaders[language] = std::make_shared<glslang::TShader>(language);
    glslang::TShader& glslang_shader = *m_glslang_shaders[language].get();

    const char* const source_string = main_with_preamble.data();
    const int         source_length = static_cast<int>(main_with_preamble.size());
    const char* const source_name   = main_name.c_str();
    glslang_shader.setStringsWithLengthsAndNames(&source_string, &source_length, &source_name, 1);

#if defined(ERHE_GRAPHICS_API_METAL) || defined(ERHE_GRAPHICS_API_VULKAN)
    glslang_shader.setEnvInput(glslang::EShSource::EShSourceGlsl, language, glslang::EShClient::EShClientVulkan, 100);
    glslang_shader.setEnvClient(glslang::EShClient::EShClientVulkan, glslang::EshTargetClientVersion::EShTargetVulkan_1_1);
#else
    glslang_shader.setEnvInput(glslang::EShSource::EShSourceGlsl, language, glslang::EShClient::EShClientOpenGL, 100);
    glslang_shader.setEnvClient(glslang::EShClient::EShClientOpenGL, glslang::EshTargetClientVersion::EShTargetOpenGL_450);
#endif
    glslang_shader.setEnvTarget(glslang::EShTargetLanguage::EShTargetSpv, glslang::EShTargetSpv_1_6);

    unsigned int messages{
        EShMsgSpvRules             | // issue messages for SPIR-V generation
        EShMsgDebugInfo            | // save debug information
      //EShMsgBuiltinSymbolTable   | // print the builtin symbol table
        EShMsgEnhanced             | // enhanced message readability
        EShMsgDisplayErrorColumn     // Display error message column aswell as line
    };

    Glsl_includer includer{
        primary_dir,
        create_info.extra_include_paths,
        std::move(preamble_body),
        c_preamble_include_name,
        &m_shader_stages_prototype.get_dependency_paths()
    };

    const ::TBuiltInResource glslang_built_in_resources = get_built_in_resources(device);
    const bool parse_ok = glslang_shader.parse(
        &glslang_built_in_resources,
        100,
        true,
        static_cast<const EShMessages>(messages),
        includer
    );
    const char* info_log       = glslang_shader.getInfoLog();
    const char* info_debug_log = glslang_shader.getInfoDebugLog();
    if (!parse_ok) {
        const std::string formatted_source = format_source(main_with_preamble);
        log_glsl->error(
            "glslang parse error in shader = {}\n{}\n{}\n{}\n{}\n",
            main_name,
            info_log       != nullptr ? info_log       : "",
            info_debug_log != nullptr ? info_debug_log : "",
            formatted_source,
            info_log       != nullptr ? info_log       : ""
        );
        m_last_compile_log.clear();
        if ((info_log != nullptr) && (info_log[0] != '\0')) {
            m_last_compile_log += info_log;
        }
        if ((info_debug_log != nullptr) && (info_debug_log[0] != '\0')) {
            if (!m_last_compile_log.empty() && m_last_compile_log.back() != '\n') {
                m_last_compile_log += '\n';
            }
            m_last_compile_log += info_debug_log;
        }
        if (m_last_compile_log.empty()) {
            m_last_compile_log = fmt::format(
                "glslang parse returned false but produced no info log for shader = {}",
                main_name
            );
        }
        return false;
    } else if ((info_log != nullptr) && (info_log[0] != '\0')) {
        log_glsl->info("\n{}\n", info_log);
    }

    return true;
}

auto Glslang_shader_stages::get_last_compile_log() const -> const std::string&
{
    return m_last_compile_log;
}

auto Glslang_shader_stages::get_last_link_log() const -> const std::string&
{
    return m_last_link_log;
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
    messages = messages | EShMsgSpvRules;             // issue messages for SPIR-V generation
    messages = messages | EShMsgDebugInfo;            // save debug information
    //messages = messages | EShMsgBuiltinSymbolTable; // print the builtin symbol table
    messages = messages | EShMsgEnhanced;             // enhanced message readability
    messages = messages | EShMsgAbsolutePath;         // Output Absolute path for messages
    messages = messages | EShMsgDisplayErrorColumn;   // Display error message column aswell as line

    const bool link_ok = m_glslang_program->link(static_cast<const EShMessages>(messages));
    const char* const info_log = m_glslang_program->getInfoLog();
    if ((info_log != nullptr) && info_log[0] != '\0') {
        log_program->info("glslang program link info log:\n{}\n", info_log);
    }
    if (!link_ok) {
        log_program->error("glslang program link failed");
        m_last_link_log = (info_log != nullptr) ? info_log : "";
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

    const Shader_stages_create_info& create_info = m_shader_stages_prototype.create_info();

    for (::EShLanguage stage : m_active_stages) {
        // Try cache first per-stage
        bool from_cache = false;
        if (m_cache != nullptr) {
            for (const Shader_stage& shader : create_info.shaders) {
                if (to_glslang(shader.type) == stage) {
                    const std::string source = m_shader_stages_prototype.get_final_source(shader, std::nullopt);
                    std::vector<unsigned int> cached = m_cache->get(source, shader.type);
                    if (!cached.empty()) {
                        m_spirv_shaders[stage] = std::move(cached);
                        from_cache = true;
                    }
                    break;
                }
            }
        }

        if (!from_cache) {
            spv::SpvBuildLogger       logger;
            auto*                     intermediate  = m_glslang_program->getIntermediate(stage);
            std::vector<unsigned int> spirv_binary;
            GlslangToSpv(*intermediate, spirv_binary, &logger, &spirv_options);
            const std::string spv_messages = logger.getAllMessages();
            if (!spv_messages.empty()) {
                log_glsl->info("SPIR_V messages::\n{}\n", spv_messages);
            }
            m_spirv_shaders[stage] = std::move(spirv_binary);

            // Store in cache
            if (m_cache != nullptr) {
                for (const Shader_stage& shader : create_info.shaders) {
                    if (to_glslang(shader.type) == stage) {
                        const std::string source = m_shader_stages_prototype.get_final_source(shader, std::nullopt);
                        m_cache->put(source, shader.type, m_spirv_shaders[stage]);
                        break;
                    }
                }
            }
        }

        if (create_info.dump_spirv_disassembly) {
            std::ostringstream disasm;
            spv::Disassemble(disasm, m_spirv_shaders[stage]);
            log_glsl->info(
                "SPIR-V disassembly for {} {} stage:\n{}\n",
                create_info.name,
                glslang_stage_name(stage),
                disasm.str()
            );
        }
    }

    return true;
}

auto Glslang_shader_stages::try_load_all_from_cache(Device& /*device*/) -> bool
{
    if (m_cache == nullptr) {
        return false;
    }

    const Shader_stages_create_info& create_info = m_shader_stages_prototype.create_info();
    std::unordered_map<::EShLanguage, std::vector<unsigned int>> cached_spirv;

    for (const Shader_stage& shader : create_info.shaders) {
        const std::string source   = m_shader_stages_prototype.get_final_source(shader, std::nullopt);
        const EShLanguage language = to_glslang(shader.type);

        std::vector<unsigned int> spirv = m_cache->get(source, shader.type);
        if (spirv.empty()) {
            return false; // Cache miss on any stage means we must compile all
        }
        cached_spirv[language] = std::move(spirv);
    }

    // All stages cached
    m_spirv_shaders = std::move(cached_spirv);
    return true;
}

auto Glslang_shader_stages::get_spirv_binary(Shader_type type) const -> std::span<const unsigned int>
{
    const EShLanguage language = to_glslang(type);
    const auto i = m_spirv_shaders.find(language);
    if (i == m_spirv_shaders.end()) {
        return {};
    }
    const unsigned int* const data = i->second.data();
    const std::size_t         size = i->second.size();
    return std::span<const unsigned int>(data, size);
}

} // namespace erhe::graphics
