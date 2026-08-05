#include "windows/lightmap_window.hpp"

#include "app_context.hpp"
#include "app_settings.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "editor_log.hpp"
#include "items.hpp"
#include "operations/geometry_operations.hpp"
#include "operations/operation_stack.hpp"
#include "renderers/lightmap_baker.hpp"
#include "renderers/lightmap_partitioner.hpp"
#include "renderers/lightmap_report.hpp"
#include "renderers/lightmap_streamer.hpp"
#include "renderers/lightmap_tile_io.hpp"

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
#include <span>

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

// Manifest for the CURRENT layout: tile bounds/density/payload names plus
// the region identity table (world-space pieces persist the SOURCE mesh
// identity + (tile, piece_ordinal); they are re-created deterministically by
// Prepare World-Space Tiles, so the streamer resolves them through the live
// partition - the piece node names never hit the manifest). Shared by the
// batch bake and the incremental save paths.
[[nodiscard]] auto build_tile_manifest(
    App_context&                        context,
    const Lightmap_baker::Atlas_layout& layout,
    const Lightmap_config&              config
) -> std::shared_ptr<Lightmap_tile_io::Manifest>
{
    auto manifest = std::make_shared<Lightmap_tile_io::Manifest>();
    manifest->tile_size        = layout.get_tile_size();
    manifest->texels_per_meter = config.texels_per_meter;
    manifest->bake_hash        = (context.lightmap_baker != nullptr)
        ? context.lightmap_baker->get_bake_parameters_hash(config.texels_per_meter)
        : 0u;
    manifest->tiles.resize(static_cast<std::size_t>(layout.get_tile_count()));
    for (int tile = 0; tile < layout.get_tile_count(); ++tile) {
        Lightmap_tile_io::Tile_entry& entry = manifest->tiles[static_cast<std::size_t>(tile)];
        const Lightmap_baker::Tile& layout_tile = layout.tiles[static_cast<std::size_t>(tile)];
        entry.id            = tile;
        entry.bounds_min    = layout_tile.world_bounds.min;
        entry.bounds_max    = layout_tile.world_bounds.max;
        entry.density_scale = layout_tile.density_scale;
        entry.payload       = Lightmap_tile_io::payload_name(tile);
    }
    for (const Lightmap_baker::Instance_region& region : layout.regions) {
        if (!region.mesh || (region.tile < 0) || (region.tile >= layout.get_tile_count())) {
            continue;
        }
        const erhe::scene::Mesh* identity_mesh = region.mesh.get();
        if ((region.piece_ordinal >= 0) && (context.lightmap_partitioner != nullptr)) {
            for (const Lightmap_partitioner::Original_entry& entry : context.lightmap_partitioner->get_entries()) {
                if (entry.piece_mesh == region.mesh) {
                    identity_mesh = entry.original_mesh.get();
                    break;
                }
            }
        }
        manifest->tiles[static_cast<std::size_t>(region.tile)].regions.push_back(
            Lightmap_tile_io::Region_entry{
                .node_path       = Lightmap_tile_io::node_path(identity_mesh->get_node()),
                .node_index_path = Lightmap_tile_io::node_index_path(identity_mesh->get_node()),
                .mesh_name       = identity_mesh->get_name(),
                .primitive_index = (region.piece_ordinal >= 0) ? region.source_primitive_index : region.primitive_index,
                .piece_ordinal   = region.piece_ordinal,
                .uv_scale_offset = region.uv_scale_offset // tile-local
            }
        );
    }
    return manifest;
}

// Writes one tile's payload and rewrites the manifest, so any interruption
// leaves a consistent set on disk. Errors land in the Problems list.
[[nodiscard]] auto persist_tile_payload(
    const std::filesystem::path&      directory,
    const Lightmap_tile_io::Manifest& manifest,
    const int                         tile,
    const int                         width,
    const int                         height,
    std::span<const uint16_t>         rgba16,
    Lightmap_report* const            report
) -> bool
{
    std::string error;
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        if (report != nullptr) {
            report->add_error(Lightmap_report::Stage::persist, directory.string(), ec.message());
        }
        return false;
    }
    const Lightmap_tile_io::Tile_entry& entry = manifest.tiles[static_cast<std::size_t>(tile)];
    const bool payload_ok = Lightmap_tile_io::write_tile_payload(
        directory / entry.payload,
        width,
        height,
        rgba16,
        manifest.bake_hash,
        entry.bounds_min,
        entry.bounds_max,
        &error
    );
    if (!payload_ok) {
        if (report != nullptr) {
            report->add_error(Lightmap_report::Stage::persist, entry.payload, error);
        }
        return false;
    }
    if (!Lightmap_tile_io::write_manifest(directory, manifest, &error)) {
        if (report != nullptr) {
            report->add_error(Lightmap_report::Stage::persist, "manifest.json", error);
        }
        return false;
    }
    return true;
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
    // Deliberately does NOT start the interactive bake: until the user
    // bakes again the new UVs sample the stale atlas (black / garbled),
    // which is the expected "needs a rebake" state after a reorder.
    return true;
}

auto Lightmap_window::start_bake_to_disk() -> bool
{
    if (m_context.lightmap_baker == nullptr) {
        return false;
    }
    Lightmap_baker& baker = *m_context.lightmap_baker;
    if (baker.is_offline_bake_active()) {
        return false;
    }
    const std::shared_ptr<Scene_root> scene_root = m_context.selection->get_active_scene_root();
    if (!scene_root) {
        return false;
    }
    const Lightmap_config& config = m_context.editor_settings->lightmap;
    baker.set_tile_config(config.tile_texture_size, config.resident_tile_budget);
    if (baker.get_layout().get_tile_count() == 0) {
        if (!baker.update_layout(*scene_root.get(), config.texels_per_meter, config.uv_min_chart_texels)) {
            return false;
        }
    }
    const Lightmap_baker::Atlas_layout& layout = baker.get_layout();
    const std::filesystem::path directory =
        Lightmap_tile_io::directory_for_scene(scene_root->get_source_path());

    // Full manifest up front (the layout is fixed for the whole bake); the
    // sink persists each tile as it completes, so an interrupted bake
    // leaves a consistent set on disk.
    std::shared_ptr<Lightmap_tile_io::Manifest> manifest = build_tile_manifest(m_context, layout, config);

    Lightmap_report* const report = m_context.lightmap_report;
    if (report != nullptr) {
        report->clear_stage(Lightmap_report::Stage::bake);
        report->clear_stage(Lightmap_report::Stage::persist);
    }
    return baker.start_offline_bake(
        *scene_root.get(),
        static_cast<uint32_t>(std::max(1, config.offline_sweeps)),
        [directory, manifest, report](
            const int                    tile,
            const int                    width,
            const int                    height,
            const std::vector<uint16_t>& rgba16
        ) -> bool {
            return persist_tile_payload(
                directory,
                *manifest,
                tile,
                width,
                height,
                std::span<const uint16_t>{rgba16.data(), rgba16.size()},
                report
            );
        }
    );
}

auto Lightmap_window::save_tile_to_disk(const int tile) -> bool
{
    if (m_context.lightmap_baker == nullptr) {
        return false;
    }
    Lightmap_baker& baker = *m_context.lightmap_baker;
    const Lightmap_baker::Atlas_layout& layout = baker.get_layout();
    if ((tile < 0) || (tile >= layout.get_tile_count())) {
        return false;
    }
    const std::shared_ptr<Scene_root> scene_root = m_context.selection->get_active_scene_root();
    if (!scene_root) {
        return false;
    }
    std::vector<uint16_t> rgba16;
    if (!baker.read_back_tile(tile, rgba16)) {
        return false; // not resident / not published - nothing to save
    }
    const Lightmap_config& config = m_context.editor_settings->lightmap;
    const std::filesystem::path directory =
        Lightmap_tile_io::directory_for_scene(scene_root->get_source_path());
    const std::shared_ptr<Lightmap_tile_io::Manifest> manifest = build_tile_manifest(m_context, layout, config);
    const int tile_size = layout.get_tile_size();
    const bool saved = persist_tile_payload(
        directory,
        *manifest,
        tile,
        tile_size,
        tile_size,
        std::span<const uint16_t>{rgba16.data(), rgba16.size()},
        m_context.lightmap_report
    );
    if (saved) {
        baker.mark_tile_saved(tile);
        // The disk set changed; make the streamer re-read the manifest the
        // next time it owns the lightmap binding.
        if (m_context.lightmap_streamer != nullptr) {
            m_context.lightmap_streamer->invalidate();
        }
        log_render->info("Lightmap_window: tile {} saved to {}", tile, directory.string());
    }
    return saved;
}

auto Lightmap_window::save_all_tiles() -> std::size_t
{
    if (m_context.lightmap_baker == nullptr) {
        return 0;
    }
    const int tile_count = m_context.lightmap_baker->get_layout().get_tile_count();
    std::size_t saved = 0;
    for (int tile = 0; tile < tile_count; ++tile) {
        // save_tile_to_disk() quietly skips tiles with no resident
        // published content (there is nothing in memory to persist for
        // them - bake or batch-process to fill those).
        if (save_tile_to_disk(tile)) {
            ++saved;
        }
    }
    log_render->info("Lightmap_window: Save All Tiles saved {} of {} tiles", saved, tile_count);
    return saved;
}

void Lightmap_window::update()
{
    // Offline bake-to-disk: one tile per frame; each call blocks for that
    // tile's full bake (standalone submits), the UI stays live between
    // tiles.
    if ((m_context.lightmap_baker != nullptr) && m_context.lightmap_baker->is_offline_bake_active()) {
        const std::shared_ptr<Scene_root> scene_root = m_context.selection->get_active_scene_root();
        if (scene_root) {
            m_context.lightmap_baker->offline_tick(*scene_root.get());
        } else {
            m_context.lightmap_baker->cancel_offline_bake();
        }
    }

    // Save-on-evict drain: the residency swap parks tiles with unsaved
    // published content instead of dropping them (see
    // Lightmap_baker::set_save_on_evict); persist them here - a safe point
    // in the frame for standalone readback submits - then release them for
    // eviction. A failed save is released too (the error is already in
    // Problems); holding the slot forever would deadlock residency.
    if (m_context.lightmap_baker != nullptr) {
        for (;;) {
            const int tile = m_context.lightmap_baker->take_tile_pending_save();
            if (tile < 0) {
                break;
            }
            save_tile_to_disk(tile);
            m_context.lightmap_baker->mark_tile_saved(tile);
        }
    }

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
    // Fresh run: the report should reflect this generation, not stale ones.
    if (m_context.lightmap_report != nullptr) {
        m_context.lightmap_report->clear_stage(Lightmap_report::Stage::uv_unwrap);
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
    {
        // Tile texture size: power-of-two combo (the baker clamps anyway).
        const char* const tile_size_names[]  = { "256", "512", "1024", "2048", "4096", "8192" };
        const int         tile_size_values[] = {  256,   512,   1024,   2048,   4096,   8192  };
        int tile_size_index = 3;
        for (int i = 0; i < IM_ARRAYSIZE(tile_size_values); ++i) {
            if (config.tile_texture_size == tile_size_values[i]) {
                tile_size_index = i;
                break;
            }
        }
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::Combo("Tile texture size", &tile_size_index, tile_size_names, IM_ARRAYSIZE(tile_size_names))) {
            config.tile_texture_size = tile_size_values[tile_size_index];
            m_context.app_settings->settings_store().touch();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Texel side of one spatial lightmap tile. The world partitions into tiles whose\n"
                "content fits this texture at the requested density (spatial tile extents adapt;\n"
                "texel density flexes down only as a last resort), so layout always succeeds.\n"
                "Takes effect on the next Update Atlas Layout."
            );
        }
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::DragInt("Resident tiles", &config.resident_tile_budget, 0.1f, 1, 64)) {
            m_context.app_settings->settings_store().touch();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "How many tiles may be GPU-resident at once (9 = 3x3 around the camera).\n"
                "The nearest tiles stream in / bake; the rest render unlit. Bounds total\n"
                "lightmap memory regardless of world size."
            );
        }
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

    // Failures / warnings from every pipeline stage (UV unwrap runs on
    // worker threads and used to fail silently into the log). Newest last.
    if ((m_context.lightmap_report != nullptr) && !m_context.lightmap_report->empty()) {
        const std::vector<Lightmap_report::Entry> entries = m_context.lightmap_report->snapshot();
        std::size_t error_count   = 0;
        std::size_t warning_count = 0;
        for (const Lightmap_report::Entry& entry : entries) {
            if (entry.is_warning) {
                ++warning_count;
            } else {
                ++error_count;
            }
        }
        ImGui::SeparatorText("Problems");
        ImGui::Text("%zu error(s), %zu warning(s)", error_count, warning_count);
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) {
            m_context.lightmap_report->clear();
        }
        for (const Lightmap_report::Entry& entry : entries) {
            const ImVec4 color = entry.is_warning ? ImVec4{1.0f, 0.8f, 0.2f, 1.0f} : ImVec4{1.0f, 0.35f, 0.3f, 1.0f};
            ImGui::TextColored(
                color,
                "[%s] %s: %s",
                Lightmap_report::c_str(entry.stage),
                entry.subject.c_str(),
                entry.message.c_str()
            );
        }
        ImGui::Separator();
    }

    if (m_context.lightmap_baker != nullptr) {
        ImGui::SameLine();
        ImGui::BeginDisabled(async_busy);
        if (ImGui::Button("Update Atlas Layout")) {
            const std::shared_ptr<Scene_root> scene_root = m_context.selection->get_active_scene_root();
            if (scene_root) {
                m_context.lightmap_baker->set_tile_config(config.tile_texture_size, config.resident_tile_budget);
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
                    "filter-tap / dilation pollution picks up similar values. Does not bake; the atlas\n"
                    "is stale until you bake again."
                );
            }
            ImGui::EndDisabled(); // async_busy || !bake_supported (Bake Direct Lighting)
            ImGui::Text(
                "Atlas: %d spatial tiles of %d^2, %zu regions, %d resident slots (%d x %d display)",
                layout.get_tile_count(), layout.get_tile_size(), layout.regions.size(),
                layout.get_slot_count(), layout.width, layout.height
            );
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
        // Bake mode: applies to every bake path (interactive, one-shot,
        // bake-to-disk) - the gather reads it each dispatch; changing it
        // restarts accumulation (Lightmap_baker::set_options).
        {
            const char* const bake_mode_names[] = { "Direct only", "Direct + indirect bounce" };
            int bake_mode = config.indirect_bounce ? 1 : 0;
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::Combo("Bake mode", &bake_mode, bake_mode_names, IM_ARRAYSIZE(bake_mode_names))) {
                config.indirect_bounce = (bake_mode == 1);
                m_context.app_settings->settings_store().touch();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Direct only: explicit light sampling with shadow rays.\n"
                    "Direct + indirect bounce: adds one cosine-weighted hemisphere bounce ray\n"
                    "per sample. Respected by every bake (interactive, one-shot, bake-to-disk);\n"
                    "changing it restarts accumulation."
                );
            }
        }
        // Play/stop toggle + state text for the interactive bake.
        const bool baking = m_context.lightmap_baker->is_baking_enabled();
        if (ImGui::Button(baking ? "Stop###bake_toggle" : "Start###bake_toggle")) {
            m_context.lightmap_baker->set_baking_enabled(!baking);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Interactive progressive bake: light accumulates across frames while you\n"
                "edit; light/mesh edits restart convergence."
            );
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            m_context.lightmap_baker->request_reset();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Restart accumulation (keeps atlas layout and G-buffer).");
        }
        ImGui::SameLine();
        if (baking) {
            ImGui::TextColored(ImVec4{0.4f, 0.9f, 0.4f, 1.0f}, "Baking");
        } else {
            ImGui::TextUnformatted("Stopped");
        }
        if (m_context.lightmap_baker->is_baking_enabled()) {
            const int tile_count = m_context.lightmap_baker->get_layout().get_tile_count();
            if (tile_count > 1) {
                ImGui::Text(
                    "Sweeps: %u (tile %d/%d, row %d)",
                    m_context.lightmap_baker->get_sweep_count(),
                    m_context.lightmap_baker->get_cursor_tile(),
                    tile_count,
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
        // Camera clamp for multi-tile layouts: gather only the N spatial
        // tiles nearest the viewport camera; the rest keep their last
        // publish and release their accumulation memory.
        if (m_context.lightmap_baker->get_layout().get_tile_count() > 1) {
            ImGui::SetNextItemWidth(120.0f);
            ImGui::DragInt("Active tiles", &config.active_tile_budget, 0.1f, 0, m_context.lightmap_baker->get_layout().get_tile_count());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Bake only the N spatial tiles nearest the viewport camera (0 = the whole\n"
                    "resident set). Far tiles keep showing their last published lighting and\n"
                    "free their accumulation memory."
                );
            }
        }
        ImGui::EndDisabled(); // !bake_supported (interactive bake)

        // Tile debugging: see and grab what the residency ranking picked.
        {
            const Lightmap_baker::Atlas_layout& layout = m_context.lightmap_baker->get_layout();
            bool show_tile_bounds = m_context.lightmap_baker->get_show_tile_bounds();
            if (ImGui::Checkbox("Show tile bounds", &show_tile_bounds)) {
                m_context.lightmap_baker->set_show_tile_bounds(show_tile_bounds);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Draw every spatial tile's world bounds as x-ray wireframe boxes in the\n"
                    "viewport: purple = all tiles, white = active (display-slot-holding) tiles."
                );
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(layout.regions.empty());
            if (ImGui::Button("Select Active Tile Meshes")) {
                std::vector<std::shared_ptr<erhe::Item_base>> items;
                for (const Lightmap_baker::Instance_region& region : layout.regions) {
                    if (!region.mesh || (region.tile < 0) || (region.tile >= layout.get_tile_count())) {
                        continue;
                    }
                    if (layout.tiles[static_cast<std::size_t>(region.tile)].slot < 0) {
                        continue;
                    }
                    erhe::scene::Node* const node = region.mesh->get_node();
                    if (node == nullptr) {
                        continue;
                    }
                    std::shared_ptr<erhe::Item_base> item = std::dynamic_pointer_cast<erhe::Item_base>(node->shared_from_this());
                    if (item && (std::find(items.begin(), items.end(), item) == items.end())) {
                        items.push_back(std::move(item));
                    }
                }
                m_context.selection->set_selection(items);
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Set the scene selection to every mesh node whose region lives in an active (slot-holding) tile.");
            }
        }
    }

    // World-space partition: make every lightmapped mesh/primitive instance
    // unique, bake its transform into world space and clip it against the
    // spatial tile tree; pieces render from the "Lightmap Pieces" group
    // with per-piece atlas mappings.
    if ((m_context.lightmap_baker != nullptr) && (m_context.lightmap_partitioner != nullptr)) {
        ImGui::SeparatorText("World-Space Tiles");
        Lightmap_partitioner& partitioner = *m_context.lightmap_partitioner;
        ImGui::BeginDisabled(async_busy);
        if (ImGui::Button("Prepare World-Space Tiles")) {
            const std::shared_ptr<Scene_root> scene_root = m_context.selection->get_active_scene_root();
            if (scene_root) {
                m_context.lightmap_baker->set_tile_config(config.tile_texture_size, config.resident_tile_budget);
                const bool prepared = partitioner.prepare(
                    *scene_root.get(),
                    Lightmap_partitioner::Params{
                        .texels_per_meter = config.texels_per_meter,
                        .min_face_texels  = config.uv_min_chart_texels,
                        .hard_angles_deg  = config.hard_angles_deg,
                        .gutter_texels    = config.uv_gutter_texels,
                        .min_chart_texels = config.uv_min_chart_texels,
                        .parameterizer    = static_cast<erhe::geometry::operation::Atlas_parameterizer>(std::clamp(config.uv_parameterizer, 0, 4)),
                        .packer           = static_cast<erhe::geometry::operation::Atlas_packer>(std::clamp(config.uv_packer, 0, 2))
                    }
                );
                if (prepared) {
                    partitioner.set_render_with_lightmaps(config.render_with_lightmaps);
                }
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Uniquify every lightmapped mesh instance, bake its transform into world space,\n"
                "clip it against the spatial tile planes (shared cut vertices are binary exact)\n"
                "and re-unwrap each piece. Originals stay in the scene for revert / re-prepare."
            );
        }
        if (partitioner.is_prepared()) {
            ImGui::SameLine();
            if (ImGui::Button("Revert Tiling")) {
                partitioner.revert();
            }
            if (partitioner.is_prepared()) {
                std::size_t piece_count = 0;
                for (const Lightmap_partitioner::Original_entry& entry : partitioner.get_entries()) {
                    piece_count += entry.pieces.size();
                }
                ImGui::Text(
                    "%zu meshes -> %zu pieces in %d tiles",
                    partitioner.get_entries().size(), piece_count, partitioner.get_tile_count()
                );
                const std::size_t stale_count = partitioner.count_stale_transforms();
                if (stale_count > 0) {
                    ImGui::TextColored(
                        ImVec4{1.0f, 0.8f, 0.2f, 1.0f},
                        "%zu source meshes moved since the clip - pieces are stale, re-prepare",
                        stale_count
                    );
                }
                if (ImGui::Checkbox("Render with lightmaps", &config.render_with_lightmaps)) {
                    partitioner.set_render_with_lightmaps(config.render_with_lightmaps);
                    m_context.app_settings->settings_store().touch();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "ON: the world-space piece meshes render (every lightmapped mesh, regardless\n"
                        "of which tiles are loaded; non-resident tiles fall back to white).\n"
                        "OFF: the original meshes render."
                    );
                }
            }
        }
        ImGui::EndDisabled();
    }

    // Tile persistence: the batch bake processes every spatial tile (not
    // just the resident ones) one tile at a time; Save All persists what
    // the interactive baker currently holds in memory. Evictions save
    // automatically (see Lightmap_window::update).
    if (m_context.lightmap_baker != nullptr) {
        ImGui::SeparatorText("Tile Persistence");
        ImGui::BeginDisabled(!bake_supported || async_busy);
        const bool offline_active = m_context.lightmap_baker->is_offline_bake_active();
        if (!offline_active) {
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::DragInt("Sweeps per tile", &config.offline_sweeps, 0.25f, 1, 1024)) {
                m_context.app_settings->settings_store().touch();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Accumulation sweeps gathered per tile before it is written to disk.");
            }
            if (ImGui::Button("Batch Process All Tiles")) {
                start_bake_to_disk();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Bake every spatial tile in turn (one tile per frame; each tile: G-buffer +\n"
                    "N gather sweeps + resolve) and write tile_<N>.lmt + manifest.json into\n"
                    "<scene>.lightmap/. Only one tile's working set is resident at a time, so any\n"
                    "world size bakes within the fixed memory budget."
                );
            }
            ImGui::SameLine();
            if (ImGui::Button("Save All Tiles")) {
                save_all_tiles();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Write every resident tile's CURRENT published lightmap (interactive bake\n"
                    "state) to <scene>.lightmap/ right now. Non-resident tiles have no content in\n"
                    "memory - use Batch Process All Tiles to bake and persist those. Evicted\n"
                    "tiles are saved automatically."
                );
            }
        } else {
            const Lightmap_baker::Offline_progress& progress = m_context.lightmap_baker->get_offline_progress();
            ImGui::Text("Baking tile %d/%d (%u sweeps per tile)", progress.tiles_done + 1, progress.tile_count, progress.target_sweeps);
            const float fraction = (progress.tile_count > 0)
                ? static_cast<float>(progress.tiles_done) / static_cast<float>(progress.tile_count)
                : 0.0f;
            ImGui::ProgressBar(fraction, ImVec2{-1.0f, 0.0f});
            if (ImGui::Button("Cancel")) {
                m_context.lightmap_baker->cancel_offline_bake();
            }
        }
        ImGui::EndDisabled();
    }

    // Optional features (all on by default; off = A/B comparison and
    // debugging). The baker picks the changes up through
    // Lightmap_baker::set_options, which handles the required invalidation;
    // bicubic sampling is a pure viewport toggle.
    ImGui::SeparatorText("Features");
    ImGui::BeginDisabled(!bake_supported);
    bool touched = false;
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
