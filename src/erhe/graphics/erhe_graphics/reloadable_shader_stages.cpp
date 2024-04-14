#include "erhe_graphics/reloadable_shader_stages.hpp"

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

} // namespace erhe::graphics
