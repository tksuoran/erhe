#include "windows/lightmap_window.hpp"

#include "app_context.hpp"
#include "app_settings.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "items.hpp"
#include "operations/geometry_operations.hpp"
#include "operations/operation_stack.hpp"
#include "renderers/lightmap_baker.hpp"

#include "erhe_scene_renderer/forward_renderer.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_item/item.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

#include <imgui/imgui.h>

#include <algorithm>

namespace editor {

namespace {

// Lightmapped, non-skinned content mesh nodes of the active scene, as
// operation items (skinned meshes are excluded from baking - they have no
// static BLAS either).
[[nodiscard]] auto collect_lightmapped_mesh_nodes(App_context& context) -> std::vector<std::shared_ptr<erhe::Item_base>>
{
    std::vector<std::shared_ptr<erhe::Item_base>> items;
    const std::shared_ptr<Scene_root> scene_root = context.selection->get_active_scene_root();
    if (!scene_root) {
        return items;
    }
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : scene_root->layers().content()->meshes) {
        if (!mesh || mesh->skin) {
            continue;
        }
        if ((mesh->get_flag_bits() & erhe::Item_flags::lightmapped) == 0u) {
            continue;
        }
        erhe::scene::Node* const node = mesh->get_node();
        if (node == nullptr) {
            continue;
        }
        std::shared_ptr<erhe::Item_base> item = std::dynamic_pointer_cast<erhe::Item_base>(node->shared_from_this());
        if (item) {
            items.push_back(std::move(item));
        }
    }
    return items;
}

} // anonymous namespace

Lightmap_window::Lightmap_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 app_context
)
    : Imgui_window{imgui_renderer, imgui_windows, "Lightmap", "lightmap", true}
    , m_context   {app_context}
{
}

void Lightmap_window::generate_lightmap_uvs()
{
    queue_generate_lightmap_uvs({});
}

auto Lightmap_window::reorder_charts_by_bake() -> bool
{
    if (m_context.lightmap_baker == nullptr) {
        return false;
    }
    std::unordered_map<const erhe::geometry::Geometry*, std::vector<float>> keys = m_context.lightmap_baker->build_chart_order_keys();
    if (keys.empty()) {
        return false; // no bake yet - nothing to order by
    }
    if (!queue_generate_lightmap_uvs(std::move(keys))) {
        return false;
    }
    // Without the interactive bake nothing would rebake after the primitive
    // swap and the new UVs would sample the stale atlas (black / garbled).
    m_context.lightmap_baker->set_baking_enabled(true);
    return true;
}

void Lightmap_window::update()
{
    if (!m_reorder_requested) {
        return;
    }
    // Wait for in-flight operations first (matching the button's disabled
    // state; the request may have been set the frame before ops appeared).
    const std::size_t in_flight =
        static_cast<std::size_t>(m_context.pending_async_ops.load()) +
        static_cast<std::size_t>(m_context.running_async_ops.load()) +
        ((m_context.operation_stack != nullptr) ? m_context.operation_stack->get_queued_count() : 0u);
    if (in_flight > 0) {
        return; // retry next frame
    }
    m_reorder_requested = false;
    reorder_charts_by_bake();
}

auto Lightmap_window::queue_generate_lightmap_uvs(std::unordered_map<const erhe::geometry::Geometry*, std::vector<float>>&& per_facet_chart_order) -> bool
{
    const std::vector<std::shared_ptr<erhe::Item_base>> items = collect_lightmapped_mesh_nodes(m_context);
    if (items.empty()) {
        return false;
    }
    const Lightmap_config& config = m_context.editor_settings->lightmap;
    const float hard_angles_deg  = config.hard_angles_deg;
    const float texels_per_meter = config.texels_per_meter;
    const float gutter_texels    = config.uv_gutter_texels;
    const float min_chart_texels = config.uv_min_chart_texels;
    const auto  parameterizer    = static_cast<erhe::geometry::operation::Atlas_parameterizer>(
        std::clamp(config.uv_parameterizer, 0, 4)
    );
    const auto  packer           = static_cast<erhe::geometry::operation::Atlas_packer>(
        std::clamp(config.uv_packer, 0, 2)
    );
    async_for_nodes_with_mesh(
        m_context,
        items,
        [this, hard_angles_deg, texels_per_meter, gutter_texels, min_chart_texels, parameterizer, packer, per_facet_chart_order = std::move(per_facet_chart_order)](Mesh_operation_parameters&& params) {
            // Runs on a tf::Executor worker: queue() is main-thread-only.
            m_context.operation_stack->queue_from_thread(
                std::make_shared<Make_atlas_operation>(
                    std::move(params),
                    2, // lightmap UV channel (texcoord usage_index 2)
                    hard_angles_deg,
                    parameterizer,
                    packer,
                    texels_per_meter, // density-aware chart gutters
                    gutter_texels,
                    min_chart_texels,
                    per_facet_chart_order
                )
            );
        }
    );
    return true;
}

void Lightmap_window::imgui()
{
    Lightmap_config& config = m_context.editor_settings->lightmap;

    // UV unwrap and atlas layout are CPU-only and stay usable; everything
    // that shoots rays (bakes and their tunables) is held when the baker's
    // ray query pipeline is unavailable.
    const bool bake_supported = (m_context.lightmap_baker != nullptr) && m_context.lightmap_baker->is_bake_supported();
    if (!bake_supported) {
        const bool capture_layer =
            (m_context.graphics_device != nullptr) &&
            m_context.graphics_device->get_info().ray_query_disabled_by_capture_layer;
        if (capture_layer) {
            ImGui::TextColored(
                ImVec4{1.0f, 0.8f, 0.2f, 1.0f},
                "Baking disabled: Xcode GPU frame-capture layer is loaded (no GPU ray tracing)."
            );
            ImGui::TextUnformatted(
                "The capture layer crashes Metal acceleration structure builds.\n"
                "Fix: Edit Scheme > Run > Options > GPU Frame Capture = Disabled, then relaunch.\n"
                "UV generation and atlas layout still work."
            );
        } else {
            ImGui::TextColored(
                ImVec4{1.0f, 0.8f, 0.2f, 1.0f},
                "Baking disabled: GPU ray tracing is not supported by this device / backend."
            );
        }
        ImGui::Separator();
    }

    const std::vector<std::shared_ptr<erhe::Item_base>> lightmapped = collect_lightmapped_mesh_nodes(m_context);
    ImGui::Text("Lightmapped meshes in active scene: %zu", lightmapped.size());
    if (lightmapped.empty()) {
        ImGui::TextUnformatted("Enable the \"Lightmapped\" flag on content meshes (Properties window) to include them.");
    }

    if (ImGui::DragFloat("Texels per meter", &config.texels_per_meter, 0.5f, 1.0f, 256.0f, "%.1f")) {
        m_context.app_settings->settings_store().touch();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Lightmap texel density; sets each instance's atlas region size. The one quality knob.");
    }

    // Unwrap method knobs (doc/geogram_atlas_packing_feature_request.md):
    // exposed so unwrap defects (overlapping / folded UV triangles, see the
    // Lightmap Texture window's overlap check) can be iterated on live.
    {
        const char* const parameterizer_names[] = { "Projection", "LSCM", "Spectral LSCM", "ABF++", "Per-facet" };
        const char* const packer_names[]        = { "None", "Tetris", "xatlas" };
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::Combo("UV parameterizer", &config.uv_parameterizer, parameterizer_names, IM_ARRAYSIZE(parameterizer_names))) {
            m_context.app_settings->settings_store().touch();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Chart parameterizer for Generate Lightmap UVs. ABF++ is the Geogram default.\n"
                "Per-facet: every facet is its own chart (no Geogram; zero overlaps by construction,\n"
                "no shared texels - doc/lightmap_seam_driven_unwrap_plan.md first pass)."
            );
        }
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::Combo("UV packer", &config.uv_packer, packer_names, IM_ARRAYSIZE(packer_names))) {
            m_context.app_settings->settings_store().touch();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Geogram chart packer; with texel density > 0 erhe repacks charts itself,\nbut the packer still affects chart normalization.");
        }
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::DragFloat("Chart gutter (texels)", &config.uv_gutter_texels, 0.25f, 0.0f, 16.0f, "%.2f")) {
            m_context.app_settings->settings_store().touch();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Minimum empty space between charts, in texels at the expected density (erhe's own packing).");
        }
        // Leak condition: filter taps reach outside the chart (bilinear 1
        // texel, bicubic 2); each chart owns only half the gutter, so the
        // gutter must be at least twice the filter reach.
        const float required_gutter = config.bicubic_sampling ? 4.0f : 2.0f;
        if (config.uv_gutter_texels < required_gutter) {
            ImGui::TextColored(
                ImVec4{1.0f, 0.8f, 0.2f, 1.0f},
                "Gutter %.1f < %.0f texels: %s taps will read the neighboring chart's light (cross-chart leak)",
                config.uv_gutter_texels,
                required_gutter,
                config.bicubic_sampling ? "bicubic" : "bilinear"
            );
        }
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::DragFloat("Min chart size (texels)", &config.uv_min_chart_texels, 0.25f, 0.0f, 16.0f, "%.2f")) {
            m_context.app_settings->settings_store().touch();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Charts smaller than this (shorter side, in texels at the expected density) are scaled up\n"
                "(capped 16x) so every chart contains at least one texel center and bakes valid data.\n"
                "0 disables. Matters most for per-facet unwraps of dense meshes."
            );
        }
    }

    // Stale-data guard: Generate Lightmap UVs is queued async, and even
    // after the worker finishes its operation still sits in the operation
    // stack until the main thread executes it. Acting on the layout or
    // baking in that window would consume the OLD UVs and leave stale
    // results; hold the downstream buttons until both drain.
    const std::size_t async_ops =
        static_cast<std::size_t>(m_context.pending_async_ops.load()) +
        static_cast<std::size_t>(m_context.running_async_ops.load()) +
        ((m_context.operation_stack != nullptr) ? m_context.operation_stack->get_queued_count() : 0u);
    const bool async_busy = async_ops > 0;
    if (async_busy) {
        ImGui::TextColored(ImVec4{1.0f, 0.8f, 0.2f, 1.0f}, "Operations in flight: %zu (UV generation?) - layout / bake disabled", async_ops);
    }

    ImGui::BeginDisabled(lightmapped.empty() || async_busy);
    if (ImGui::Button("Generate Lightmap UVs")) {
        generate_lightmap_uvs();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Automatic UV unwrap into texcoord channel 2 for every lightmapped mesh (method: UV parameterizer above).\n"
            "Undoable. Inspect with the Lightmap Texture window or Scene View Config > Shader Debug > TexCoord 2 (Lightmap)."
        );
    }

    if (m_context.lightmap_baker != nullptr) {
        ImGui::SameLine();
        ImGui::BeginDisabled(async_busy);
        if (ImGui::Button("Update Atlas Layout")) {
            const std::shared_ptr<Scene_root> scene_root = m_context.selection->get_active_scene_root();
            if (scene_root) {
                m_context.lightmap_baker->update_layout(*scene_root.get(), config.texels_per_meter, config.uv_min_chart_texels);
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Pack every lightmapped primitive with channel-2 UVs into the shared atlas page.");
        }
        ImGui::EndDisabled(); // async_busy (Update Atlas Layout)
        const Lightmap_baker::Atlas_layout& layout = m_context.lightmap_baker->get_layout();
        if (layout.width > 0) {
            ImGui::BeginDisabled(async_busy || !bake_supported);
            if (ImGui::Button("Bake Direct Lighting")) {
                const std::shared_ptr<Scene_root> scene_root = m_context.selection->get_active_scene_root();
                if (scene_root && m_context.lightmap_baker->bake_gbuffer() && m_context.lightmap_baker->bake_direct(*scene_root.get())) {
                    if (m_context.forward_renderer != nullptr) {
                        m_context.forward_renderer->set_lightmap_texture(m_context.lightmap_baker->get_lightmap_texture());
                    }
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Rasterize the texel G-buffer, then bake direct lighting (ray-query shadow rays)\n"
                    "into the lightmap atlas. Lightmapped meshes sample it in place of ambient light."
                );
            }
            // Per-facet mode only: chart order keys are indexed by facet id.
            ImGui::SameLine();
            ImGui::BeginDisabled(config.uv_parameterizer != 4);
            if (ImGui::Button("Reorder Charts By Bake")) {
                // Deferred to Lightmap_window::update() at a safe point in
                // the frame - the readback must not run mid-ImGui.
                m_reorder_requested = true;
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Leak camouflage (per-facet mode, needs a bake): re-unwrap with charts packed in\n"
                    "baked-luminance order, so similarly lit facets are atlas neighbors and cross-chart\n"
                    "filter-tap / dilation pollution picks up similar values. Rebakes automatically."
                );
            }
            ImGui::EndDisabled(); // async_busy || !bake_supported (Bake Direct Lighting)
            ImGui::Text("Atlas: %d x %d, %zu regions", layout.width, layout.height, layout.regions.size());
            if (ImGui::TreeNode("Regions")) {
                for (const Lightmap_baker::Instance_region& region : layout.regions) {
                    ImGui::Text(
                        "%s[%zu]: %d x %d at (%d, %d), %.2f m^2, UV coverage %.0f%%",
                        region.mesh ? region.mesh->get_name().c_str() : "<gone>",
                        region.primitive_index,
                        region.width, region.height,
                        region.x, region.y,
                        region.world_area,
                        100.0f * region.uv_coverage
                    );
                }
                ImGui::TreePop();
            }
        }
    }

    // Interactive bake (plan section 3a): while on, the editor tick records
    // a budgeted gather slice + publish into every frame.
    if (m_context.lightmap_baker != nullptr) {
        ImGui::BeginDisabled(!bake_supported);
        bool baking = m_context.lightmap_baker->is_baking_enabled();
        if (ImGui::Checkbox("Baking", &baking)) {
            m_context.lightmap_baker->set_baking_enabled(baking);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Interactive progressive bake: direct light + indirect bounces accumulate\n"
                "across frames while you edit; light/mesh edits restart convergence."
            );
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            m_context.lightmap_baker->request_reset();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Restart accumulation (keeps atlas layout and G-buffer).");
        }
        if (m_context.lightmap_baker->is_baking_enabled()) {
            const int cell_count = m_context.lightmap_baker->get_layout().get_cell_count();
            if (cell_count > 1) {
                ImGui::Text(
                    "Sweeps: %u (tile %d/%d, row %d)",
                    m_context.lightmap_baker->get_sweep_count(),
                    m_context.lightmap_baker->get_cursor_cell(),
                    cell_count,
                    m_context.lightmap_baker->get_cursor_row()
                );
            } else {
                ImGui::Text(
                    "Sweeps: %u (row %d)",
                    m_context.lightmap_baker->get_sweep_count(),
                    m_context.lightmap_baker->get_cursor_row()
                );
            }
        }
        // Camera clamp for tiled atlases: pages larger than one tile cell
        // gather only the N nearest cells; the rest keep their last publish
        // and release their accumulation memory.
        if (m_context.lightmap_baker->get_layout().get_cell_count() > 1) {
            ImGui::SetNextItemWidth(120.0f);
            ImGui::DragInt("Active tiles", &config.active_tile_budget, 0.1f, 0, m_context.lightmap_baker->get_layout().get_cell_count());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Bake only the N tile cells nearest the viewport camera (0 = all).\n"
                    "Far tiles keep showing their last published lighting and free\n"
                    "their accumulation memory."
                );
            }
        }
        ImGui::EndDisabled(); // !bake_supported (interactive bake)
    }

    // Optional features (all on by default; off = A/B comparison and
    // debugging). The baker picks the changes up through
    // Lightmap_baker::set_options, which handles the required invalidation;
    // bicubic sampling is a pure viewport toggle.
    ImGui::SeparatorText("Features");
    ImGui::BeginDisabled(!bake_supported);
    bool touched = false;
    touched |= ImGui::Checkbox("Indirect bounce", &config.indirect_bounce);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("One cosine-weighted hemisphere bounce ray per sample; off = pure direct lighting.\nToggling restarts accumulation.");
    }
    touched |= ImGui::Checkbox("Terminator fix", &config.terminator_fix);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Phong-tessellated smooth sample positions (shadow-terminator fix).\nToggling re-rasters the G-buffer and restarts accumulation.");
    }
    {
        const char* const supersample_names[] = { "Off", "16 points (4x4)", "64 points (8x8)" };
        ImGui::SetNextItemWidth(140.0f);
        touched |= ImGui::Combo("Supersampled ray origins", &config.supersample_points, supersample_names, IM_ARRAYSIZE(supersample_names));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Frostbite Flux texel supersampling: regular grid of sample points per texel;\n"
                "every shadow/bounce ray starts from a uniform-randomly picked valid point instead\n"
                "of one fixed origin per texel. Integrates partial-coverage texels and softens\n"
                "direct-shadow aliasing. 64 points is the Flux default; 16 halves the cost of the\n"
                "page-sized RGBA32F origin target (grid-side x resolution per axis while baking).\n"
                "Changing re-rasters the G-buffer and restarts accumulation."
            );
        }
    }
    {
        const char* const coverage_names[] = { "Conservative raster", "9-tap jitter", "25-tap jitter" };
        ImGui::SetNextItemWidth(140.0f);
        touched |= ImGui::Combo("Texel coverage", &config.coverage_mode, coverage_names, IM_ARRAYSIZE(coverage_names));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "G-buffer texel coverage strategy: native conservative rasterization (one pass),\n"
                "or re-render each region with sub-texel jitter offsets spanning half a texel\n"
                "(9 = 3x3 grid, 25 = 5x5 grid; denser edge coverage, slower G-buffer bake).\n"
                "Changing re-rasters the G-buffer and restarts accumulation."
            );
        }
        const bool conservative_supported =
            (m_context.graphics_device != nullptr) &&
            m_context.graphics_device->get_info().use_conservative_rasterization;
        if ((config.coverage_mode == 0) && !conservative_supported) {
            ImGui::TextColored(
                ImVec4{1.0f, 0.8f, 0.2f, 1.0f},
                "Conservative rasterization not supported by this device - falling back to 9-tap jitter"
            );
        }
    }
    touched |= ImGui::Checkbox("Denoise (JNLM)", &config.denoise);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Joint non-local means denoise of the published atlas at each per-sweep publish.");
    }
    touched |= ImGui::Checkbox("Dilation", &config.dilation);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Flood valid texels into chart padding at publish so filtering never reads unbaked (black) texels.");
    }
    touched |= ImGui::Checkbox("Seam blend", &config.seam_blend);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Blend both sides of every UV seam edge toward each other at publish.");
    }
    touched |= ImGui::Checkbox("Bicubic sampling", &config.bicubic_sampling);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Viewport lightmap filtering: cubic B-spline reconstruction instead of bilinear.\nApplies immediately; no rebake needed.");
    }
    ImGui::EndDisabled(); // !bake_supported (Features)
    if (touched) {
        m_context.app_settings->settings_store().touch();
    }
}

} // namespace editor
