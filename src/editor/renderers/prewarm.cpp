#include "renderers/prewarm.hpp"

#include "app_context.hpp"
#include "app_rendering.hpp"
#include "app_scenes.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "renderers/composition_pass.hpp"
#include "scene/scene_root.hpp"

#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/forward_renderer.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_scene_renderer/standard_shader_variant.hpp"
#include "erhe_scene_renderer/standard_shader_variants.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <span>
#include <vector>

namespace editor {

namespace {

// Mirrors the type-and-cast_shadow tally in Light_projections::apply
// (light_buffer.cpp around lines 184-203). The runtime feeds these six
// counts into compute_bucket_variant_key on each frame; reproducing the
// tally here lets the prewarm produce keys that hash to the same cache
// entries the runtime will consume.
[[nodiscard]] auto snapshot_light_counts(const erhe::scene::Light_layer& layer)
    -> erhe::scene_renderer::Standard_variant_light_counts
{
    using Light_type = erhe::scene::Light_type;

    auto type_index = [](const Light_type t) -> std::size_t {
        switch (t) {
            case Light_type::directional: return 0;
            case Light_type::spot:        return 1;
            case Light_type::point:       return 2;
            default:                      return 3;
        }
    };

    std::size_t per_type_shadow   [4] = {0, 0, 0, 0};
    std::size_t per_type_nonshadow[4] = {0, 0, 0, 0};
    for (const std::shared_ptr<erhe::scene::Light>& light : layer.lights) {
        if (!light) {
            continue;
        }
        const std::size_t t = type_index(light->type);
        if (light->cast_shadow) {
            ++per_type_shadow[t];
        } else {
            ++per_type_nonshadow[t];
        }
    }

    erhe::scene_renderer::Standard_variant_light_counts result{};
    result.directional_shadowmapped_count = static_cast<uint16_t>(per_type_shadow[0]);
    result.directional_light_count        = static_cast<uint16_t>(per_type_shadow[0] + per_type_nonshadow[0]);
    result.spot_shadowmapped_count        = static_cast<uint16_t>(per_type_shadow[1]);
    result.spot_light_count               = static_cast<uint16_t>(per_type_shadow[1] + per_type_nonshadow[1]);
    result.point_shadowmapped_count       = static_cast<uint16_t>(per_type_shadow[2]);
    result.point_light_count              = static_cast<uint16_t>(per_type_shadow[2] + per_type_nonshadow[2]);
    return result;
}

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

    // View counts to prewarm. OpenXR uses multiview view_count = 2;
    // single-view paths use 0. We prewarm both whenever OpenXR is on so
    // the desktop mirror window (single-view) and the headset (multiview)
    // both find their variants in cache.
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
            snapshot_light_counts(*light_layer);

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
            .primitive_mode           = erhe::primitive::Primitive_mode::polygon_fill
        };
        context.forward_renderer->prewarm_standard_variants(params);
    }

    const std::size_t after = context.standard_shader_variants->size();
    const std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    log_startup->info(
        "prewarm: forward+content-library compiled {} new variants ({} total) in {} ms",
        after - before, after, ms
    );
}

} // namespace editor
