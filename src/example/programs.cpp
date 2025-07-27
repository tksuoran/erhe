#include "programs.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_scene_renderer/program_interface.hpp"

namespace example {

Programs::Programs(erhe::graphics::Device& graphics_device, erhe::scene_renderer::Program_interface& program_interface)
    : shader_path{std::filesystem::path("res") / std::filesystem::path("shaders")}
    , default_uniform_block{graphics_device}
    , shadow_sampler_compare{
        graphics_device.get_info().use_bindless_texture
            ? nullptr
            : default_uniform_block.add_sampler("s_shadow_compare", erhe::graphics::Glsl_type::sampler_2d_array_shadow, shadow_texture_unit_compare)
    }
    , shadow_sampler_no_compare{
        graphics_device.get_info().use_bindless_texture
            ? nullptr
            : default_uniform_block.add_sampler("s_shadow_no_compare", erhe::graphics::Glsl_type::sampler_2d_array, shadow_texture_unit_no_compare)
    }
    , texture_sampler{
        graphics_device.get_info().use_bindless_texture
            ? nullptr
            : default_uniform_block.add_sampler(
                "s_texture",
                erhe::graphics::Glsl_type::sampler_2d,
                0,
                graphics_device.get_info().max_texture_image_units
            )
    }

    , nearest_sampler{
        graphics_device,
        erhe::graphics::Sampler_create_info{
            .min_filter  = erhe::graphics::Filter::nearest,
            .mag_filter  = erhe::graphics::Filter::nearest,
            .mipmap_mode = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
            .debug_label = "Programs nearest"
        }
    }
    , linear_sampler{
        graphics_device,
        erhe::graphics::Sampler_create_info{
            .min_filter  = erhe::graphics::Filter::linear,
            .mag_filter  = erhe::graphics::Filter::linear,
            .mipmap_mode = erhe::graphics::Sampler_mipmap_mode::nearest,
            .debug_label = "Programs linear"
        }
    }
    , linear_mipmap_linear_sampler{
        graphics_device,
        erhe::graphics::Sampler_create_info{
            .min_filter  = erhe::graphics::Filter::linear,
            .mag_filter  = erhe::graphics::Filter::linear,
            .mipmap_mode = erhe::graphics::Sampler_mipmap_mode::linear,
            .debug_label = "Programs linear mipmap"
        }
    }

    , standard{
        program_interface.make_program(
            program_interface.make_prototype(
                shader_path,
                erhe::graphics::Shader_stages_create_info{
                    .name                  = "standard",
                    .default_uniform_block = graphics_device.get_info().use_bindless_texture
                        ? nullptr
                        : &default_uniform_block,
                    .dump_interface    = true,
                    .dump_final_source = true
                }
            )
        )
    }
{
}

}
