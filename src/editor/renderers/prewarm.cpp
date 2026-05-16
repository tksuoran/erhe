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

#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/forward_renderer.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_scene_renderer/shadow_renderer.hpp"
//#include "erhe_scene_renderer/standard_shader_variant.hpp"
//#include "erhe_scene_renderer/standard_shader_variants.hpp"

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#   include "erhe_xr/headset.hpp"
#   include "erhe_xr/xr_session.hpp"
#endif

#include <algorithm>
#include <chrono>
#include <memory>
#include <span>
#include <vector>

namespace editor {

void prewarm_all(
    App_context&                                 /*context*/,
    const std::function<void(std::string_view)>& /*init_message*/
)
{
#if 0 // TODO This must be redone
    if ((context.forward_renderer == nullptr) ||
        (context.app_scenes == nullptr) ||
        (context.app_rendering == nullptr) ||
        (context.mesh_memory == nullptr))
    {
        return;
    }

    const std::size_t before = context.standard_shader_variants->size();
    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();

    // View counts to prewarm. OpenXR uses multiview view_count = N
    // (typically 2 for stereo, 4 for Varjo quad-view); single-view paths
    // use 0. We prewarm both whenever OpenXR is on so the desktop
    // mirror window (single-view) and the headset (multiview) both
    // find their variants in cache. The OpenXR view count is sourced
    // from Xr_session::get_view_count() (which reflects the runtime's
    // view-configuration view count), with 2 as a fall-back when XR is
    // selected but the session is not up yet -- that lower bound
    // matches the stereo case we ship most often.
    std::vector<uint32_t> view_counts;
    view_counts.push_back(0u);
#if defined(ERHE_XR_LIBRARY_OPENXR)
    if (context.OpenXR) {
        uint32_t xr_view_count = 2u;
        if ((context.headset_view != nullptr) && (context.headset_view->get_headset() != nullptr)) {
            erhe::xr::Xr_session* xr_session = context.headset_view->get_headset()->get_xr_session();
            if (xr_session != nullptr) {
                const uint32_t runtime_view_count = xr_session->get_view_count();
                if (runtime_view_count >= 2u) {
                    xr_view_count = runtime_view_count;
                }
            }
        }
        view_counts.push_back(xr_view_count);
    }
#endif

    // VkPipeline-cache warmup target for the OpenXR multiview color pass.
    // Quest 3's per-frame budget on a cold install is dominated by the
    // driver IR-optimization step inside vkCreateGraphicsPipelines for
    // the headset color pipelines (the application-level cache also
    // misses on the first bind, but that cost is small once the driver
    // cache has the binary). The real multiview render pass is built per
    // frame inside Xr_session::render_frame_multiview() so the
    // application-level cache cannot be populated here; the driver-level
    // VkPipelineCache (Device_impl::m_pipeline_cache) reuses the binary
    // across compatible render passes as long as the format/usage tuple
    // matches.
    //
    // Sample count is sourced from the OpenXR runtime
    // (recommendedSwapchainSampleCount via Xr_session). The swapchain
    // images this session created carry exactly that sample count and
    // the headset render pass inherits it; mirroring the value here
    // keeps the warmup target compatible with the runtime pass. Quest
    // reports 1, which means single-sample for that platform. Usage
    // flags mirror headset_view.cpp's per-frame render pass descriptor
    // exactly.
    std::vector<erhe::scene_renderer::Forward_renderer::Warmup_target> warmup_targets;
#if defined(ERHE_XR_LIBRARY_OPENXR)
    if (context.OpenXR && (context.headset_view != nullptr) && (context.headset_view->get_headset() != nullptr)) {
        erhe::xr::Headset*    headset    = context.headset_view->get_headset();
        erhe::xr::Xr_session* xr_session = headset->get_xr_session();
        if (xr_session != nullptr) {
            const erhe::dataformat::Format color_format         = xr_session->get_swapchain_color_format();
            const erhe::dataformat::Format depth_stencil_format = xr_session->get_swapchain_depth_stencil_format();
            if (color_format != erhe::dataformat::Format::format_undefined) {
                erhe::scene_renderer::Forward_renderer::Warmup_target target{};
                target.view_count               = xr_session->get_view_count();
                target.color_attachment_count   = 1u;
                target.color_attachment_formats[0] = color_format;
                target.color_usage_before[0]    = erhe::graphics::Image_usage_flag_bit_mask::color_attachment;
                target.color_usage_after[0]     = erhe::graphics::Image_usage_flag_bit_mask::color_attachment;
                target.depth_attachment_format  = depth_stencil_format;
                target.depth_usage_before       = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
                target.depth_usage_after        = erhe::graphics::Image_usage_flag_bit_mask::depth_stencil_attachment;
                target.sample_count             = xr_session->get_swapchain_sample_count();
                if (target.view_count >= 2u) {
                    warmup_targets.push_back(target);
                }
            }
        }
    }
#endif

    std::vector<erhe::graphics::Lazy_render_pipeline*> standard_pipelines =
        collect_standard_variant_pipelines(context.app_rendering->composition_passes());

    const std::vector<std::shared_ptr<Scene_root>>& scene_roots =
        context.app_scenes->get_scene_roots();

    std::size_t total_forward_warmups = 0;
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
            .mesh_memory              = *context.mesh_memory,
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
            .primitive_mode           = erhe::primitive::Primitive_mode::polygon_fill,
            .warmup_targets           = std::span<const erhe::scene_renderer::Forward_renderer::Warmup_target>{warmup_targets}
        };
        if (init_message) {
            init_message(scene_root->get_name());
        }
        total_forward_warmups += context.forward_renderer->prewarm_standard_variants(params);
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
                &context.mesh_memory->vertex_input(),
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
            *context.standard_shader_variants
        );
    }
    if (context.brush_preview != nullptr) {
        context.brush_preview->prewarm_variants(
            *context.forward_renderer,
            *context.standard_shader_variants
        );
    }

    const std::chrono::steady_clock::time_point t_end = std::chrono::steady_clock::now();
    const std::size_t after_previews = context.standard_shader_variants->size();
    const auto ms_forward  = std::chrono::duration_cast<std::chrono::milliseconds>(t_after_forward - t0).count();
    const auto ms_shadow   = std::chrono::duration_cast<std::chrono::milliseconds>(t_after_shadow  - t_after_forward).count();
    const auto ms_previews = std::chrono::duration_cast<std::chrono::milliseconds>(t_end           - t_after_shadow).count();
    log_startup->info(
        "prewarm: forward+content-library compiled {} new variants ({} total), warmed {} pipeline(s) in {} ms; "
        "shadow prewarm walked {} node(s) in {} ms; "
        "previews compiled {} new variants ({} total) in {} ms",
        after_forward  - before,          after_forward,  total_forward_warmups, ms_forward,
        shadow_node_count,                                ms_shadow,
        after_previews - after_shadow,    after_previews, ms_previews
    );
#endif
}

} // namespace editor
