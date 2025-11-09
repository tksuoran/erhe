// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/gl/gl_shader_stages.hpp"
#include "erhe_graphics/glsl_format_source.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/gl/gl_debug.hpp"
#include "erhe_graphics/gl/gl_helpers.hpp"
#include "erhe_gl/enum_string_functions.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>

namespace erhe::graphics {

Shader_stages_prototype_impl::Shader_stages_prototype_impl(Device& device, Shader_stages_create_info&& create_info)
    : m_device               {device}
    , m_create_info          {create_info}
    , m_default_uniform_block{device}
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(m_handle.gl_name() != 0);
    if (create_info.build) {
        post_link();
    }
}

Shader_stages_prototype_impl::Shader_stages_prototype_impl(Device& device, const Shader_stages_create_info& create_info)
    : m_device               {device}
    , m_create_info          {create_info}
    , m_default_uniform_block{device}
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(m_handle.gl_name() != 0);
    if (create_info.build) {
        post_link();
    }
}

void Shader_stages_prototype_impl::compile_shaders()
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(m_state == state_init);
    for (const auto& shader : m_create_info.shaders) {
        m_glslang_shaders.push_back(compile_glslang(shader));
        if (m_state == state_fail) {
            break;
        }
    }
}

auto Shader_stages_prototype_impl::link_program() -> bool
{
    ERHE_PROFILE_FUNCTION();

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

    link_glslang_program();
    return true;
}

void Shader_stages_prototype_impl::post_link()
{
}

auto Shader_stages_prototype_impl::is_valid() -> bool
{
    if ((m_state != state_ready) && (m_state != state_fail)) {
        post_link();
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
            (digit == '0') || (digit == '1')
        )
    ) {
        return false;
    }

    return true;
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
