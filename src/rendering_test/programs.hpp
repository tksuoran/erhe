#pragma once

#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/sampler.hpp"

#include <filesystem>

namespace erhe::graphics {
    class Device;
}

namespace erhe::scene_renderer {
    class Program_interface;
}

namespace rendering_test {

class Programs
{
public:
    Programs(
        erhe::graphics::Device&                  graphics_device,
        erhe::scene_renderer::Program_interface& program_interface
    );

    // Public members
    std::filesystem::path            shader_path;
    erhe::graphics::Shader_resource  default_uniform_block;
    erhe::graphics::Shader_resource* shadow_sampler_compare;
    erhe::graphics::Shader_resource* shadow_sampler_no_compare;
    erhe::graphics::Shader_resource* texture_sampler;
    erhe::graphics::Sampler          nearest_sampler;
    erhe::graphics::Sampler          linear_sampler;
    erhe::graphics::Sampler          linear_mipmap_linear_sampler;
    erhe::graphics::Shader_stages    standard;
    erhe::graphics::Shader_stages    wide_lines;
};

} // namespace rendering_test
