// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/vulkan/vulkan_shader_stages.hpp"
#include "erhe_graphics/glsl_format_source.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>

namespace erhe::graphics {

Shader_stages_prototype_impl::Shader_stages_prototype_impl(Device& device, Shader_stages_create_info&& create_info)
    : m_device               {device}
    , m_create_info          {create_info}
    , m_default_uniform_block{device}
    , m_glslang_shader_stages{*this, &device.get_spirv_cache()}
{
    if (m_glslang_shader_stages.try_load_all_from_cache(device)) {
        m_state = Shader_build_state::ready;
    }
}

Shader_stages_prototype_impl::Shader_stages_prototype_impl(Device& device, const Shader_stages_create_info& create_info)
    : m_device               {device}
    , m_create_info          {create_info}
    , m_default_uniform_block{device}
    , m_glslang_shader_stages{*this, &device.get_spirv_cache()}
{
    if (m_glslang_shader_stages.try_load_all_from_cache(device)) {
        m_state = Shader_build_state::ready;
    }
}

void Shader_stages_prototype_impl::compile_shaders()
{
    ERHE_PROFILE_FUNCTION();

    if (m_state != Shader_build_state::init) {
        return; // Already compiled or beyond compile stage
    }
    for (const auto& shader : m_create_info.shaders) {
        if (shader.type == Shader_type::geometry_shader) {
            log_program->warn("Vulkan backend does not support geometry shaders: {}", m_create_info.name);
            m_state = Shader_build_state::fail;
            return;
        }
        if (!m_glslang_shader_stages.compile_shader(m_device, shader)) {
            const std::string& compile_log = m_glslang_shader_stages.get_last_compile_log();
            std::string error_msg = fmt::format(
                "GLSL compilation failed for shader: {}\n{}",
                m_create_info.name,
                compile_log
            );
            log_program->error("{}", error_msg);
            std::string source = m_create_info.final_source(m_device, shader, nullptr);
            m_device.shader_error(error_msg, source);
            m_state = Shader_build_state::fail;
            return;
        }
    }
    m_state = Shader_build_state::shader_compilation_started;
}

auto Shader_stages_prototype_impl::link_program() -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (m_state == Shader_build_state::fail) {
        return false;
    }

    if (m_state == Shader_build_state::init) {
        compile_shaders();
    }

    if (m_state == Shader_build_state::fail) {
        return false;
    }

    if (m_state == Shader_build_state::ready) {
        return true; // Already linked (cache hit)
    }

    ERHE_VERIFY(m_state == Shader_build_state::shader_compilation_started);

    if (!m_glslang_shader_stages.link_program(m_device)) {
        const std::string& link_log = m_glslang_shader_stages.get_last_link_log();
        std::string error_msg = fmt::format(
            "GLSL -> SPIR-V link failed for: {}\n{}",
            m_create_info.name,
            link_log
        );
        log_program->error("{}", error_msg);
        if (!m_create_info.shaders.empty()) {
            std::string source = m_create_info.final_source(m_device, m_create_info.shaders.front(), nullptr);
            m_device.shader_error(error_msg, source);
        }
        m_state = Shader_build_state::fail;
        return false;
    }

    m_state = Shader_build_state::ready;
    return true;
}

auto Shader_stages_prototype_impl::get_final_source(
    const Shader_stage&         shader,
    std::optional<unsigned int> gl_name
) -> std::string
{
    return m_create_info.final_source(m_device, shader, &m_paths, gl_name);
}

auto Shader_stages_prototype_impl::get_dependency_paths() -> std::vector<std::filesystem::path>&
{
    return m_paths;
}

auto Shader_stages_prototype_impl::is_valid() -> bool
{
    return m_state == Shader_build_state::ready;
}

auto Shader_stages_prototype_impl::get_spirv_binary(Shader_type type) const -> std::span<const unsigned int>
{
    return m_glslang_shader_stages.get_spirv_binary(type);
}

auto Shader_stages_prototype_impl::create_info() const -> const Shader_stages_create_info&
{
    return m_create_info;
}

auto Shader_stages_prototype_impl::name() const -> const std::string&
{
    return m_create_info.name;
}

} // namespace erhe::graphics
