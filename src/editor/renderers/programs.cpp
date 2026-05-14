#include "renderers/programs.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/shader_monitor.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_scene_renderer/shader_variant_cache.hpp"
#include "erhe_scene_renderer/standard_shader_variant.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>
#include <taskflow/taskflow.hpp>

namespace editor {

namespace {

// Helper to keep the Programs member-init list terse.
[[nodiscard]] auto sv_key(
    std::string                                      name,
    std::vector<std::pair<std::string, std::string>> defines = {},
    bool                                             no_vertex_input = false,
    bool                                             dump_interface  = false
) -> erhe::scene_renderer::Shader_variant_key
{
    return erhe::scene_renderer::Shader_variant_key{
        std::move(name),
        std::move(defines),
        /*multiview_view_count*/ 0u,
        no_vertex_input,
        dump_interface
    };
}

[[nodiscard]] auto multiview_count(const erhe::scene_renderer::Program_interface& program_interface) -> uint32_t
{
    // max_view_count >= 2 means the editor was built for multiview
    // (Quest stereo); each handle gets a multiview sibling that
    // load_programs will warm up. <2 means single-view only;
    // multiview_shader_stages() on every handle returns nullptr.
    const int n = program_interface.config.max_view_count;
    return (n >= 2) ? static_cast<uint32_t>(n) : 0u;
}

} // anonymous namespace

Programs::Programs(
    erhe::graphics::Device&                     graphics_device,
    erhe::scene_renderer::Program_interface&    program_interface,
    erhe::scene_renderer::Shader_variant_cache& cache
)
    : shader_paths{
        std::filesystem::path{"res"} / std::filesystem::path{"shaders"},
        std::filesystem::path{"res"} / std::filesystem::path{"editor"} / std::filesystem::path{"shaders"},
    }
    , error                   {cache, sv_key("error", {}, false, /*dump_interface*/ true), multiview_count(program_interface)}
    , brdf_slice              {cache, sv_key("brdf_slice"),                                multiview_count(program_interface)}
    , brush                   {cache, sv_key("brush"),                                     multiview_count(program_interface)}
    , standard                {cache, sv_key("standard", erhe::scene_renderer::make_standard_variant_defines(erhe::scene_renderer::Standard_variant_key{})), multiview_count(program_interface)}
    , anisotropic_slope       {cache, sv_key("anisotropic_slope"),                         multiview_count(program_interface)}
    , anisotropic_engine_ready{cache, sv_key("anisotropic_engine_ready"),                  multiview_count(program_interface)}
    , circular_brushed_metal  {cache, sv_key("circular_brushed_metal"),                    multiview_count(program_interface)}
    , textured                {cache, sv_key("textured"),                                  multiview_count(program_interface)}
    , sky                     {cache, sv_key("sky"),                                       multiview_count(program_interface)}
    , grid                    {cache, sv_key("grid"),                                      multiview_count(program_interface)}
    , wide_lines_draw_color   {cache, sv_key("wide_lines",   {{"ERHE_USE_DRAW_COLOR",   "1"}}), multiview_count(program_interface)}
    , wide_lines_vertex_color {cache, sv_key("wide_lines",   {{"ERHE_USE_VERTEX_COLOR", "1"}}), multiview_count(program_interface)}
    , points                  {cache, sv_key("points"),                                    multiview_count(program_interface)}
    , id                      {cache, sv_key("id"),                                        multiview_count(program_interface)}
    , tool                    {cache, sv_key("tool"),                                      multiview_count(program_interface)}
    , debug_depth             {cache, sv_key("visualize_depth", {}, /*no_vertex_input*/ true), multiview_count(program_interface)}
    , debug_vertex_normal     {cache, sv_key("standard_debug", {{"ERHE_DEBUG_VERTEX_NORMAL",      "1"}}), multiview_count(program_interface)}
    , debug_fragment_normal   {cache, sv_key("standard_debug", {{"ERHE_DEBUG_FRAGMENT_NORMAL",    "1"}}), multiview_count(program_interface)}
    , debug_normal_texture    {cache, sv_key("standard_debug", {{"ERHE_DEBUG_NORMAL_TEXTURE",     "1"}}), multiview_count(program_interface)}
    , debug_tangent           {cache, sv_key("standard_debug", {{"ERHE_DEBUG_TANGENT",            "1"}}), multiview_count(program_interface)}
    , debug_vertex_tangent_w  {cache, sv_key("standard_debug", {{"ERHE_DEBUG_TANGENT_W",          "1"}}), multiview_count(program_interface)}
    , debug_bitangent         {cache, sv_key("standard_debug", {{"ERHE_DEBUG_BITANGENT",          "1"}}), multiview_count(program_interface)}
    , debug_texcoord          {cache, sv_key("standard_debug", {{"ERHE_DEBUG_TEXCOORD",           "1"}}), multiview_count(program_interface)}
    , debug_base_color_texture{cache, sv_key("standard_debug", {{"ERHE_DEBUG_BASE_COLOR_TEXTURE", "1"}}), multiview_count(program_interface)}
    , debug_vertex_color_rgb  {cache, sv_key("standard_debug", {{"ERHE_DEBUG_VERTEX_COLOR_RGB",   "1"}}), multiview_count(program_interface)}
    , debug_vertex_color_alpha{cache, sv_key("standard_debug", {{"ERHE_DEBUG_VERTEX_COLOR_ALPHA", "1"}}), multiview_count(program_interface)}
    , debug_aniso_strength    {cache, sv_key("standard_debug", {{"ERHE_DEBUG_ANISO_STRENGTH",     "1"}}), multiview_count(program_interface)}
    , debug_aniso_texcoord    {cache, sv_key("standard_debug", {{"ERHE_DEBUG_ANISO_TEXCOORD",     "1"}}), multiview_count(program_interface)}
    , debug_vdotn             {cache, sv_key("standard_debug", {{"ERHE_DEBUG_VDOTN",              "1"}}), multiview_count(program_interface)}
    , debug_ldotn             {cache, sv_key("standard_debug", {{"ERHE_DEBUG_LDOTN",              "1"}}), multiview_count(program_interface)}
    , debug_hdotv             {cache, sv_key("standard_debug", {{"ERHE_DEBUG_HDOTV",              "1"}}), multiview_count(program_interface)}
    , debug_joint_indices     {cache, sv_key("standard_debug", {{"ERHE_DEBUG_JOINT_INDICES",      "1"}}), multiview_count(program_interface)}
    , debug_joint_weights     {cache, sv_key("standard_debug", {{"ERHE_DEBUG_JOINT_WEIGHTS",      "1"}}), multiview_count(program_interface)}
    , debug_omega_o           {cache, sv_key("standard_debug", {{"ERHE_DEBUG_OMEGA_O",            "1"}}), multiview_count(program_interface)}
    , debug_omega_i           {cache, sv_key("standard_debug", {{"ERHE_DEBUG_OMEGA_I",            "1"}}), multiview_count(program_interface)}
    , debug_omega_g           {cache, sv_key("standard_debug", {{"ERHE_DEBUG_OMEGA_G",            "1"}}), multiview_count(program_interface)}
    , debug_vertex_valency    {cache, sv_key("standard_debug", {{"ERHE_DEBUG_VERTEX_VALENCY",     "1"}}), multiview_count(program_interface)}
    , debug_polygon_edge_count{cache, sv_key("standard_debug", {{"ERHE_DEBUG_POLYGON_EDGE_COUNT", "1"}}), multiview_count(program_interface)}
    , debug_metallic          {cache, sv_key("standard_debug", {{"ERHE_DEBUG_METALLIC",           "1"}}), multiview_count(program_interface)}
    , debug_roughness         {cache, sv_key("standard_debug", {{"ERHE_DEBUG_ROUGHNESS",          "1"}}), multiview_count(program_interface)}
    , debug_occlusion         {cache, sv_key("standard_debug", {{"ERHE_DEBUG_OCCLUSION",          "1"}}), multiview_count(program_interface)}
    , debug_emissive          {cache, sv_key("standard_debug", {{"ERHE_DEBUG_EMISSIVE",           "1"}}), multiview_count(program_interface)}
    , debug_shadowmap_texels  {cache, sv_key("standard_debug", {{"ERHE_DEBUG_SHADOWMAP_TEXELS",   "1"}}), multiview_count(program_interface)}
    , debug_shadow            {cache, sv_key("shadow_debug"),                                                multiview_count(program_interface)}
    , debug_misc              {cache, sv_key("standard_debug", {{"ERHE_DEBUG_MISC",               "1"}}), multiview_count(program_interface)}
{
    static_cast<void>(graphics_device);
    // Name -> handle map for get_multiview(name). Backs the legacy
    // name-keyed multiview lookup that app_rendering.cpp still uses
    // through its get_multiview_stages helper. Flagged for removal
    // under E13 dead-code (every pipeline already passes a
    // Shader_stages_handle* directly via the create_info field).
    m_handles_by_name = {
        {"error",                     &error},
        {"brdf_slice",                &brdf_slice},
        {"brush",                     &brush},
        {"standard",                  &standard},
        {"anisotropic_slope",         &anisotropic_slope},
        {"anisotropic_engine_ready",  &anisotropic_engine_ready},
        {"circular_brushed_metal",    &circular_brushed_metal},
        {"textured",                  &textured},
        {"sky",                       &sky},
        {"grid",                      &grid},
        {"wide_lines_draw_color",     &wide_lines_draw_color},
        {"wide_lines_vertex_color",   &wide_lines_vertex_color},
        {"points",                    &points},
        {"id",                        &id},
        {"tool",                      &tool},
        {"debug_depth",               &debug_depth},
        {"debug_vertex_normal",       &debug_vertex_normal},
        {"debug_fragment_normal",     &debug_fragment_normal},
        {"debug_normal_texture",      &debug_normal_texture},
        {"debug_tangent",             &debug_tangent},
        {"debug_vertex_tangent_w",    &debug_vertex_tangent_w},
        {"debug_bitangent",           &debug_bitangent},
        {"debug_texcoord",            &debug_texcoord},
        {"debug_base_color_texture",  &debug_base_color_texture},
        {"debug_vertex_color_rgb",    &debug_vertex_color_rgb},
        {"debug_vertex_color_alpha",  &debug_vertex_color_alpha},
        {"debug_aniso_strength",      &debug_aniso_strength},
        {"debug_aniso_texcoord",      &debug_aniso_texcoord},
        {"debug_vdotn",               &debug_vdotn},
        {"debug_ldotn",               &debug_ldotn},
        {"debug_hdotv",               &debug_hdotv},
        {"debug_joint_indices",       &debug_joint_indices},
        {"debug_joint_weights",       &debug_joint_weights},
        {"debug_omega_o",             &debug_omega_o},
        {"debug_omega_i",             &debug_omega_i},
        {"debug_omega_g",             &debug_omega_g},
        {"debug_vertex_valency",      &debug_vertex_valency},
        {"debug_polygon_edge_count",  &debug_polygon_edge_count},
        {"debug_metallic",            &debug_metallic},
        {"debug_roughness",           &debug_roughness},
        {"debug_occlusion",           &debug_occlusion},
        {"debug_emissive",            &debug_emissive},
        {"debug_shadowmap_texels",    &debug_shadowmap_texels},
        {"debug_shadow",              &debug_shadow},
        {"debug_misc",                &debug_misc},
    };
}

void Programs::load_programs(
    tf::Executor&                            executor,
    erhe::graphics::Device&                  /*graphics_device*/,
    erhe::scene_renderer::Program_interface& /*program_interface*/,
    const Init_message_fn&                   init_message
)
{
    // The Programs ctor records every variant's key with the cache; the
    // cache compiles on miss whenever the first consumer asks for the
    // stages. The timing of that miss is not part of the handle's
    // contract -- a backend may resolve in pipeline ctor or at bind time.
    //
    // Kept as a no-op for the parallel-init taskflow that already calls
    // it; deletion tracked under E13 dead-code.
    static_cast<void>(executor);
    if (init_message) {
        init_message("");
    }
}

auto Programs::get_variant_shader_stages(Shader_stages_variant variant) -> const erhe::graphics::Shader_stages*
{
    switch (variant) {
        case Shader_stages_variant::not_set:                  return nullptr;
        case Shader_stages_variant::error:                    return error.shader_stages();
        case Shader_stages_variant::standard:                 return standard.shader_stages();
        case Shader_stages_variant::anisotropic_slope:        return anisotropic_slope.shader_stages();
        case Shader_stages_variant::anisotropic_engine_ready: return anisotropic_engine_ready.shader_stages();
        case Shader_stages_variant::circular_brushed_metal:   return circular_brushed_metal.shader_stages();
        case Shader_stages_variant::debug_depth:              return debug_depth.shader_stages();
        case Shader_stages_variant::debug_vertex_normal:      return debug_vertex_normal.shader_stages();
        case Shader_stages_variant::debug_fragment_normal:    return debug_fragment_normal.shader_stages();
        case Shader_stages_variant::debug_normal_texture:     return debug_normal_texture.shader_stages();
        case Shader_stages_variant::debug_tangent:            return debug_tangent.shader_stages();
        case Shader_stages_variant::debug_vertex_tangent_w:   return debug_vertex_tangent_w.shader_stages();
        case Shader_stages_variant::debug_bitangent:          return debug_bitangent.shader_stages();
        case Shader_stages_variant::debug_texcoord:           return debug_texcoord.shader_stages();
        case Shader_stages_variant::debug_base_color_texture: return debug_base_color_texture.shader_stages();
        case Shader_stages_variant::debug_vertex_color_rgb:   return debug_vertex_color_rgb.shader_stages();
        case Shader_stages_variant::debug_vertex_color_alpha: return debug_vertex_color_alpha.shader_stages();
        case Shader_stages_variant::debug_aniso_strength:     return debug_aniso_strength.shader_stages();
        case Shader_stages_variant::debug_aniso_texcoord:     return debug_aniso_texcoord.shader_stages();
        case Shader_stages_variant::debug_vdotn:              return debug_vdotn.shader_stages();
        case Shader_stages_variant::debug_ldotn:              return debug_ldotn.shader_stages();
        case Shader_stages_variant::debug_hdotv:              return debug_hdotv.shader_stages();
        case Shader_stages_variant::debug_joint_indices:      return debug_joint_indices.shader_stages();
        case Shader_stages_variant::debug_joint_weights:      return debug_joint_weights.shader_stages();
        case Shader_stages_variant::debug_omega_o:            return debug_omega_o.shader_stages();
        case Shader_stages_variant::debug_omega_i:            return debug_omega_i.shader_stages();
        case Shader_stages_variant::debug_omega_g:            return debug_omega_g.shader_stages();
        case Shader_stages_variant::debug_vertex_valency:     return debug_vertex_valency.shader_stages();
        case Shader_stages_variant::debug_polygon_edge_count: return debug_polygon_edge_count.shader_stages();
        case Shader_stages_variant::debug_metallic:           return debug_metallic.shader_stages();
        case Shader_stages_variant::debug_roughness:          return debug_roughness.shader_stages();
        case Shader_stages_variant::debug_occlusion:          return debug_occlusion.shader_stages();
        case Shader_stages_variant::debug_emissive:           return debug_emissive.shader_stages();
        case Shader_stages_variant::debug_shadowmap_texels:   return debug_shadowmap_texels.shader_stages();
        case Shader_stages_variant::debug_misc:               return debug_misc.shader_stages();
        default:                                              return error.shader_stages();
    }
}

auto Programs::get_multiview(std::string_view name) -> const erhe::graphics::Shader_stages*
{
    auto it = m_handles_by_name.find(std::string{name});
    if (it == m_handles_by_name.end()) {
        return nullptr;
    }
    return it->second->multiview_shader_stages();
}

}
