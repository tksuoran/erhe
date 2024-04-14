#include "erhe_graphics/shader_stages.hpp"

#include "glslang/Public/ShaderLang.h"

#include <igl/Device.h>

#include <fmt/format.h>

#include <cassert>

namespace erhe::graphics
{

using std::string;

Shader_stage_create_info::Shader_stage_create_info(igl::ShaderStage stage, const std::string_view source)
    : stage {stage}
    , source{source}
{
}

Shader_stage_create_info::Shader_stage_create_info(igl::ShaderStage stage, const std::filesystem::path path)
    : stage{stage}
    , path {path}
{
}

auto Shader_stages::name() const -> const std::string&
{
    return m_name;
}

auto Shader_stages::get() const -> const igl::IShaderStages*
{
    return m_shader_stages.get();
}

Shader_stages::Shader_stages(Shader_stages_prototype&& prototype)
{
    m_shader_stages = std::move(prototype.m_shader_stages);
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
