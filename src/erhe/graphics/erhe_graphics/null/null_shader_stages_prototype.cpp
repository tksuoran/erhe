#include "erhe_graphics/null/null_shader_stages.hpp"
#include "erhe_graphics/device.hpp"

namespace erhe::graphics {

Shader_stages_prototype_impl::Shader_stages_prototype_impl(Device& device, Shader_stages_create_info&& create_info)
    : m_device     {device}
    , m_create_info{std::move(create_info)}
    , m_is_valid   {true}
{
}

Shader_stages_prototype_impl::Shader_stages_prototype_impl(Device& device, const Shader_stages_create_info& create_info)
    : m_device     {device}
    , m_create_info{create_info}
    , m_is_valid   {true}
{
}

auto Shader_stages_prototype_impl::name() const -> const std::string&
{
    return m_create_info.name;
}

auto Shader_stages_prototype_impl::create_info() const -> const Shader_stages_create_info&
{
    return m_create_info;
}

auto Shader_stages_prototype_impl::is_valid() -> bool
{
    return m_is_valid;
}

void Shader_stages_prototype_impl::compile_shaders()
{
    // No-op in null backend
}

auto Shader_stages_prototype_impl::link_program() -> bool
{
    // No-op in null backend
    return true;
}

void Shader_stages_prototype_impl::dump_reflection() const
{
    // No-op in null backend
}

auto Shader_stages_prototype_impl::get_final_source(
    const Shader_stage&            shader,
    std::optional<unsigned int>    gl_name
) -> std::string
{
    static_cast<void>(shader);
    static_cast<void>(gl_name);
    return {};
}

} // namespace erhe::graphics
