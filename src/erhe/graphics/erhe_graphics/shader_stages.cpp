#include "erhe_graphics/shader_stages.hpp"

#include "glslang/Public/ShaderLang.h"

#include <igl/Device.h>

#include <fmt/format.h>

#include <cassert>

namespace erhe::graphics
{

using std::string;

auto Reloadable_shader_stages::make_prototype(igl::IDevice& device) -> Shader_stages_prototype
{
    erhe::graphics::Shader_stages_prototype prototype{device, create_info};
    return prototype;
}

Reloadable_shader_stages::Reloadable_shader_stages(
    igl::IDevice&                    device,
    const Shader_stages_create_info& create_info
)
    : create_info  {create_info}
    , shader_stages{make_prototype(device)}
{
}

Reloadable_shader_stages::Reloadable_shader_stages(
    Shader_stages_prototype&& prototype
)
    : create_info  {prototype.create_info()}
    , shader_stages{std::move(prototype)}
{
}

Reloadable_shader_stages::Reloadable_shader_stages(Reloadable_shader_stages&& other)
    : create_info  {std::move(other.create_info)}
    , shader_stages{std::move(other.shader_stages)}
{
}

Reloadable_shader_stages& Reloadable_shader_stages::operator=(Reloadable_shader_stages&& other)
{
    create_info   = std::move(other.create_info);
    shader_stages = std::move(other.shader_stages);
    return *this;
}

Shader_stage::Shader_stage(igl::ShaderStage type, const std::string_view source)
    : type  {type}
    , source{source}
{
}

Shader_stage::Shader_stage(igl::ShaderStage type, const std::filesystem::path path)
    : type{type}
    , path{path}
{
}

auto Shader_stages::name() const -> const std::string&
{
    return m_name;
}

auto Shader_stages::get() const -> std::shared_ptr<igl::IShaderStages>
{
    return m_shader_stages;
}

Shader_stages::Shader_stages(Shader_stages_prototype&& prototype)
{
    std::vector<std::shared_ptr<igl::IShaderModule>> modules;

    m_shader_stages = prototype.m_device.createShaderStages();
    m_is_valid = true;

    std::string label = fmt::format(
        "(P) {}{}",
        m_name,
        prototype.is_valid() ? "" : " (Failed)"
    );
}

auto Shader_stages::is_valid() const -> bool
{
    return m_is_valid;
}

void Shader_stages::invalidate()
{
    m_is_valid = false;
}

void Shader_stages::reload(Shader_stages_prototype&& prototype)
{
    if (!prototype.is_valid()) {
        invalidate();
        return;
    }

    m_shader_stages = std::move(prototype.m_shader_stages);
    m_is_valid = true;
}

} // namespace erhe::graphics
