#pragma once

#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/sampler.hpp"

#include <filesystem>
#include <memory>

namespace erhe::graphics{
    class Device;
    class Sampler;
    class Shader_resource;
    class Shader_stages;
}

namespace erhe::scene_renderer{
    class Program_interface;
}

namespace example {

enum class Shader_stages_variant : int {
    standard
};

static constexpr const char* c_shader_stages_variant_strings[] = {
    "Standard"
};

class Programs
{
public:
    Programs(
        erhe::graphics::Device&                  graphics_device,
        erhe::scene_renderer::Program_interface& program_interface
    );

    static constexpr int         s_texture_unit_base  =  2; // First two are reserved for shadow samplers
    static constexpr std::size_t s_texture_unit_count = 14; // For non bindless textures

    // Public members
    int                              shadow_texture_unit_compare{0};
    int                              shadow_texture_unit_no_compare{1};
    std::filesystem::path            shader_path;
    erhe::graphics::Shader_resource  default_uniform_block;   // for non-bindless textures
    erhe::graphics::Shader_resource* shadow_sampler_compare;
    erhe::graphics::Shader_resource* shadow_sampler_no_compare;
    erhe::graphics::Shader_resource* texture_sampler;
    erhe::graphics::Sampler          nearest_sampler;
    erhe::graphics::Sampler          linear_sampler;
    erhe::graphics::Sampler          linear_mipmap_linear_sampler;
    erhe::graphics::Shader_stages    standard;
};

}
