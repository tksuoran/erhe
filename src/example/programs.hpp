#pragma once

#include "erhe_graphics/sampler.hpp"
#include "erhe_scene_renderer/variant_handle.hpp"

#include <filesystem>

namespace erhe::graphics{
    class Device;
}

namespace erhe::scene_renderer{
    class Program_interface;
    class Shader_variant_cache;
}

namespace example {

class Programs
{
public:
    Programs(
        erhe::graphics::Device&                     graphics_device,
        erhe::scene_renderer::Shader_variant_cache& shader_variant_cache
    );

    // Samplers
    erhe::scene_renderer::Shader_variant_cache& shader_variant_cache;
    erhe::graphics::Sampler nearest_sampler;
    erhe::graphics::Sampler linear_sampler;
    erhe::graphics::Sampler linear_mipmap_linear_sampler;
};

}
