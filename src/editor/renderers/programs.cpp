#include "renderers/programs.hpp"
#include "renderers/mesh_memory.hpp"

#include "editor_log.hpp"

#include "erhe/configuration/configuration.hpp"
#include "erhe/gl/command_info.hpp"
#include "erhe/graphics/shader_monitor.hpp"
#include "erhe/graphics/instance.hpp"
#include "erhe/scene_renderer/camera_buffer.hpp"
#include "erhe/scene_renderer/light_buffer.hpp"
#include "erhe/scene_renderer/material_buffer.hpp"
#include "erhe/scene_renderer/primitive_buffer.hpp"
#include "erhe/scene_renderer/program_interface.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

namespace editor {

Programs::Shader_stages_builder::Shader_stages_builder(
    erhe::graphics::Shader_stages&              shader_stages,
    erhe::graphics::Instance&                   graphics_instance,
    erhe::scene_renderer::Program_interface&    program_interface,
    std::filesystem::path                       shader_path,
    erhe::graphics::Shader_stages_create_info&& create_info
)
    : shader_stages{shader_stages}
    , prototype{
        program_interface.make_prototype(
            graphics_instance,
            shader_path,
            std::move(create_info)
        )
    }
{
}

Programs::Shader_stages_builder::Shader_stages_builder(Shader_stages_builder&& other) = default;


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
                gl::Uniform_type::sampler_2d_array,
                shadow_texture_unit
            )
    }
    , texture_sampler{
        graphics_instance.info.use_bindless_texture
            ? nullptr
            : default_uniform_block.add_sampler(
                "s_texture",
                gl::Uniform_type::sampler_2d,
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
            .min_filter = gl::Texture_min_filter::linear,
            .mag_filter = gl::Texture_mag_filter::linear,
            .debug_label = "Programs linear"
        }
    }
    , linear_mipmap_linear_sampler{
        erhe::graphics::Sampler_create_info{
            .min_filter = gl::Texture_min_filter::linear_mipmap_linear,
            .mag_filter = gl::Texture_mag_filter::linear,
            .debug_label = "Programs linear mipmap"
        }
    }
    , brdf_slice              {"brdf_slice-not_loaded"}
    , brush                   {"brush-not_loaded"}
    , standard                {"standard-not_loaded"}
    , anisotropic_slope       {"anisotropic_slope-not_loaded"}
    , anisotropic_engine_ready{"anisotropic_engine_ready-not_loaded"}
    , circular_brushed_metal  {"circular_brushed_metal-not_loaded"}
    , textured                {"textured-not_loaded"}
    , sky                     {"sky-not_loaded"}
    , wide_lines_draw_color   {"wide_lines_draw_color-not_loaded"}
    , wide_lines_vertex_color {"wide_lines_vertex_color-not_loaded"}
    , points                  {"points-not_loaded"}
    , depth                   {"depth-not_loaded"}
    , id                      {"id-not_loaded"}
    , tool                    {"tool-not_loaded"}
    , debug_depth             {"debug_depth-not_loaded"}
    , debug_normal            {"debug_normal-not_loaded"}
    , debug_tangent           {"debug_tangent-not_loaded"}
    , debug_bitangent         {"debug_bitangent-not_loaded"}
    , debug_texcoord          {"debug_texcoord-not_loaded"}
    , debug_vertex_color_rgb  {"debug_vertex_color_rgb-not_loaded"}
    , debug_vertex_color_alpha{"debug_vertex_color_alpha-not_loaded"}
    , debug_omega_o           {"debug_omega_o-not_loaded"}
    , debug_omega_i           {"debug_omega_i-not_loaded"}
    , debug_omega_g           {"debug_omega_g-not_loaded"}
    , debug_misc              {"debug_misc-not_loaded"}

{

    // Not available on Dell laptop.
    //standard      = make_program("standard", {}, {{gl::Shader_type::fragment_shader, "GL_NV_fragment_shader_barycentric"}});

    std::vector<Shader_stages_builder> prototypes;

    using CI = erhe::graphics::Shader_stages_create_info;

    auto add_shader = [
        this,
        &graphics_instance,
        &program_interface,
        &prototypes
    ](
        erhe::graphics::Shader_stages&              shader_stages,
        erhe::graphics::Shader_stages_create_info&& create_info
    )
    {
        prototypes.emplace_back(
            shader_stages,
            graphics_instance,
            program_interface,
            shader_path,
            std::move(create_info)
        );
    };

    add_shader(standard                , CI{ .instance = graphics_instance, .name = "standard"                , .default_uniform_block = &default_uniform_block, .dump_interface = true } );
    add_shader(anisotropic_slope       , CI{ .instance = graphics_instance, .name = "anisotropic_slope"       , .default_uniform_block = &default_uniform_block } );
    add_shader(anisotropic_engine_ready, CI{ .instance = graphics_instance, .name = "anisotropic_engine_ready", .default_uniform_block = &default_uniform_block } );
    add_shader(circular_brushed_metal  , CI{ .instance = graphics_instance, .name = "circular_brushed_metal"  , .default_uniform_block = &default_uniform_block } );
    add_shader(brdf_slice              , CI{ .instance = graphics_instance, .name = "brdf_slice"              , .default_uniform_block = &default_uniform_block } );
    add_shader(brush                   , CI{ .instance = graphics_instance, .name = "brush"                   , .default_uniform_block = &default_uniform_block } );
    add_shader(textured                , CI{ .instance = graphics_instance, .name = "textured"                , .default_uniform_block = &default_uniform_block } );
    add_shader(sky                     , CI{ .instance = graphics_instance, .name = "sky"                     , .default_uniform_block = &default_uniform_block } );
    add_shader(wide_lines_draw_color   , CI{ .instance = graphics_instance, .name = "wide_lines"              , .defines = { std::pair<std::string, std::string>{"ERHE_USE_DRAW_COLOR",   "1"}}});
    add_shader(wide_lines_vertex_color , CI{ .instance = graphics_instance, .name = "wide_lines"              , .defines = { std::pair<std::string, std::string>{"ERHE_USE_VERTEX_COLOR", "1"}}});
    add_shader(points                  , CI{ .instance = graphics_instance, .name = "points" } );
    add_shader(depth                   , CI{ .instance = graphics_instance, .name = "depth"  } );
    add_shader(id                      , CI{ .instance = graphics_instance, .name = "id"     } );
    add_shader(tool                    , CI{ .instance = graphics_instance, .name = "tool"   } );
    add_shader(debug_depth             , CI{ .instance = graphics_instance, .name = "visualize_depth", .default_uniform_block = &default_uniform_block } );
    add_shader(debug_normal            , CI{ .instance = graphics_instance, .name = "standard_debug", .defines = { std::pair<std::string, std::string>{"ERHE_DEBUG_NORMAL",             "1"}}, .default_uniform_block = &default_uniform_block } );
    add_shader(debug_tangent           , CI{ .instance = graphics_instance, .name = "standard_debug", .defines = { std::pair<std::string, std::string>{"ERHE_DEBUG_TANGENT",            "1"}}, .default_uniform_block = &default_uniform_block } );
    add_shader(debug_bitangent         , CI{ .instance = graphics_instance, .name = "standard_debug", .defines = { std::pair<std::string, std::string>{"ERHE_DEBUG_BITANGENT",          "1"}}, .default_uniform_block = &default_uniform_block } );
    add_shader(debug_texcoord          , CI{ .instance = graphics_instance, .name = "standard_debug", .defines = { std::pair<std::string, std::string>{"ERHE_DEBUG_TEXCOORD",           "1"}}, .default_uniform_block = &default_uniform_block } );
    add_shader(debug_vertex_color_rgb  , CI{ .instance = graphics_instance, .name = "standard_debug", .defines = { std::pair<std::string, std::string>{"ERHE_DEBUG_VERTEX_COLOR_RGB",   "1"}}, .default_uniform_block = &default_uniform_block } );
    add_shader(debug_vertex_color_alpha, CI{ .instance = graphics_instance, .name = "standard_debug", .defines = { std::pair<std::string, std::string>{"ERHE_DEBUG_VERTEX_COLOR_ALPHA", "1"}}, .default_uniform_block = &default_uniform_block } );
    add_shader(debug_omega_o           , CI{ .instance = graphics_instance, .name = "standard_debug", .defines = { std::pair<std::string, std::string>{"ERHE_DEBUG_OMEGA_O",            "1"}}, .default_uniform_block = &default_uniform_block } );
    add_shader(debug_omega_i           , CI{ .instance = graphics_instance, .name = "standard_debug", .defines = { std::pair<std::string, std::string>{"ERHE_DEBUG_OMEGA_I",            "1"}}, .default_uniform_block = &default_uniform_block } );
    add_shader(debug_omega_g           , CI{ .instance = graphics_instance, .name = "standard_debug", .defines = { std::pair<std::string, std::string>{"ERHE_DEBUG_OMEGA_G",            "1"}}, .default_uniform_block = &default_uniform_block } );
    add_shader(debug_misc              , CI{ .instance = graphics_instance, .name = "standard_debug", .defines = { std::pair<std::string, std::string>{"ERHE_DEBUG_MISC",               "1"}}, .default_uniform_block = &default_uniform_block } );

    // Compile shaders
    {
        ERHE_PROFILE_SCOPE("compile shaders");

        for (auto& entry : prototypes) {
            entry.prototype.compile_shaders();
        }
    }

    // Link programs
    {
        ERHE_PROFILE_SCOPE("link programs");

        for (auto& entry : prototypes) {
            entry.prototype.link_program();
        }
    }

    {
        ERHE_PROFILE_SCOPE("post link");

        for (auto& entry : prototypes) {
            entry.shader_stages.reload(std::move(entry.prototype));
        }
    }
}

}
