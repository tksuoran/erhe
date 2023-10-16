#include "programs.hpp"

#include "erhe_graphics/instance.hpp"
#include "erhe_scene_renderer/program_interface.hpp"

namespace example {

Programs::Programs(
    erhe::graphics::Instance&                graphics_instance,
    erhe::scene_renderer::Program_interface& program_interface
)
    : shader_path{
        std::filesystem::path("res") / std::filesystem::path("shaders")
    }
    , default_uniform_block{graphics_instance}
    , shadow_sampler{
        graphics_instance.info.use_bindless_texture
            ? nullptr
            : default_uniform_block.add_sampler(
                "s_shadow",
                igl::UniformType::sampler_2d_array,
                shadow_texture_unit
            )
    }
    , texture_sampler{
        graphics_instance.info.use_bindless_texture
            ? nullptr
            : default_uniform_block.add_sampler(
                "s_texture",
                igl::UniformType::sampler_2d,
                base_texture_unit,
                s_texture_unit_count
            )
    }

    , nearest_sampler{
        erhe::graphics::Sampler_create_info{
            .min_filter  = gl::Texture_min_filter::nearest,
            .mag_filter  = gl::Texture_mag_filter::nearest,
            .debug_label = "Programs nearest"
        }
    }
    , linear_sampler{
        erhe::graphics::Sampler_create_info{
            .min_filter  = gl::Texture_min_filter::linear,
            .mag_filter  = gl::Texture_mag_filter::linear,
            .debug_label = "Programs linear"
        }
    }
    , linear_mipmap_linear_sampler{
        erhe::graphics::Sampler_create_info{
            .min_filter  = gl::Texture_min_filter::linear_mipmap_linear,
            .mag_filter  = gl::Texture_mag_filter::linear,
            .debug_label = "Programs linear mipmap"
        }
    }

    , standard{
        program_interface.make_program(
            program_interface.make_prototype(
                graphics_instance,
                shader_path,
                erhe::graphics::Shader_stages_create_info{
                    .name              = "standard",
                    .default_uniform_block = graphics_instance.info.use_bindless_texture
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
