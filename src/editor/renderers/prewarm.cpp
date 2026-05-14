#include "renderers/prewarm.hpp"

#include "app_context.hpp"
#include "app_rendering.hpp"
#include "app_scenes.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "preview/brush_preview.hpp"
#include "preview/material_preview.hpp"
#include "renderers/composition_pass.hpp"
#include "rendergraph/shadow_render_node.hpp"
#include "scene/scene_root.hpp"

#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/forward_renderer.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_scene_renderer/shadow_renderer.hpp"
#include "erhe_scene_renderer/standard_shader_variant.hpp"
#include "erhe_scene_renderer/standard_shader_variants.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <span>
#include <vector>

namespace editor {

namespace {

[[nodiscard]] auto collect_standard_variant_pipelines(
    const std::vector<std::shared_ptr<Composition_pass>>& passes
) -> std::vector<erhe::graphics::Lazy_render_pipeline*>
{
    std::vector<erhe::graphics::Lazy_render_pipeline*> result;
    // We intentionally ignore Composition_pass::enabled here: a pass that
    // is disabled at init time may flip on later (via UI / config) and at
    // that moment the variant cache had better already have its entries.
    // Prewarming a disabled pass costs one extra glslang compile per
    // unique (key, view_count); cheap insurance against a runtime hitch.
    for (const std::shared_ptr<Composition_pass>& pass : passes) {
        if (!pass) {
            continue;
        }
        for (erhe::graphics::Lazy_render_pipeline* lazy : pass->render_pipeline_states) {
            if (lazy == nullptr) {
                continue;
            }
            const erhe::graphics::Render_pipeline_create_info& ci = lazy->data;
            if (!ci.uses_standard_variants) {
                continue;
            }
            if (ci.vertex_format == nullptr) {
                continue;
            }
            // Several composition passes share the same Lazy_render_pipeline
            // (e.g. selected/not-selected variants that differ only in
            // filter); compile-key dedup makes the bucket walk O(unique
            // pipelines) instead of O(passes).
            if (std::find(result.begin(), result.end(), lazy) == result.end()) {
                result.push_back(lazy);
            }
        }
    }
    return result;
}

} // anonymous namespace

void prewarm_all(App_context& context)
{
    if ((context.forward_renderer == nullptr) ||
        (context.standard_shader_variants == nullptr) ||
        (context.app_scenes == nullptr) ||
        (context.app_rendering == nullptr) ||
        (context.mesh_memory == nullptr))
    {
        return;
    }

    const std::size_t before = context.standard_shader_variants->size();
    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();

    // View counts to prewarm. OpenXR uses multiview view_count = N
    // (typically 2 for stereo); single-view paths use 0. We prewarm
    // both whenever OpenXR is on so the desktop mirror window
    // (single-view) and the headset (multiview) both find their
    // variants in cache. The hard-coded 2 is correct for Quest stereo;
    // a Varjo quad-view configuration (view_count = 4) needs the
    // value sourced from Headset_view / Xr_session at startup --
    // tracked in quest_fixes.md E4.
    std::vector<uint32_t> view_counts;
    if (context.OpenXR) {
        view_counts.push_back(0u);
        view_counts.push_back(2u);
    } else {
        view_counts.push_back(0u);
    }

    std::vector<erhe::graphics::Lazy_render_pipeline*> standard_pipelines =
        collect_standard_variant_pipelines(context.app_rendering->composition_passes());

    const std::vector<std::shared_ptr<Scene_root>>& scene_roots =
        context.app_scenes->get_scene_roots();

    for (const std::shared_ptr<Scene_root>& scene_root : scene_roots) {
        if (!scene_root) {
            continue;
        }
        const erhe::scene::Light_layer* light_layer = scene_root->layers().light();
        if (light_layer == nullptr) {
            continue;
        }

        const erhe::scene_renderer::Standard_variant_light_counts light_counts =
            erhe::scene_renderer::compute_standard_variant_light_counts(*light_layer);

        const erhe::scene::Mesh_layer* content_layer    = scene_root->layers().content();
        const erhe::scene::Mesh_layer* controller_layer = scene_root->layers().controller();

        std::vector<std::span<const std::shared_ptr<erhe::scene::Mesh>>> mesh_spans;
        if (content_layer != nullptr) {
            mesh_spans.push_back(content_layer->meshes);
        }
        if (controller_layer != nullptr) {
            mesh_spans.push_back(controller_layer->meshes);
        }

        // Content library: every material that exists in the library,
        // attached or not. For unattached materials we use the
        // mesh_memory's vertex_format as the fallback so a future
        // assignment to a real mesh hits the cache (the bucket-walk
        // above already covers attached materials via their meshes).
        std::span<const std::shared_ptr<erhe::primitive::Material>> extra_materials;
        const std::shared_ptr<Content_library> library = scene_root->get_content_library();
        if (library && library->materials) {
            extra_materials = library->materials->get_all<erhe::primitive::Material>();
        }

        const erhe::scene_renderer::Forward_renderer::Prewarm_parameters params{
            .render_pipeline_states   = std::span<erhe::graphics::Lazy_render_pipeline*>{standard_pipelines},
            .mesh_spans               = mesh_spans,
            .extra_materials          = extra_materials,
            .light_counts             = light_counts,
            .multiview_view_counts    = std::span<const uint32_t>{view_counts},
            .standard_shader_variants = context.standard_shader_variants,
            .fallback_vertex_format   = &context.mesh_memory->vertex_format,
            // E17 follow-up: prewarm currently only covers polygon_fill.
            // The editor's composition passes can opt into edge_lines /
            // corner_points / corner_normals / polygon_centroids, but
            // those modes do not request a Standard variant pipeline
            // (their renderers swap in a fixed shader). If a future
            // pass adds uses_standard_variants under one of those
            // modes, the prewarm-time gate above (uses_standard_variants
            // && vertex_format != nullptr) catches the new pipeline
            // even though primitive_mode below stays polygon_fill --
            // the variant key does not depend on primitive_mode.
            .primitive_mode           = erhe::primitive::Primitive_mode::polygon_fill
        };
        context.forward_renderer->prewarm_standard_variants(params);
    }

    const std::chrono::steady_clock::time_point t_after_forward = std::chrono::steady_clock::now();
    const std::size_t after_forward = context.standard_shader_variants->size();

    // Shadow depth pass: drive the per-(vertex_input_state, reverse_depth)
    // Lazy_render_pipeline + the per-light render-pass formats so
    // vkCreateGraphicsPipelines for each shadow-atlas slot runs at init.
    // The shadow shader stages are eagerly compiled (Reloadable_shader_stages)
    // so this is purely the Vulkan-pipeline phase, not a shader-module
    // phase. Goes through every active Shadow_render_node owned by
    // App_rendering -- one per Scene_view that has its shadow atlas
    // configured.
    std::size_t shadow_node_count = 0;
    if (context.shadow_renderer != nullptr) {
        for (const std::shared_ptr<Shadow_render_node>& shadow_node : context.app_rendering->get_all_shadow_nodes()) {
            if (!shadow_node) {
                continue;
            }
            const std::span<const std::unique_ptr<erhe::graphics::Render_pass>> render_passes = shadow_node->get_render_passes();
            if (render_passes.empty()) {
                continue;
            }
            context.shadow_renderer->prewarm_pipelines(
                &context.mesh_memory->vertex_input,
                render_passes,
                shadow_node->get_reverse_depth()
            );
            ++shadow_node_count;
        }
    }

    const std::chrono::steady_clock::time_point t_after_shadow = std::chrono::steady_clock::now();
    const std::size_t after_shadow = context.standard_shader_variants->size();

    // Material- and brush-preview render paths. Each preview owns its
    // own offscreen render target with its own scene_root + content
    // library; the variant cache is shared (single Standard_shader_variants
    // for the whole editor) so preview-specific (vertex_format,
    // light_count) combinations get prewarmed entries here. Both are
    // always single-view -- preview render targets are 2D textures, not
    // multiview swapchains.
    if (context.material_preview != nullptr) {
        context.material_preview->prewarm_variants(
            *context.forward_renderer,
            *context.standard_shader_variants,
            context.mesh_memory->vertex_format
        );
    }
    if (context.brush_preview != nullptr) {
        context.brush_preview->prewarm_variants(
            *context.forward_renderer,
            *context.standard_shader_variants,
            context.mesh_memory->vertex_format
        );
    }

    const std::chrono::steady_clock::time_point t_end = std::chrono::steady_clock::now();
    const std::size_t after_previews = context.standard_shader_variants->size();
    const auto ms_forward  = std::chrono::duration_cast<std::chrono::milliseconds>(t_after_forward - t0).count();
    const auto ms_shadow   = std::chrono::duration_cast<std::chrono::milliseconds>(t_after_shadow  - t_after_forward).count();
    const auto ms_previews = std::chrono::duration_cast<std::chrono::milliseconds>(t_end           - t_after_shadow).count();
    log_startup->info(
        "prewarm: forward+content-library compiled {} new variants ({} total) in {} ms; "
        "shadow prewarm walked {} node(s) in {} ms; "
        "previews compiled {} new variants ({} total) in {} ms",
        after_forward  - before,          after_forward,  ms_forward,
        shadow_node_count,                                ms_shadow,
        after_previews - after_shadow,    after_previews, ms_previews
    );
}

} // namespace editor
