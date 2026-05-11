#include "renderers/programs.hpp"

#include "erhe_graphics/shader_monitor.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>
#include <taskflow/taskflow.hpp>

namespace editor {

Programs::Shader_stages_builder::Shader_stages_builder(
    erhe::graphics::Reloadable_shader_stages& reloadable_shader_stages,
    erhe::scene_renderer::Program_interface&  program_interface
)
    : reloadable_shader_stages{reloadable_shader_stages}
    , prototype{
        program_interface.make_prototype(reloadable_shader_stages.create_info)
    }
{
}

Programs::Shader_stages_builder::Shader_stages_builder(Shader_stages_builder&& other) noexcept
    : reloadable_shader_stages{other.reloadable_shader_stages}
    , prototype               {std::move(other.prototype)}
{
}

Programs::Programs(erhe::graphics::Device& graphics_device, erhe::scene_renderer::Program_interface& /*program_interface*/)
    : shader_paths{
        std::filesystem::path{"res"} / std::filesystem::path{"shaders"},
        std::filesystem::path{"res"} / std::filesystem::path{"editor"} / std::filesystem::path{"shaders"},
    }
    , error                   {graphics_device, "error-not_loaded"}
    , brdf_slice              {graphics_device, "brdf_slice-not_loaded"}
    , brush                   {graphics_device, "brush-not_loaded"}
    , standard                {graphics_device, "standard-not_loaded"}
    , anisotropic_slope       {graphics_device, "anisotropic_slope-not_loaded"}
    , anisotropic_engine_ready{graphics_device, "anisotropic_engine_ready-not_loaded"}
    , circular_brushed_metal  {graphics_device, "circular_brushed_metal-not_loaded"}
    , textured                {graphics_device, "textured-not_loaded"}
    , sky                     {graphics_device, "sky-not_loaded"}
    , grid                    {graphics_device, "grid-not_loaded"}
    , wide_lines_draw_color   {graphics_device, "wide_lines_draw_color-not_loaded"}
    , wide_lines_vertex_color {graphics_device, "wide_lines_vertex_color-not_loaded"}
    , points                  {graphics_device, "points-not_loaded"}
    , depth                   {graphics_device, "depth-not_loaded"}
    , id                      {graphics_device, "id-not_loaded"}
    , tool                    {graphics_device, "tool-not_loaded"}
    , debug_depth             {graphics_device, "debug_depth-not_loaded"}
    , debug_vertex_normal     {graphics_device, "debug_vertex_normal-not_loaded"}
    , debug_fragment_normal   {graphics_device, "debug_fragment_normal-not_loaded"}
    , debug_normal_texture    {graphics_device, "debug_normal_texture-not_loaded"}
    , debug_tangent           {graphics_device, "debug_tangent-not_loaded"}
    , debug_vertex_tangent_w  {graphics_device, "debug_vertex_tangent_w-not_loaded"}
    , debug_bitangent         {graphics_device, "debug_bitangent-not_loaded"}
    , debug_texcoord          {graphics_device, "debug_texcoord-not_loaded"}
    , debug_base_color_texture{graphics_device, "debug_base_color_texture-not_loaded"}
    , debug_vertex_color_rgb  {graphics_device, "debug_vertex_color_rgb-not_loaded"}
    , debug_vertex_color_alpha{graphics_device, "debug_vertex_color_alpha-not_loaded"}
    , debug_aniso_strength    {graphics_device, "debug_aniso_strength-not_loaded"}
    , debug_aniso_texcoord    {graphics_device, "debug_aniso_texcoord-not_loaded"}
    , debug_vdotn             {graphics_device, "debug_v_dot_n-not_loaded"}
    , debug_ldotn             {graphics_device, "debug_v_dot_n-not_loaded"}
    , debug_hdotv             {graphics_device, "debug_h_dot_v-not_loaded"}
    , debug_joint_indices     {graphics_device, "debug_joint_indices-not_loaded"}
    , debug_joint_weights     {graphics_device, "debug_joint_weights-not_loaded"}
    , debug_omega_o           {graphics_device, "debug_omega_o-not_loaded"}
    , debug_omega_i           {graphics_device, "debug_omega_i-not_loaded"}
    , debug_omega_g           {graphics_device, "debug_omega_g-not_loaded"}
    , debug_vertex_valency    {graphics_device, "debug_vertex_valency-not_loaded"}
    , debug_polygon_edge_count{graphics_device, "debug_polygon_edge_count_g-not_loaded"}
    , debug_metallic          {graphics_device, "debug_metallic-not_loaded"}
    , debug_roughness         {graphics_device, "debug_roughness-not_loaded"}
    , debug_occlusion         {graphics_device, "debug_occlusion-not_loaded"}
    , debug_emissive          {graphics_device, "debug_emissive-not_loaded"}
    , debug_shadowmap_texels  {graphics_device, "debug_shadowmap_texels-not_loaded"}
    , debug_shadow            {graphics_device, "debug_shadow"}
    , debug_misc              {graphics_device, "debug_misc-not_loaded"}
{
}

void Programs::load_programs(
    tf::Executor&                            executor,
    erhe::graphics::Device&                  graphics_device,
    erhe::scene_renderer::Program_interface& program_interface,
    const Init_message_fn&                   init_message
)
{
    std::vector<Shader_stages_builder> prototypes;

    using CI = erhe::graphics::Shader_stages_create_info;

    // Multiview is on when the program interface was built with
    // max_view_count >= 2 (driven by Xr_session::is_multiview_enabled()
    // in editor.cpp). When on, every shader is also compiled with
    // enable_multiview(view_count) and stored in m_multiview_variants
    // for the headset render path to look up.
    const bool     multiview_enabled = (program_interface.config.max_view_count >= 2);
    const uint32_t multiview_view_count = static_cast<uint32_t>(program_interface.config.max_view_count);

    auto add_shader = [this, &program_interface, &prototypes, &init_message, multiview_enabled, multiview_view_count, &graphics_device](
        erhe::graphics::Reloadable_shader_stages&   reloadable_shader_stages,
        erhe::graphics::Shader_stages_create_info&& create_info
    ) {
        if (init_message) {
            init_message(
                fmt::format(
                    "Loading shader stages: {}",
                    create_info.debug_label.empty()
                        ? create_info.name.c_str()
                        : create_info.debug_label.data()
                )
            );
        }

        // Build the multiview variant first so we can copy from
        // create_info before std::move'ing it into the single-view
        // create_info slot. The multiview variant shares the original
        // create_info's debug_label / name -- the only delta is
        // enable_multiview(), which adds GL_EXT_multiview, ERHE_MULTIVIEW
        // = 1, etc. via the shader prelude.
        //
        // The map key has to be unique per add_shader call. Many
        // shaders share the same .name (e.g. several debug variants all
        // use "standard_debug") but differ by .debug_label, so the key
        // is debug_label when set, otherwise name. Without this, a
        // second add_shader with the same .name would replace the map
        // entry, destroying the Reloadable_shader_stages that the
        // first Shader_stages_builder in `prototypes` already holds a
        // reference to -- the dangling reference then crashes when the
        // post-link loop reads its create_info.name.
        if (multiview_enabled) {
            erhe::graphics::Shader_stages_create_info multiview_create_info = create_info;
            multiview_create_info.enable_multiview(multiview_view_count);

            const std::string mv_key = create_info.debug_label.empty()
                ? create_info.name
                : std::string{create_info.debug_label.string_view()};
            const std::string mv_placeholder_name = mv_key + "-multiview-not_loaded";
            auto& mv_slot = m_multiview_variants[mv_key];
            // Defensive: if the same key has already been registered
            // (which would indicate a duplicate add_shader call we
            // missed) fall through and overwrite -- the previous
            // Shader_stages_builder reference would dangle, but at
            // least we surface the issue rather than silently corrupt.
            ERHE_VERIFY(mv_slot == nullptr);
            mv_slot = std::make_unique<erhe::graphics::Reloadable_shader_stages>(graphics_device, mv_placeholder_name);
            mv_slot->create_info = std::move(multiview_create_info);
            prototypes.emplace_back(*mv_slot, program_interface);
        }

        reloadable_shader_stages.create_info = std::move(create_info);
        prototypes.emplace_back(reloadable_shader_stages, program_interface);
    };

    add_shader(error                   , CI{ .name = "error"                   , .dump_interface = true } );
    add_shader(standard                , CI{ .name = "standard"                 } );
    add_shader(anisotropic_slope       , CI{ .name = "anisotropic_slope"        } );
    add_shader(anisotropic_engine_ready, CI{ .name = "anisotropic_engine_ready" } );
    add_shader(circular_brushed_metal  , CI{ .name = "circular_brushed_metal"   } );
    add_shader(brdf_slice              , CI{ .name = "brdf_slice"               } );
    add_shader(brush                   , CI{ .name = "brush"                    } );
    add_shader(textured                , CI{ .name = "textured"                 } );
    add_shader(sky                     , CI{ .name = "sky"                      } );
    add_shader(grid                    , CI{ .name = "grid"                     } );
    // wide_lines shaders use geometry shaders which are not supported on Metal
    if (!graphics_device.get_info().use_compute_shader) {
        add_shader(wide_lines_draw_color   , CI{ .name = "wide_lines"              , .defines = {{"ERHE_USE_DRAW_COLOR",   "1"}}});
        add_shader(wide_lines_vertex_color , CI{ .name = "wide_lines"              , .defines = {{"ERHE_USE_VERTEX_COLOR", "1"}}});
    }
    add_shader(points                  , CI{ .name = "points" } );
    add_shader(id                      , CI{ .name = "id"     } );
    add_shader(tool                    , CI{ .name = "tool"   } );
    add_shader(debug_depth             , CI{ .name = "visualize_depth", .no_vertex_input = true } );
    add_shader(debug_vertex_normal     , CI{ .name = "standard_debug", .debug_label = "debug_vertex_normal",      .defines = {{"ERHE_DEBUG_VERTEX_NORMAL",      "1"}} } );
    add_shader(debug_fragment_normal   , CI{ .name = "standard_debug", .debug_label = "debug_fragment_normal",    .defines = {{"ERHE_DEBUG_FRAGMENT_NORMAL",    "1"}} } );
    add_shader(debug_normal_texture    , CI{ .name = "standard_debug", .debug_label = "debug_normal_texture",     .defines = {{"ERHE_DEBUG_NORMAL_TEXTURE",     "1"}} } );
    add_shader(debug_tangent           , CI{ .name = "standard_debug", .debug_label = "debug_tangent",            .defines = {{"ERHE_DEBUG_TANGENT",            "1"}} } );
    add_shader(debug_vertex_tangent_w  , CI{ .name = "standard_debug", .debug_label = "debug_vertex_tangent_w",   .defines = {{"ERHE_DEBUG_TANGENT_W",          "1"}} } );
    add_shader(debug_bitangent         , CI{ .name = "standard_debug", .debug_label = "debug_bitangent",          .defines = {{"ERHE_DEBUG_BITANGENT",          "1"}} } );
    add_shader(debug_texcoord          , CI{ .name = "standard_debug", .debug_label = "debug_texcoord",           .defines = {{"ERHE_DEBUG_TEXCOORD",           "1"}} } );
    add_shader(debug_base_color_texture, CI{ .name = "standard_debug", .debug_label = "debug_base_color_texture", .defines = {{"ERHE_DEBUG_BASE_COLOR_TEXTURE", "1"}} } );
    add_shader(debug_vertex_color_rgb  , CI{ .name = "standard_debug", .debug_label = "debug_vertex_color_rgb",   .defines = {{"ERHE_DEBUG_VERTEX_COLOR_RGB",   "1"}} } );
    add_shader(debug_vertex_color_alpha, CI{ .name = "standard_debug", .debug_label = "debug_vertex_color_alpha", .defines = {{"ERHE_DEBUG_VERTEX_COLOR_ALPHA", "1"}} } );
    add_shader(debug_aniso_strength    , CI{ .name = "standard_debug", .debug_label = "debug_aniso_strength",     .defines = {{"ERHE_DEBUG_ANISO_STRENGTH",     "1"}} } );
    add_shader(debug_aniso_texcoord    , CI{ .name = "standard_debug", .debug_label = "debug_aniso_texcoord",     .defines = {{"ERHE_DEBUG_ANISO_TEXCOORD",     "1"}} } );
    add_shader(debug_vdotn             , CI{ .name = "standard_debug", .debug_label = "debug_vdotn",              .defines = {{"ERHE_DEBUG_VDOTN",              "1"}} } );
    add_shader(debug_ldotn             , CI{ .name = "standard_debug", .debug_label = "debug_ldotn",              .defines = {{"ERHE_DEBUG_LDOTN",              "1"}} } );
    add_shader(debug_hdotv             , CI{ .name = "standard_debug", .debug_label = "debug_hdotv",              .defines = {{"ERHE_DEBUG_HDOTV",              "1"}} } );
    add_shader(debug_joint_indices     , CI{ .name = "standard_debug", .debug_label = "debug_joint_indices",      .defines = {{"ERHE_DEBUG_JOINT_INDICES",      "1"}} } );
    add_shader(debug_joint_weights     , CI{ .name = "standard_debug", .debug_label = "debug_joint_weights",      .defines = {{"ERHE_DEBUG_JOINT_WEIGHTS",      "1"}} } );
    add_shader(debug_omega_o           , CI{ .name = "standard_debug", .debug_label = "debug_omega_o",            .defines = {{"ERHE_DEBUG_OMEGA_O",            "1"}} } );
    add_shader(debug_omega_i           , CI{ .name = "standard_debug", .debug_label = "debug_omega_i",            .defines = {{"ERHE_DEBUG_OMEGA_I",            "1"}} } );
    add_shader(debug_omega_g           , CI{ .name = "standard_debug", .debug_label = "debug_omega_g",            .defines = {{"ERHE_DEBUG_OMEGA_G",            "1"}} } );
    add_shader(debug_vertex_valency    , CI{ .name = "standard_debug", .debug_label = "debug_vertex_valency",     .defines = {{"ERHE_DEBUG_VERTEX_VALENCY",     "1"}} } );
    add_shader(debug_polygon_edge_count, CI{ .name = "standard_debug", .debug_label = "debug_polygon_edge_count", .defines = {{"ERHE_DEBUG_POLYGON_EDGE_COUNT", "1"}} } );
    add_shader(debug_metallic          , CI{ .name = "standard_debug", .debug_label = "debug_metallic",           .defines = {{"ERHE_DEBUG_METALLIC",           "1"}} } );
    add_shader(debug_roughness         , CI{ .name = "standard_debug", .debug_label = "debug_roughness",          .defines = {{"ERHE_DEBUG_ROUGHNESS",          "1"}} } );
    add_shader(debug_occlusion         , CI{ .name = "standard_debug", .debug_label = "debug_occlusion",          .defines = {{"ERHE_DEBUG_OCCLUSION",          "1"}} } );
    add_shader(debug_emissive          , CI{ .name = "standard_debug", .debug_label = "debug_emissive",           .defines = {{"ERHE_DEBUG_EMISSIVE",           "1"}} } );
    add_shader(debug_shadowmap_texels  , CI{ .name = "standard_debug", .debug_label = "debug_shadowmap_texels",   .defines = {{"ERHE_DEBUG_SHADOWMAP_TEXELS",   "1"}} } );
    add_shader(debug_shadow            , CI{ .name = "shadow_debug",   .debug_label = "debug_shadow" } );
    add_shader(debug_misc              , CI{ .name = "standard_debug", .debug_label = "debug_misc",               .defines = {{"ERHE_DEBUG_MISC",               "1"}} } );

    // Compile shaders

    static_cast<void>(executor);
    {
        ERHE_PROFILE_SCOPE("compile shaders");

        /// tf::Taskflow compile_taskflow;
        for (auto& entry : prototypes) {
            if (init_message) {
                init_message(
                    fmt::format(
                        "Compiling shader stages: {}",
                        entry.reloadable_shader_stages.create_info.debug_label.empty()
                            ? entry.reloadable_shader_stages.create_info.name.c_str()
                            : entry.reloadable_shader_stages.create_info.debug_label.data()
                    )
                );
            }
            entry.prototype.compile_shaders();
            // std::this_thread::sleep_for(std::chrono::milliseconds{100});
        }
        //// executor.run(compile_taskflow).wait();
    }

    // Link programs
    {
        ERHE_PROFILE_SCOPE("link programs");

        //// tf::Taskflow link_taskflow;
        for (auto& entry : prototypes) {
            if (init_message) {
                init_message(
                    fmt::format(
                        "Compiling shader stages: {}",
                        entry.reloadable_shader_stages.create_info.debug_label.empty()
                            ? entry.reloadable_shader_stages.create_info.name.c_str()
                            : entry.reloadable_shader_stages.create_info.debug_label.data()
                    )
                );
            }
            entry.prototype.link_program();
            // std::this_thread::sleep_for(std::chrono::milliseconds{100});
        }
    }

    {
        ERHE_PROFILE_SCOPE("post link");

        for (auto& entry : prototypes) {
            entry.reloadable_shader_stages.shader_stages.reload(std::move(entry.prototype));
            graphics_device.get_shader_monitor().add(entry.reloadable_shader_stages);
        }
    }

    if (init_message) {
        init_message("");
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
        case Shader_stages_variant::debug_vertex_normal:      return &debug_vertex_normal.shader_stages;
        case Shader_stages_variant::debug_fragment_normal:    return &debug_fragment_normal.shader_stages;
        case Shader_stages_variant::debug_normal_texture:     return &debug_normal_texture.shader_stages;
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
        case Shader_stages_variant::debug_metallic:           return &debug_metallic.shader_stages;
        case Shader_stages_variant::debug_roughness:          return &debug_roughness.shader_stages;
        case Shader_stages_variant::debug_occlusion:          return &debug_occlusion.shader_stages;
        case Shader_stages_variant::debug_emissive:           return &debug_emissive.shader_stages;
        case Shader_stages_variant::debug_shadowmap_texels:   return &debug_shadowmap_texels.shader_stages;
        case Shader_stages_variant::debug_misc:               return &debug_misc.shader_stages;
        default:                                              return &error.shader_stages;
    }
}

auto Programs::get_multiview(std::string_view name) const -> const erhe::graphics::Shader_stages*
{
    auto it = m_multiview_variants.find(std::string{name});
    if (it == m_multiview_variants.end()) {
        return nullptr;
    }
    return &it->second->shader_stages;
}

}
