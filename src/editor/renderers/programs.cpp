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
    , textured                {cache, sv_key("textured"),                                  multiview_count(program_interface)}
    , sky                     {cache, sv_key("sky"),                                       multiview_count(program_interface)}
    , grid                    {cache, sv_key("grid"),                                      multiview_count(program_interface)}
    , wide_lines_draw_color   {cache, sv_key("wide_lines",   {{"ERHE_USE_DRAW_COLOR",   "1"}}), multiview_count(program_interface)}
    , wide_lines_vertex_color {cache, sv_key("wide_lines",   {{"ERHE_USE_VERTEX_COLOR", "1"}}), multiview_count(program_interface)}
    , points                  {cache, sv_key("points"),                                    multiview_count(program_interface)}
    , id                      {cache, sv_key("id"),                                        multiview_count(program_interface)}
    , tool                    {cache, sv_key("tool"),                                      multiview_count(program_interface)}
    , debug_depth             {cache, sv_key("visualize_depth", {}, /*no_vertex_input*/ true), multiview_count(program_interface)}
    , debug_shadow            {cache, sv_key("shadow_debug"),                              multiview_count(program_interface)}
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
        {"textured",                  &textured},
        {"sky",                       &sky},
        {"grid",                      &grid},
        {"wide_lines_draw_color",     &wide_lines_draw_color},
        {"wide_lines_vertex_color",   &wide_lines_vertex_color},
        {"points",                    &points},
        {"id",                        &id},
        {"tool",                      &tool},
        {"debug_depth",               &debug_depth},
        {"debug_shadow",              &debug_shadow},
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
        case Shader_stages_variant::debug_depth:              return debug_depth.shader_stages();
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
