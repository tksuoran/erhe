#include "renderers/prewarm.hpp"

#include "app_context.hpp"
#include "app_rendering.hpp"
#include "app_scenes.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "preview/brush_preview.hpp"
#include "preview/material_preview.hpp"
#include "renderers/composer.hpp"
#include "renderers/composition_pass.hpp"
#include "rendergraph/shadow_render_node.hpp"
#include "scene/scene_root.hpp"
#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#   include "erhe_xr/headset.hpp"
#   include "erhe_xr/xr_session.hpp"
#endif

#include "erhe_primitive/material.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/forward_renderer.hpp"
#include "erhe_scene_renderer/shader_key.hpp"
#include "erhe_scene_renderer/shadow_renderer.hpp"

#include <chrono>

namespace editor {

namespace {

using clock_type      = std::chrono::steady_clock;
using milliseconds_d  = std::chrono::duration<double, std::milli>;

[[nodiscard]] auto ms_since(const clock_type::time_point& start) -> double
{
    return std::chrono::duration_cast<milliseconds_d>(clock_type::now() - start).count();
}

} // anonymous namespace

void prewarm_all(
    App_context&                                 context,
    const std::function<void(std::string_view)>& init_message
)
{
    ERHE_PROFILE_FUNCTION();

    if (
        (context.app_scenes       == nullptr) ||
        (context.app_rendering    == nullptr) ||
        (context.forward_renderer == nullptr) ||
        (context.shadow_renderer  == nullptr) ||
        (context.mesh_memory      == nullptr)
    ) {
        return;
    }

    // Collect the view counts the runtime forward path will encounter.
    // Single-view (view_count = 0, matches base.views.size() == 1 at runtime) is
    // always present. OpenXR builds with multiview also walk
    // Xr_session::get_view_count() (typically 2 for stereo).
    std::vector<uint32_t> multiview_view_counts{ 0u };
#if defined(ERHE_XR_LIBRARY_OPENXR)
    if (context.headset_view != nullptr) {
        erhe::xr::Headset* headset = context.headset_view->get_headset();
        if (headset != nullptr) {
            erhe::xr::Xr_session* xr_session = headset->get_xr_session();
            if ((xr_session != nullptr) && xr_session->is_multiview_enabled()) {
                const uint32_t xr_view_count = xr_session->get_view_count();
                if (xr_view_count > 1u) {
                    multiview_view_counts.push_back(xr_view_count);
                }
            }
        }
    }
#endif

    const clock_type::time_point t_total_start = clock_type::now();

    // ---------------------------------------------------------------------
    // Per-Scene_root walk: forward (Phase 1) + shadow (Phase 1 always,
    // Phase 2 when shadow nodes exist) + content-library extras.
    // ---------------------------------------------------------------------
    std::size_t forward_pipeline_warmups = 0;
    std::size_t scenes_walked            = 0;
    std::size_t shadow_calls             = 0;
    const clock_type::time_point t_forward_start = clock_type::now();

    const std::vector<std::shared_ptr<Composition_pass>>&         passes       = context.app_rendering->composition_passes();
    const std::vector<std::shared_ptr<Shadow_render_node>>&       shadow_nodes = context.app_rendering->get_all_shadow_nodes();

    const std::span<const uint32_t> view_counts_span{multiview_view_counts.data(), multiview_view_counts.size()};

    for (const std::shared_ptr<Scene_root>& scene_root : context.app_scenes->get_scene_roots()) {
        if (!scene_root) {
            continue;
        }
        ++scenes_walked;

        if (init_message) {
            init_message(scene_root->get_name());
        }

        erhe::scene::Scene&             scene       = scene_root->get_scene();
        const erhe::scene::Light_layer* light_layer = scene_root->layers().light();

        std::span<const std::shared_ptr<erhe::scene::Light>> lights_span{};
        if (light_layer != nullptr) {
            lights_span = light_layer->lights;
        }
        const erhe::scene_renderer::Light_layer_partition partition = erhe::scene_renderer::compute_light_layer_partition(lights_span);

        std::span<const std::shared_ptr<erhe::primitive::Material>> extra_materials{};
        const std::shared_ptr<Content_library> content_library = scene_root->get_content_library();
        if (content_library && content_library->materials) {
            extra_materials = content_library->materials->get_all<erhe::primitive::Material>();
        }

        for (const std::shared_ptr<Composition_pass>& pass : passes) {
            if (!pass) {
                continue;
            }
            Composition_pass_data& data = pass->data;
            // Fullscreen passes (empty mesh_layers) take the
            // Forward_renderer::draw_primitives path with their own
            // fixed shader_stages; they do not consult the variant cache,
            // so prewarming them via prewarm_standard_variants would just
            // compile unrelated variants.
            if (data.mesh_layers.empty()) {
                continue;
            }
            if (data.base_render_pipelines.empty()) {
                continue;
            }

            std::vector<std::span<const std::shared_ptr<erhe::scene::Mesh>>> mesh_spans;
            mesh_spans.reserve(data.mesh_layers.size());
            for (const erhe::scene::Layer_id layer_id : data.mesh_layers) {
                const std::shared_ptr<erhe::scene::Mesh_layer> mesh_layer = scene.get_mesh_layer_by_id(layer_id);
                if (mesh_layer) {
                    mesh_spans.push_back(mesh_layer->meshes);
                }
            }

            const erhe::scene_renderer::Forward_renderer::Prewarm_parameters params{
                .blending_mode_policy          = data.blending_mode_policy,
                .render_pipeline_states        = data.base_render_pipelines,
                .mesh_spans                    = mesh_spans,
                .extra_materials               = extra_materials,
                .multiview_view_counts         = view_counts_span,
                .mesh_memory                   = *context.mesh_memory,
                .primitive_mode                = data.primitive_mode,
                .warmup_targets                = {},
                .light_partition               = partition,
                .shader_key_force_enable_mask  = data.shader_key_force_enable_mask,
                .shader_key_force_disable_mask = data.shader_key_force_disable_mask,
                .shader_debug                  = erhe::scene_renderer::Shader_debug::none
            };
            forward_pipeline_warmups += context.forward_renderer->prewarm_standard_variants(params);
        }

        // Shadow variants the runtime would request for this scene's
        // content + controller meshes (the layers shadow_renderer::render
        // walks). Done per-scene_root so the depth-only shader modules
        // exist before first frame even when no Shadow_render_node has
        // been constructed yet (it's typically built lazily after init,
        // via a viewport-config message). When shadow nodes do exist,
        // each one's render_passes also drives Phase 2 pipeline cache
        // population; the redundant Phase 1 calls on the second+ pass
        // hit the variant cache.
        std::vector<std::span<const std::shared_ptr<erhe::scene::Mesh>>> shadow_mesh_spans;
        if (const erhe::scene::Mesh_layer* content_layer = scene_root->layers().content(); content_layer != nullptr) {
            shadow_mesh_spans.push_back(content_layer->meshes);
        }
        if (const erhe::scene::Mesh_layer* controller_layer = scene_root->layers().controller(); controller_layer != nullptr) {
            shadow_mesh_spans.push_back(controller_layer->meshes);
        }

        if (shadow_nodes.empty()) {
            context.shadow_renderer->prewarm_pipelines({}, shadow_mesh_spans);
            ++shadow_calls;
        } else {
            for (const std::shared_ptr<Shadow_render_node>& shadow_node : shadow_nodes) {
                if (!shadow_node) {
                    continue;
                }
                context.shadow_renderer->prewarm_pipelines(shadow_node->get_render_passes(), shadow_mesh_spans);
                ++shadow_calls;
            }
        }
    }
    const double forward_ms = ms_since(t_forward_start);

    // ---------------------------------------------------------------------
    // Previews
    // ---------------------------------------------------------------------
    const clock_type::time_point t_preview_start = clock_type::now();
    if (context.material_preview != nullptr) {
        context.material_preview->prewarm_variants(*context.forward_renderer);
    }
    if (context.brush_preview != nullptr) {
        context.brush_preview->prewarm_variants(*context.forward_renderer);
    }
    const double preview_ms = ms_since(t_preview_start);

    const double total_ms = ms_since(t_total_start);

    log_startup->info(
        "prewarm: walked {} scene root(s); forward warmed {} pipeline(s); "
        "shadow prewarm: {} call(s) over {} node(s); "
        "scene phase {:.1f} ms; previews {:.1f} ms; total {:.1f} ms",
        scenes_walked,
        forward_pipeline_warmups,
        shadow_calls,
        shadow_nodes.size(),
        forward_ms,
        preview_ms,
        total_ms
    );
}

} // namespace editor
