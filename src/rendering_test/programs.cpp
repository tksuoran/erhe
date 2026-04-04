#include "programs.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_scene_renderer/program_interface.hpp"

namespace rendering_test {

Programs::Programs(erhe::graphics::Device& graphics_device, erhe::scene_renderer::Program_interface& program_interface)
    : shader_path{std::filesystem::path("res") / std::filesystem::path("shaders")}
    , default_uniform_block{graphics_device}
    , shadow_sampler_compare{
        graphics_device.get_info().uses_default_uniform_block()
            ? default_uniform_block.add_sampler(
                "s_shadow_compare",
                erhe::graphics::Glsl_type::sampler_2d_array_shadow,
                erhe::scene_renderer::c_texture_heap_slot_shadow_compare
            )
            : nullptr
    }
    , shadow_sampler_no_compare{
        graphics_device.get_info().uses_default_uniform_block()
            ? default_uniform_block.add_sampler(
                "s_shadow_no_compare",
                erhe::graphics::Glsl_type::sampler_2d_array,
                erhe::scene_renderer::c_texture_heap_slot_shadow_no_compare
            )
            : nullptr
    }
    , texture_sampler{
        graphics_device.get_info().uses_sampler_array_in_set_0()
            ? default_uniform_block.add_sampler(
                "s_texture",
                erhe::graphics::Glsl_type::sampler_2d,
                erhe::scene_renderer::c_texture_heap_slot_count_reserved,
                std::min(
                    graphics_device.get_info().max_per_stage_descriptor_samplers - erhe::scene_renderer::c_texture_heap_slot_count_reserved,
                    uint32_t{64}
                )
            )
            : nullptr
    }
    , nearest_sampler{
        graphics_device,
        erhe::graphics::Sampler_create_info{
            .min_filter  = erhe::graphics::Filter::nearest,
            .mag_filter  = erhe::graphics::Filter::nearest,
            .mipmap_mode = erhe::graphics::Sampler_mipmap_mode::nearest,
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
                    .default_uniform_block = graphics_device.get_info().uses_default_uniform_block()
                        ? &default_uniform_block
                        : nullptr,
                    .dump_interface    = false,
                    .dump_final_source = false
                }
            )
        )
    }
    , wide_lines{graphics_device, "wide_lines-not_loaded"}
{
    // wide_lines uses geometry shaders - only load on backends that support them
    if (!graphics_device.get_info().use_compute_shader) {
        auto prototype = program_interface.make_prototype(
            shader_path,
            erhe::graphics::Shader_stages_create_info{
                .name    = "wide_lines",
                .defines = {{"ERHE_USE_DRAW_COLOR", "1"}}
            }
        );
        if (prototype.is_valid()) {
            wide_lines = erhe::graphics::Shader_stages{graphics_device, std::move(prototype)};
        }
    }
}

} // namespace rendering_test
