#pragma once

#include "erhe/graphics/shader_resource.hpp"
#include "erhe/graphics/shader_stages.hpp"
#include "erhe/graphics/sampler.hpp"

#include <filesystem>
#include <memory>

namespace erhe::graphics
{
    class Instance;
    class Sampler;
    class Shader_resource;
    class Shader_stages;
}

namespace erhe::scene_renderer
{
    class Program_interface;
}

namespace example {

enum class Shader_stages_variant : int
{
    standard
};

static constexpr const char* c_shader_stages_variant_strings[] =
{
    "Standard"
};

class Programs
{
public:
    Programs(
        erhe::graphics::Instance&                graphics_instance,
        erhe::scene_renderer::Program_interface& program_interface
    );

    static constexpr std::size_t s_texture_unit_count = 15; // one reserved for shadows

    // Public members
    int                              shadow_texture_unit{15};
    int                              base_texture_unit{0};
    std::filesystem::path            shader_path;
    erhe::graphics::Shader_resource  default_uniform_block;   // for non-bindless textures
    erhe::graphics::Shader_resource* shadow_sampler;
    erhe::graphics::Shader_resource* texture_sampler;
    erhe::graphics::Sampler          nearest_sampler;
    erhe::graphics::Sampler          linear_sampler;
    erhe::graphics::Sampler          linear_mipmap_linear_sampler;
    erhe::graphics::Shader_stages    standard;
};

}
