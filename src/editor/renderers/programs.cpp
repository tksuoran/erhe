#include "renderers/programs.hpp"

#include "erhe_graphics/shader_monitor.hpp"
#include "erhe_graphics/instance.hpp"
#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_profile/profile.hpp"

#include <taskflow/taskflow.hpp>

namespace editor {

Programs::Shader_stages_builder::Shader_stages_builder(
    erhe::graphics::Reloadable_shader_stages& reloadable_shader_stages,
    erhe::graphics::Instance&                 graphics_instance,
    erhe::scene_renderer::Program_interface&  program_interface,
    std::filesystem::path                     shader_path
)
    : reloadable_shader_stages{reloadable_shader_stages}
    , prototype{
        program_interface.make_prototype(
            graphics_instance,
            shader_path,
            reloadable_shader_stages.create_info
        )
    }
{
}

Programs::Shader_stages_builder::Shader_stages_builder(Shader_stages_builder&& other)
    : reloadable_shader_stages{other.reloadable_shader_stages}
    , prototype               {std::move(other.prototype)}
{
}

Programs::Programs(erhe::graphics::Instance& graphics_instance)
    : shader_path{std::filesystem::path("res") / std::filesystem::path("shaders")}
    , default_uniform_block{graphics_instance}
    , shadow_sampler{
        graphics_instance.info.use_bindless_texture
            ? nullptr
            : default_uniform_block.add_sampler("s_shadow", gl::Uniform_type::sampler_2d_array, shadow_texture_unit)
    }
    , texture_sampler{
        graphics_instance.info.use_bindless_texture
            ? nullptr
            : default_uniform_block.add_sampler("s_texture", gl::Uniform_type::sampler_2d, base_texture_unit, s_texture_unit_count)
    }
    , nearest_sampler{
        erhe::graphics::Sampler_create_info{
            .min_filter  = gl::Texture_min_filter::nearest_mipmap_nearest,
            .mag_filter  = gl::Texture_mag_filter::nearest,
            .debug_label = "Programs nearest"
        }
    }
    , linear_sampler{
        erhe::graphics::Sampler_create_info{
            .min_filter = gl::Texture_min_filter::linear_mipmap_nearest,
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
    , error                   {"error-not_loaded"}
    , brdf_slice              {"brdf_slice-not_loaded"}
    , brush                   {"brush-not_loaded"}
    , standard                {"standard-not_loaded"}
    , anisotropic_slope       {"anisotropic_slope-not_loaded"}
    , anisotropic_engine_ready{"anisotropic_engine_ready-not_loaded"}
    , circular_brushed_metal  {"circular_brushed_metal-not_loaded"}
    , textured                {"textured-not_loaded"}
    , sky                     {"sky-not_loaded"}
    , grid                    {"grid-not_loaded"}
    , fat_triangle            {"fat_triangle-not_loaded"}
    , wide_lines_draw_color   {"wide_lines_draw_color-not_loaded"}
    , wide_lines_vertex_color {"wide_lines_vertex_color-not_loaded"}
    , points                  {"points-not_loaded"}
    , depth                   {"depth-not_loaded"}
    , id                      {"id-not_loaded"}
    , tool                    {"tool-not_loaded"}
    , debug_depth             {"debug_depth-not_loaded"}
    , debug_normal            {"debug_normal-not_loaded"}
    , debug_tangent           {"debug_tangent-not_loaded"}
    , debug_vertex_tangent_w  {"debug_vertex_tangent_w-not_loaded"}
    , debug_bitangent         {"debug_bitangent-not_loaded"}
    , debug_texcoord          {"debug_texcoord-not_loaded"}
    , debug_base_color_texture{"debug_base_color_texture-not_loaded"}
    , debug_vertex_color_rgb  {"debug_vertex_color_rgb-not_loaded"}
    , debug_vertex_color_alpha{"debug_vertex_color_alpha-not_loaded"}
    , debug_aniso_strength    {"debug_aniso_strength-not_loaded"}
    , debug_aniso_texcoord    {"debug_aniso_texcoord-not_loaded"}
    , debug_vdotn             {"debug_v_dot_n-not_loaded"}
    , debug_ldotn             {"debug_v_dot_n-not_loaded"}
    , debug_hdotv             {"debug_h_dot_v-not_loaded"}
    , debug_joint_indices     {"debug_joint_indices-not_loaded"}
    , debug_joint_weights     {"debug_joint_weights-not_loaded"}
    , debug_omega_o           {"debug_omega_o-not_loaded"}
    , debug_omega_i           {"debug_omega_i-not_loaded"}
    , debug_omega_g           {"debug_omega_g-not_loaded"}
    , debug_vertex_valency    {"debug_vertex_valency-not_loaded"}
    , debug_polygon_edge_count{"debug_polygon_edge_count_g-not_loaded"}
    , debug_misc              {"debug_misc-not_loaded"}
{
}

void Programs::load_programs(
    tf::Executor&                            executor,
    erhe::graphics::Instance&                graphics_instance,
    erhe::scene_renderer::Program_interface& program_interface
)
{
    // Not available on Dell laptop.
    //standard      = make_program("standard", {}, {{gl::Shader_type::fragment_shader, "GL_NV_fragment_shader_barycentric"}});

    std::vector<Shader_stages_builder> prototypes;

    using CI = erhe::graphics::Shader_stages_create_info;

    auto add_shader = [this, &graphics_instance, &program_interface, &prototypes](
        erhe::graphics::Reloadable_shader_stages&   reloadable_shader_stages,
        erhe::graphics::Shader_stages_create_info&& create_info
    ) {
        reloadable_shader_stages.create_info = std::move(create_info);
        prototypes.emplace_back(reloadable_shader_stages, graphics_instance, program_interface, shader_path);
    };

    add_shader(error                   , CI{ .name = "error"                   , .default_uniform_block = &default_uniform_block, .dump_interface = true } );
    add_shader(standard                , CI{ .name = "standard"                , .default_uniform_block = &default_uniform_block } );
    add_shader(anisotropic_slope       , CI{ .name = "anisotropic_slope"       , .default_uniform_block = &default_uniform_block } );
    add_shader(anisotropic_engine_ready, CI{ .name = "anisotropic_engine_ready", .default_uniform_block = &default_uniform_block } );
    add_shader(circular_brushed_metal  , CI{ .name = "circular_brushed_metal"  , .default_uniform_block = &default_uniform_block } );
    add_shader(brdf_slice              , CI{ .name = "brdf_slice"              , .default_uniform_block = &default_uniform_block } );
    add_shader(brush                   , CI{ .name = "brush"                   , .default_uniform_block = &default_uniform_block } );
    add_shader(textured                , CI{ .name = "textured"                , .default_uniform_block = &default_uniform_block } );
    add_shader(sky                     , CI{ .name = "sky"                     , .default_uniform_block = &default_uniform_block } );
    add_shader(grid                    , CI{ .name = "grid"                    , .default_uniform_block = &default_uniform_block } );
    add_shader(fat_triangle            , CI{ .name = "fat_triangle"            , .defines = {
        { "ERHE_LINE_SHADER_SHOW_DEBUG_LINES",        "0"},
        { "ERHE_LINE_SHADER_PASSTHROUGH_BASIC_LINES", "0"},
        { "ERHE_LINE_SHADER_STRIP",                   "1"}}});
    add_shader(wide_lines_draw_color   , CI{ .name = "wide_lines"              , .defines = {{"ERHE_USE_DRAW_COLOR",   "1"}}});
    add_shader(wide_lines_vertex_color , CI{ .name = "wide_lines"              , .defines = {{"ERHE_USE_VERTEX_COLOR", "1"}}});
    add_shader(points                  , CI{ .name = "points" } );
    add_shader(id                      , CI{ .name = "id"     } );
    add_shader(tool                    , CI{ .name = "tool"   } );
    add_shader(debug_depth             , CI{ .name = "visualize_depth", .default_uniform_block = &default_uniform_block } );
    add_shader(debug_normal            , CI{ .name = "standard_debug", .defines = {{"ERHE_DEBUG_NORMAL",             "1"}}, .default_uniform_block = &default_uniform_block } );
    add_shader(debug_tangent           , CI{ .name = "standard_debug", .defines = {{"ERHE_DEBUG_TANGENT",            "1"}}, .default_uniform_block = &default_uniform_block } );
    add_shader(debug_vertex_tangent_w  , CI{ .name = "standard_debug", .defines = {{"ERHE_DEBUG_TANGENT_W",          "1"}}, .default_uniform_block = &default_uniform_block } );
    add_shader(debug_bitangent         , CI{ .name = "standard_debug", .defines = {{"ERHE_DEBUG_BITANGENT",          "1"}}, .default_uniform_block = &default_uniform_block } );
    add_shader(debug_texcoord          , CI{ .name = "standard_debug", .defines = {{"ERHE_DEBUG_TEXCOORD",           "1"}}, .default_uniform_block = &default_uniform_block } );
    add_shader(debug_base_color_texture, CI{ .name = "standard_debug", .defines = {{"ERHE_DEBUG_BASE_COLOR_TEXTURE", "1"}}, .default_uniform_block = &default_uniform_block } );
    add_shader(debug_vertex_color_rgb  , CI{ .name = "standard_debug", .defines = {{"ERHE_DEBUG_VERTEX_COLOR_RGB",   "1"}}, .default_uniform_block = &default_uniform_block } );
    add_shader(debug_vertex_color_alpha, CI{ .name = "standard_debug", .defines = {{"ERHE_DEBUG_VERTEX_COLOR_ALPHA", "1"}}, .default_uniform_block = &default_uniform_block } );
    add_shader(debug_aniso_strength    , CI{ .name = "standard_debug", .defines = {{"ERHE_DEBUG_ANISO_STRENGTH",     "1"}}, .default_uniform_block = &default_uniform_block } );
    add_shader(debug_aniso_texcoord    , CI{ .name = "standard_debug", .defines = {{"ERHE_DEBUG_ANISO_TEXCOORD",     "1"}}, .default_uniform_block = &default_uniform_block } );
    add_shader(debug_vdotn             , CI{ .name = "standard_debug", .defines = {{"ERHE_DEBUG_VDOTN",              "1"}}, .default_uniform_block = &default_uniform_block } );
    add_shader(debug_ldotn             , CI{ .name = "standard_debug", .defines = {{"ERHE_DEBUG_LDOTN",              "1"}}, .default_uniform_block = &default_uniform_block } );
    add_shader(debug_hdotv             , CI{ .name = "standard_debug", .defines = {{"ERHE_DEBUG_HDOTV",              "1"}}, .default_uniform_block = &default_uniform_block } );
    add_shader(debug_joint_indices     , CI{ .name = "standard_debug", .defines = {{"ERHE_DEBUG_JOINT_INDICES",      "1"}}, .default_uniform_block = &default_uniform_block } );
    add_shader(debug_joint_weights     , CI{ .name = "standard_debug", .defines = {{"ERHE_DEBUG_JOINT_WEIGHTS",      "1"}}, .default_uniform_block = &default_uniform_block } );
    add_shader(debug_omega_o           , CI{ .name = "standard_debug", .defines = {{"ERHE_DEBUG_OMEGA_O",            "1"}}, .default_uniform_block = &default_uniform_block } );
    add_shader(debug_omega_i           , CI{ .name = "standard_debug", .defines = {{"ERHE_DEBUG_OMEGA_I",            "1"}}, .default_uniform_block = &default_uniform_block } );
    add_shader(debug_omega_g           , CI{ .name = "standard_debug", .defines = {{"ERHE_DEBUG_OMEGA_G",            "1"}}, .default_uniform_block = &default_uniform_block } );
    add_shader(debug_vertex_valency    , CI{ .name = "standard_debug", .defines = {{"ERHE_DEBUG_VERTEX_VALENCY",     "1"}}, .default_uniform_block = &default_uniform_block } );
    add_shader(debug_polygon_edge_count, CI{ .name = "standard_debug", .defines = {{"ERHE_DEBUG_POLYGON_EDGE_COUNT", "1"}}, .default_uniform_block = &default_uniform_block } );
    add_shader(debug_misc              , CI{ .name = "standard_debug", .defines = {{"ERHE_DEBUG_MISC",               "1"}}, .default_uniform_block = &default_uniform_block } );

    // Compile shaders

    static_cast<void>(executor);
    {
        ERHE_PROFILE_SCOPE("compile shaders");

        /// tf::Taskflow compile_taskflow;
        for (auto& entry : prototypes) {
            entry.prototype.compile_shaders();
        }
        //// executor.run(compile_taskflow).wait();
    }

    // Link programs
    {
        ERHE_PROFILE_SCOPE("link programs");

        //// tf::Taskflow link_taskflow;
        for (auto& entry : prototypes) {
            entry.prototype.link_program();
        }
    }

    {
        ERHE_PROFILE_SCOPE("post link");

        for (auto& entry : prototypes) {
            entry.reloadable_shader_stages.shader_stages.reload(std::move(entry.prototype));
            graphics_instance.shader_monitor.add(entry.reloadable_shader_stages);
        }
    }
}

auto Programs::get_variant_shader_stages(Shader_stages_variant variant) const -> const erhe::graphics::Shader_stages*
{
    switch (variant) {
        case Shader_stages_variant::not_set:                  return nullptr;
        case Shader_stages_variant::error:                    return &error.shader_stages;
        case Shader_stages_variant::standard:                 return &standard.shader_stages;
        case Shader_stages_variant::anisotropic_slope:        return &anisotropic_slope.shader_stages;
        case Shader_stages_variant::anisotropic_engine_ready: return &anisotropic_engine_ready.shader_stages;
        case Shader_stages_variant::circular_brushed_metal:   return &circular_brushed_metal.shader_stages;
        case Shader_stages_variant::debug_depth:              return &debug_depth.shader_stages;
        case Shader_stages_variant::debug_normal:             return &debug_normal.shader_stages;
        case Shader_stages_variant::debug_tangent:            return &debug_tangent.shader_stages;
        case Shader_stages_variant::debug_vertex_tangent_w:   return &debug_vertex_tangent_w.shader_stages;
        case Shader_stages_variant::debug_bitangent:          return &debug_bitangent.shader_stages;
        case Shader_stages_variant::debug_texcoord:           return &debug_texcoord.shader_stages;
        case Shader_stages_variant::debug_base_color_texture: return &debug_base_color_texture.shader_stages;
        case Shader_stages_variant::debug_vertex_color_rgb:   return &debug_vertex_color_rgb.shader_stages;
        case Shader_stages_variant::debug_vertex_color_alpha: return &debug_vertex_color_alpha.shader_stages;
        case Shader_stages_variant::debug_aniso_strength:     return &debug_aniso_strength.shader_stages;
        case Shader_stages_variant::debug_aniso_texcoord:     return &debug_aniso_texcoord.shader_stages;
        case Shader_stages_variant::debug_vdotn:              return &debug_vdotn.shader_stages;
        case Shader_stages_variant::debug_ldotn:              return &debug_ldotn.shader_stages;
        case Shader_stages_variant::debug_hdotv:              return &debug_hdotv.shader_stages;
        case Shader_stages_variant::debug_joint_indices:      return &debug_joint_indices.shader_stages;
        case Shader_stages_variant::debug_joint_weights:      return &debug_joint_weights.shader_stages;
        case Shader_stages_variant::debug_omega_o:            return &debug_omega_o.shader_stages;
        case Shader_stages_variant::debug_omega_i:            return &debug_omega_i.shader_stages;
        case Shader_stages_variant::debug_omega_g:            return &debug_omega_g.shader_stages;
        case Shader_stages_variant::debug_vertex_valency:     return &debug_vertex_valency.shader_stages;
        case Shader_stages_variant::debug_polygon_edge_count: return &debug_polygon_edge_count.shader_stages;
        case Shader_stages_variant::debug_misc:               return &debug_misc.shader_stages;
        default:                                              return &error.shader_stages;
    }
}

}
