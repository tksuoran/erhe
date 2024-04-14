#pragma once

#include "erhe_graphics/shader_stages.hpp"

namespace erhe::graphics
{

class Reloadable_shader_stages
{
public:
    explicit Reloadable_shader_stages(
        Shader_stages_prototype&& prototype
    );
    Reloadable_shader_stages(
        igl::IDevice&                    device,
        const Shader_stages_create_info& create_info
    );
    Reloadable_shader_stages(Reloadable_shader_stages&&);
    Reloadable_shader_stages& operator=(Reloadable_shader_stages&&);

    Shader_stages_create_info create_info;
    Shader_stages             shader_stages;

private:
    [[nodiscard]] auto make_prototype(igl::IDevice& device) -> Shader_stages_prototype;
};

} // namespace erhe::graphics
