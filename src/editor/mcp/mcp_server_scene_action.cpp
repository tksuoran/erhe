// Mcp_server scene action tools (select, transform, brush, shapes, nodes, lights, cameras, reparent, lock, tags).
// Split out of mcp_server.cpp; shares helpers via mcp_server_shared.hpp.

#include "mcp/mcp_server_shared.hpp"

#include "config/generated/editor_settings_config.hpp"
#include "items.hpp"
#include "operations/geometry_operations.hpp"
#include "operations/node_transform_operation.hpp"
#include "renderers/lightmap_baker.hpp"
#include "renderers/lightmap_partitioner.hpp"
#include "renderers/lightmap_tile_io.hpp"
#include "windows/lightmap_texture_window.hpp"
#include "windows/lightmap_window.hpp"
#include "windows/viewport_config_window.hpp"
#include "scene/generated/scene_settings_serialization.hpp"
#include "tools/clipboard.hpp"

#include "erhe_gltf/gltf_item_flags.hpp"
#include "erhe_scene_renderer/forward_renderer.hpp"

#include <simdjson.h>

#include <fmt/format.h>

#include <algorithm>
#include <iterator>

namespace editor {

using namespace mcp_server_detail;

auto Mcp_server::action_set_scene_settings(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }
    bool changed = false;
    if (args.contains("ambient_light")) {
        const json& value = args["ambient_light"];
        if (!value.is_array() || (value.size() < 3)) {
            json r = make_text_content("ambient_light must be an array of 3 or 4 numbers");
            r["isError"] = true;
            return r.dump();
        }
        sr->get_scene().ambient_light = glm::vec4{
            value[0].get<float>(),
            value[1].get<float>(),
            value[2].get<float>(),
            (value.size() >= 4) ? value[3].get<float>() : 0.0f
        };
        changed = true;
    }
    if (args.contains("settings")) {
        const json& value = args["settings"];
        if (!value.is_object()) {
            json r = make_text_content("settings must be an object (Scene_settings shape; {} resets every override)");
            r["isError"] = true;
            return r.dump();
        }
        // Loud reject of unversioned sub-configs: the codegen deserializer
        // treats a missing _version as version 1 and SILENTLY drops every
        // added_in > 1 field (e.g. a versionless sky object loses
        // "enabled"). Hand-written MCP input must say which version it is.
        for (const auto& [key, sub] : value.items()) {
            if (sub.is_object() && !sub.empty() && !sub.contains("_version")) {
                json r = make_text_content(
                    "settings." + key + " has no _version; a versionless object deserializes as "
                    "version 1 and silently drops newer fields - add the config's current _version"
                );
                r["isError"] = true;
                return r.dump();
            }
        }
        // Default: replace semantics - the whole Scene_settings is rebuilt
        // from the given object, so omitted fields return to "use the
        // editor-global default" ({} clears every override). With
        // merge: true the given fields are deep-merged (RFC 7386) over the
        // CURRENT settings instead: omitted fields keep their values and a
        // null deletes an override - no client-side accumulator needed.
        json settings_value = value;
        if (args.value("merge", false)) {
            json current = json::parse(serialize(sr->get_scene_settings(), 0), nullptr, false);
            if (current.is_object()) {
                current.merge_patch(value);
                settings_value = std::move(current);
            }
        }
        Scene_settings new_settings{};
        const std::string            settings_text = settings_value.dump();
        simdjson::ondemand::parser   settings_parser;
        simdjson::padded_string      settings_padded{settings_text};
        simdjson::ondemand::document settings_document;
        simdjson::ondemand::object   settings_object;
        const bool ok =
            (settings_parser.iterate(settings_padded).get(settings_document) == simdjson::SUCCESS) &&
            (settings_document.get_object().get(settings_object) == simdjson::SUCCESS) &&
            (deserialize(settings_object, new_settings) == simdjson::SUCCESS);
        if (!ok) {
            json r = make_text_content("settings did not deserialize as Scene_settings");
            r["isError"] = true;
            return r.dump();
        }
        sr->get_scene_settings() = new_settings;
        changed = true;
    }
    return make_json_content({
        {"updated",    changed},
        {"scene_name", sr->get_name()}
    }).dump();
}

auto Mcp_server::action_select_items(const json& args) -> std::string
{
    if (!m_context.selection) {
        json r = make_text_content("Selection system not available");
        r["isError"] = true;
        return r.dump();
    }

    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    const json ids_json = args.value("ids", json::array());
    std::set<std::size_t> target_ids;
    for (const auto& id_val : ids_json) {
        if (id_val.is_number_unsigned() || id_val.is_number_integer()) {
            target_ids.insert(id_val.get<std::size_t>());
        }
    }

    // Mirrors the UI's scoped selection semantics: selecting (or clearing)
    // in one scene leaves other scenes' selections untouched, and the
    // selection change makes the target scene the active scene.
    if (target_ids.empty()) {
        m_context.selection->clear_selection(static_cast<erhe::Item_host*>(sr));
        return make_text_content("Selection cleared in scene: " + sr->get_name()).dump();
    }

    auto items_to_select = find_items_by_ids(*sr, target_ids);
    {
        Scoped_selection_change selection_change{*m_context.selection};
        m_context.selection->clear_selection(static_cast<erhe::Item_host*>(sr));
        for (const std::shared_ptr<erhe::Item_base>& item : items_to_select) {
            m_context.selection->add_to_selection(item);
        }
    }
    // A UI click both selects and focuses the scene's window; the focus part
    // activates the scene even when the selection itself did not change
    // (re-selecting an already-selected item produces no selection diff).
    // Mirror that here so select_items always activates the target scene.
    m_context.selection->set_active_scene_root(sr->shared_from_this());

    json selected = json::array();
    for (const auto& item : items_to_select) {
        selected.push_back({
            {"name", item->get_name()},
            {"type", std::string{item->get_type_name()}},
            {"id",   item->get_id()}
        });
    }

    return make_json_content({
        {"selected_count", static_cast<int>(items_to_select.size())},
        {"items",          selected}
    }).dump();
}

auto Mcp_server::action_set_item_flags(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }
    const json ids_json = args.value("ids", json::array());
    std::set<std::size_t> target_ids;
    for (const auto& id_val : ids_json) {
        if (id_val.is_number_unsigned() || id_val.is_number_integer()) {
            target_ids.insert(id_val.get<std::size_t>());
        }
    }
    uint64_t flag_bits = 0;
    for (const auto& flag_val : args.value("flags", json::array())) {
        if (!flag_val.is_string()) {
            continue;
        }
        const uint64_t bit = erhe::gltf::persistent_item_flag_from_name(flag_val.get<std::string>());
        if (bit == 0) {
            json r = make_text_content("Unknown flag name: " + flag_val.get<std::string>());
            r["isError"] = true;
            return r.dump();
        }
        flag_bits |= bit;
    }
    if ((flag_bits == 0) || target_ids.empty()) {
        json r = make_text_content("Give ids and at least one persistent flag name (e.g. \"lightmapped\", \"shadow_cast\")");
        r["isError"] = true;
        return r.dump();
    }
    const bool enabled = args.value("enabled", true);

    json updated = json::array();
    for (const std::shared_ptr<erhe::Item_base>& item : find_items_by_ids(*sr, target_ids)) {
        // Mesh-scoped flags (lightmapped, shadow_cast) live on the Mesh
        // attachment; a Node id resolves to its mesh so callers can pass the
        // ids that get_scene_nodes returns.
        std::shared_ptr<erhe::Item_base> target = item;
        const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_mesh(item);
        if (mesh) {
            target = mesh;
        }
        if (enabled) {
            target->enable_flag_bits(flag_bits);
        } else {
            target->disable_flag_bits(flag_bits);
        }
        updated.push_back({
            {"id",    target->get_id()},
            {"name",  target->get_name()},
            {"type",  std::string{target->get_type_name()}},
            {"flags", erhe::Item_flags::to_string(target->get_flag_bits())}
        });
    }
    return make_json_content({
        {"enabled", enabled},
        {"updated", updated}
    }).dump();
}

auto Mcp_server::action_lightmap_bake_gbuffer(const json& args) -> std::string
{
    if (m_context.lightmap_baker == nullptr) {
        json r = make_text_content("Lightmap baker not available");
        r["isError"] = true;
        return r.dump();
    }
    if (!m_context.lightmap_baker->is_supported()) {
        json r = make_text_content("Lightmap G-buffer pipeline not available");
        r["isError"] = true;
        return r.dump();
    }
    // The G-buffer targets are tile-sized and hold one spatial tile at a
    // time; raster every tile in turn so multi-tile layouts are fully
    // exercised, writing per-tile debug PNGs (suffix _tile<N> on multi-
    // tile layouts) after each tile's bake while its data is resident.
    const Lightmap_baker::Atlas_layout& layout = m_context.lightmap_baker->get_layout();
    const std::string debug_png_base = args.value("debug_png_base", "");
    json files       = json::array();
    int  baked_tiles = 0;
    bool pngs_ok     = !debug_png_base.empty();
    for (int tile = 0; tile < layout.get_tile_count(); ++tile) {
        if (!m_context.lightmap_baker->bake_gbuffer(tile)) {
            continue;
        }
        ++baked_tiles;
        if (!debug_png_base.empty()) {
            const std::string base = (layout.get_tile_count() > 1)
                ? fmt::format("{}_tile{}", debug_png_base, tile)
                : debug_png_base;
            if (m_context.lightmap_baker->debug_write_gbuffer_pngs(base)) {
                files.push_back(base + "_position.png");
                files.push_back(base + "_normal.png");
            } else {
                pngs_ok = false;
            }
        }
    }
    if (baked_tiles == 0) {
        json r = make_text_content("G-buffer bake failed (no layout? run lightmap_prepare_tiles first)");
        r["isError"] = true;
        return r.dump();
    }
    json result{
        {"baked",       true},
        {"baked_tiles", baked_tiles},
        {"tile_count",  layout.get_tile_count()},
        {"width",       layout.width},
        {"height",      layout.height}
    };
    if (!debug_png_base.empty()) {
        result["debug_pngs_written"] = pngs_ok;
        result["files"]              = files;
    }
    return make_json_content(result).dump();
}

auto Mcp_server::action_lightmap_bake_direct(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }
    if (m_context.lightmap_baker == nullptr) {
        json r = make_text_content("Lightmap baker not available");
        r["isError"] = true;
        return r.dump();
    }
    const bool baked = m_context.lightmap_baker->bake_direct(*sr);
    if (!baked) {
        json r = make_text_content("Direct bake failed (needs ray query, a packed atlas and a baked G-buffer - run lightmap_prepare_tiles + lightmap_bake_gbuffer first)");
        r["isError"] = true;
        return r.dump();
    }
    // Hand the fresh atlas to the forward renderer so lightmapped draws
    // sample it (per-primitive scale/offset gates each draw).
    if (m_context.forward_renderer != nullptr) {
        m_context.forward_renderer->set_lightmap_texture(m_context.lightmap_baker->get_lightmap_texture());
    }
    json result{
        {"baked",  true},
        {"width",  m_context.lightmap_baker->get_layout().width},
        {"height", m_context.lightmap_baker->get_layout().height}
    };
    const std::string debug_png = args.value("debug_png", "");
    if (!debug_png.empty()) {
        const bool written = m_context.lightmap_baker->debug_write_lightmap_png(debug_png);
        result["debug_png_written"] = written;
        if (written) {
            result["file"] = debug_png;
        }
    }
    return make_json_content(result).dump();
}

auto Mcp_server::action_lightmap_set_baking(const json& args) -> std::string
{
    if (m_context.lightmap_baker == nullptr) {
        json r = make_text_content("Lightmap baker not available");
        r["isError"] = true;
        return r.dump();
    }
    Lightmap_baker& baker = *m_context.lightmap_baker;
    if (args.contains("enabled")) {
        // Disabling PAUSES: accumulation and sweep counts are kept and
        // re-enabling continues where it paused; reset is the restart.
        baker.set_baking_enabled(args.value("enabled", true));
    }
    if (args.value("reset", false)) {
        baker.request_reset();
    }
    if (args.value("single_iteration", false)) {
        baker.request_single_iteration();
    }
    json result{
        {"baking",     baker.is_baking_enabled()},
        {"sweeps",     baker.get_sweep_count()},
        {"cursor_row", baker.get_cursor_row()},
        {"width",      baker.get_layout().width},
        {"height",     baker.get_layout().height}
    };
    const std::string debug_png = args.value("debug_png", "");
    if (!debug_png.empty()) {
        const bool written = baker.debug_write_lightmap_png(debug_png);
        result["debug_png_written"] = written;
        if (written) {
            result["file"] = debug_png;
        }
    }
    return make_json_content(result).dump();
}

auto Mcp_server::action_lightmap_bake_to_disk(const json& args) -> std::string
{
    static_cast<void>(args);
    if ((m_context.lightmap_window == nullptr) || (m_context.lightmap_baker == nullptr)) {
        return make_error_content("Lightmap window / baker not available");
    }
    if (!m_context.lightmap_baker->is_bake_supported()) {
        return make_error_content("Lightmap ray-query pipeline not available");
    }
    if (m_context.lightmap_baker->is_offline_bake_active()) {
        return make_error_content("Offline bake already running");
    }
    const std::shared_ptr<Scene_root> scene_root = m_context.selection ? m_context.selection->get_active_scene_root() : nullptr;
    if (!scene_root) {
        return make_error_content("No active scene");
    }
    if (!m_context.lightmap_window->start_bake_to_disk()) {
        return make_error_content("Failed to start bake-to-disk (no layout / no lightmapped meshes? check Lightmap window Problems)");
    }
    // Blocking run to completion: MCP-driven verification is headless, so
    // stalling this frame is fine (each offline_tick bakes one full tile).
    int guard = 0;
    while (m_context.lightmap_baker->is_offline_bake_active() && (guard++ < 100000)) {
        m_context.lightmap_baker->offline_tick(*scene_root.get());
    }
    const Lightmap_baker::Offline_progress& progress = m_context.lightmap_baker->get_offline_progress();
    const std::filesystem::path directory = Lightmap_tile_io::directory_for_scene(scene_root->get_source_path());
    return make_json_content({
        {"tiles_baked", progress.tiles_done},
        {"tile_count",  progress.tile_count},
        {"sweeps",      progress.target_sweeps},
        {"directory",   directory.string()},
        {"completed",   progress.tiles_done == progress.tile_count}
    }).dump();
}

auto Mcp_server::action_lightmap_save_all_tiles(const json& args) -> std::string
{
    static_cast<void>(args);
    if ((m_context.lightmap_window == nullptr) || (m_context.lightmap_baker == nullptr)) {
        return make_error_content("Lightmap window / baker not available");
    }
    const std::shared_ptr<Scene_root> scene_root = m_context.selection ? m_context.selection->get_active_scene_root() : nullptr;
    if (!scene_root) {
        return make_error_content("No active scene");
    }
    const std::size_t saved      = m_context.lightmap_window->save_all_tiles();
    const int         tile_count = m_context.lightmap_baker->get_layout().get_tile_count();
    const std::filesystem::path directory = Lightmap_tile_io::directory_for_scene(scene_root->get_source_path());
    return make_json_content({
        {"saved",      saved},
        {"tile_count", tile_count},
        {"directory",  directory.string()}
    }).dump();
}

auto Mcp_server::action_lightmap_clear_tiles(const json& args) -> std::string
{
    static_cast<void>(args);
    if ((m_context.lightmap_window == nullptr) || (m_context.lightmap_baker == nullptr)) {
        return make_error_content("Lightmap window / baker not available");
    }
    const std::shared_ptr<Scene_root> scene_root = m_context.selection ? m_context.selection->get_active_scene_root() : nullptr;
    if (!scene_root) {
        return make_error_content("No active scene");
    }
    const std::filesystem::path directory = Lightmap_tile_io::directory_for_scene(scene_root->get_source_path());
    if (!m_context.lightmap_window->clear_all_tiles()) {
        return make_error_content("Clear failed (offline bake running?)");
    }
    return make_json_content({
        {"cleared",   true},
        {"directory", directory.string()}
    }).dump();
}

auto Mcp_server::action_lightmap_prepare_tiles(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    if ((m_context.lightmap_partitioner == nullptr) || (m_context.lightmap_baker == nullptr) || (m_context.editor_settings == nullptr)) {
        return make_error_content("Lightmap partitioner not available");
    }
    if (m_context.lightmap_partitioner->is_prepare_in_flight()) {
        return make_error_content("Prepare already in flight - poll get_async_status (lightmap_prepare) or lightmap_prepare_cancel first");
    }
    // In-flight guard: queued mesh operations swap source primitives when
    // the main thread executes them; a swap landing mid-prepare would clip
    // stale geometry.
    const std::size_t async_ops =
        static_cast<std::size_t>(m_context.pending_async_ops.load()) +
        static_cast<std::size_t>(m_context.running_async_ops.load()) +
        ((m_context.operation_stack != nullptr) ? m_context.operation_stack->get_queued_count() : 0u);
    if (async_ops > 0) {
        return make_error_content(
            "Operations still in flight (" + std::to_string(async_ops) +
            ") - poll get_async_status until pending + running + queued_operations == 0, then retry"
        );
    }
    const Lightmap_config& config = m_context.editor_settings->lightmap;
    const int tile_texture_size    = args.value("tile_texture_size",    config.tile_texture_size);
    const int resident_tile_budget = args.value("resident_tile_budget", config.resident_tile_budget);
    const Lightmap_partitioner::Params params{
        .min_face_texels  = config.uv_min_chart_texels,
        .hard_angles_deg  = config.hard_angles_deg,
        .gutter_texels    = config.uv_gutter_texels,
        .min_chart_texels = config.uv_min_chart_texels,
        .parameterizer    = static_cast<erhe::geometry::operation::Atlas_parameterizer>(std::clamp(config.uv_parameterizer, 0, 4)),
        .packer           = static_cast<erhe::geometry::operation::Atlas_packer>(std::clamp(config.uv_packer, 0, 2))
    };

    if (!args.value("wait", false)) {
        // Asynchronous default: launch and return immediately (the blocking
        // path exceeds the MCP request timeout on large scenes). Poll
        // get_async_status until pending + running + queued_operations == 0,
        // then read lightmap_prepare.last_result.
        const bool launched = m_context.lightmap_partitioner->request_prepare(*sr, params, tile_texture_size, resident_tile_budget);
        if (!launched) {
            return make_error_content("Prepare launch failed (no lightmapped meshes? check Lightmap window Problems)");
        }
        m_context.lightmap_partitioner->set_render_with_lightmaps(config.render_with_lightmaps);
        const Lightmap_partitioner::Prepare_progress progress = m_context.lightmap_partitioner->get_prepare_progress();
        if (!progress.in_flight) {
            // Tiny scene: the synchronous fallback already committed.
            const Lightmap_partitioner::Prepare_result& result = m_context.lightmap_partitioner->get_last_prepare_result();
            if (!result.committed) {
                return make_error_content("Partition failed (" + (result.abort_reason.empty() ? std::string{"check Lightmap window Problems"} : result.abort_reason) + ")");
            }
        }
        return make_json_content({
            {"queued",        true},
            {"regions_total", progress.regions_total},
            {"message",       "poll get_async_status until pending + running + queued_operations == 0, then read lightmap_prepare.last_result"}
        }).dump();
    }

    const bool prepared = m_context.lightmap_partitioner->prepare(*sr, params, tile_texture_size, resident_tile_budget);
    if (!prepared) {
        const Lightmap_partitioner::Prepare_result& result = m_context.lightmap_partitioner->get_last_prepare_result();
        return make_error_content("Partition failed (" + (result.abort_reason.empty() ? std::string{"no lightmapped meshes? check Lightmap window Problems"} : result.abort_reason) + ")");
    }
    m_context.lightmap_partitioner->set_render_with_lightmaps(config.render_with_lightmaps);
    json meshes = json::array();
    std::size_t piece_count = 0;
    for (const Lightmap_partitioner::Original_entry& entry : m_context.lightmap_partitioner->get_entries()) {
        json pieces = json::array();
        for (const Lightmap_partitioner::Piece_info& piece : entry.pieces) {
            pieces.push_back({
                {"tile",                   piece.tile},
                {"source_primitive_index", piece.source_primitive_index},
                {"ordinal",                piece.ordinal}
            });
        }
        piece_count += entry.pieces.size();
        meshes.push_back({
            {"original_mesh", entry.original_mesh ? entry.original_mesh->get_name() : ""},
            {"piece_mesh",    entry.piece_mesh    ? entry.piece_mesh->get_name()    : ""},
            {"pieces",        pieces}
        });
    }
    return make_json_content({
        {"prepared",    true},
        {"mesh_count",  m_context.lightmap_partitioner->get_entries().size()},
        {"piece_count", piece_count},
        {"tile_count",  m_context.lightmap_partitioner->get_tile_count()},
        {"meshes",      meshes}
    }).dump();
}

auto Mcp_server::query_lightmap_tiles(const json& args) -> std::string
{
    static_cast<void>(args);
    if (m_context.lightmap_baker == nullptr) {
        return make_error_content("Lightmap baker not available");
    }
    const Lightmap_baker::Atlas_layout& layout = m_context.lightmap_baker->get_layout();
    json tiles = json::array();
    for (int tile = 0; tile < layout.get_tile_count(); ++tile) {
        const Lightmap_baker::Tile& layout_tile = layout.tiles[static_cast<std::size_t>(tile)];
        tiles.push_back({
            {"tile",             tile},
            {"level",            layout_tile.key.level},
            {"ix",               layout_tile.key.ix},
            {"iz",               layout_tile.key.iz},
            {"cell_size_m",      layout_tile.key.cell_size(m_context.lightmap_baker->get_cell_size())},
            {"texels_per_meter", layout_tile.texels_per_meter},
            {"density_scale",    layout_tile.density_scale},
            {"has_content",      layout_tile.has_content},
            {"resident",         layout_tile.slot >= 0},
            {"cell_min",         {layout_tile.cell_bounds.min.x, layout_tile.cell_bounds.min.z}},
            {"cell_max",         {layout_tile.cell_bounds.max.x, layout_tile.cell_bounds.max.z}}
        });
    }
    json overrides = json::array();
    for (const glm::ivec3& value : m_context.lightmap_baker->get_tile_overrides()) {
        overrides.push_back({{"level", value.x}, {"ix", value.y}, {"iz", value.z}});
    }
    return make_json_content({
        {"cell_size_m", m_context.lightmap_baker->get_cell_size()},
        {"tile_size",   layout.get_tile_size()},
        {"tile_count",  layout.get_tile_count()},
        {"tiles",       tiles},
        {"overrides",   overrides}
    }).dump();
}

auto Mcp_server::action_lightmap_subdivide_tile(const json& args) -> std::string
{
    if (m_context.lightmap_window == nullptr) {
        return make_error_content("Lightmap window not available");
    }
    const Lightmap_tile_key key{args.value("level", 0), args.value("ix", 0), args.value("iz", 0)};
    const std::string error = m_context.lightmap_window->subdivide_tile(key);
    if (!error.empty()) {
        return make_error_content("Subdivide failed: " + error);
    }
    const bool reprepare = (m_context.lightmap_partitioner != nullptr) && m_context.lightmap_partitioner->is_prepare_in_flight();
    return make_json_content({
        {"subdivided",         {{"level", key.level}, {"ix", key.ix}, {"iz", key.iz}}},
        {"reprepare_launched", reprepare},
        {"message",            reprepare ? "poll get_async_status until idle, then read the new layout via lightmap_get_tiles" : "layout updates on the next bake tick"}
    }).dump();
}

auto Mcp_server::action_lightmap_merge_tile(const json& args) -> std::string
{
    if (m_context.lightmap_window == nullptr) {
        return make_error_content("Lightmap window not available");
    }
    const Lightmap_tile_key key{args.value("level", 0), args.value("ix", 0), args.value("iz", 0)};
    const std::string error = m_context.lightmap_window->merge_tile(key);
    if (!error.empty()) {
        return make_error_content("Merge failed: " + error);
    }
    const bool reprepare = (m_context.lightmap_partitioner != nullptr) && m_context.lightmap_partitioner->is_prepare_in_flight();
    return make_json_content({
        {"merged_into_parent_of", {{"level", key.level}, {"ix", key.ix}, {"iz", key.iz}}},
        {"reprepare_launched",    reprepare},
        {"message",               reprepare ? "poll get_async_status until idle, then read the new layout via lightmap_get_tiles" : "layout updates on the next bake tick"}
    }).dump();
}

auto Mcp_server::action_lightmap_revert_tiles(const json& args) -> std::string
{
    static_cast<void>(args);
    if (m_context.lightmap_partitioner == nullptr) {
        return make_error_content("Lightmap partitioner not available");
    }
    if (m_context.lightmap_partitioner->is_prepare_in_flight()) {
        return make_error_content("Prepare in flight - lightmap_prepare_cancel first, then poll get_async_status until idle");
    }
    const bool was_prepared = m_context.lightmap_partitioner->is_prepared();
    m_context.lightmap_partitioner->revert();
    return make_json_content({{"reverted", was_prepared}}).dump();
}

auto Mcp_server::action_lightmap_prepare_cancel(const json& args) -> std::string
{
    static_cast<void>(args);
    if (m_context.lightmap_partitioner == nullptr) {
        return make_error_content("Lightmap partitioner not available");
    }
    const bool was_in_flight = m_context.lightmap_partitioner->is_prepare_in_flight();
    m_context.lightmap_partitioner->cancel_prepare();
    // Does not wait: the job discards on its next completion check; poll
    // get_async_status until pending drains.
    return make_json_content({{"cancel_requested", was_in_flight}}).dump();
}

auto Mcp_server::action_lightmap_set_render(const json& args) -> std::string
{
    if (m_context.lightmap_partitioner == nullptr) {
        return make_error_content("Lightmap partitioner not available");
    }
    if (args.contains("enabled")) {
        // Same mirror as the Lightmap window checkbox: sets every scene view's
        // Visual Style shadow mode (Baked Lightmaps also disables that view's
        // shadow-map updates) and re-applies the global proxy swap.
        Viewport_config_window::set_shadow_mode_all_views(
            m_context,
            args.value("enabled", false) ? Shadow_mode::baked_lightmaps : Shadow_mode::shadow_maps
        );
    }
    return make_json_content({
        {"render_with_lightmaps", m_context.lightmap_partitioner->get_render_with_lightmaps()},
        {"prepared",              m_context.lightmap_partitioner->is_prepared()}
    }).dump();
}

auto Mcp_server::action_lightmap_frame_selection(const json& args) -> std::string
{
    static_cast<void>(args);
    if (m_context.lightmap_texture_window == nullptr) {
        return make_error_content("Lightmap Texture window not available");
    }
    m_context.lightmap_texture_window->show_window();
    m_context.lightmap_texture_window->request_frame_selection();
    return make_json_content({{"requested", true}}).dump();
}

auto Mcp_server::action_lightmap_reorder_charts(const json& args) -> std::string
{
    if ((m_context.lightmap_window == nullptr) || (m_context.editor_settings == nullptr)) {
        return make_error_content("Lightmap window not available");
    }
    // Order keys are indexed by facet id == chart id (per_facet mode only).
    if (m_context.editor_settings->lightmap.uv_parameterizer != 4) {
        return make_error_content("Requires uv_parameterizer = per_facet (4); set it in Lightmap settings");
    }
    const std::size_t reorder_async_ops =
        static_cast<std::size_t>(m_context.pending_async_ops.load()) +
        static_cast<std::size_t>(m_context.running_async_ops.load()) +
        ((m_context.operation_stack != nullptr) ? m_context.operation_stack->get_queued_count() : 0u);
    if (reorder_async_ops > 0) {
        return make_error_content("Operations still in flight - poll get_async_status until idle, then retry");
    }
    const int tile = args.value("active", false)
        ? Lightmap_window::c_reorder_active_tiles
        : args.value("tile", Lightmap_window::c_reorder_all_tiles);
    if (!m_context.lightmap_window->reorder_charts_by_bake(tile)) {
        return make_error_content("No bake to order by (bake first), no prepared world-space partition, or a prepare is in flight");
    }
    return make_json_content({
        {"queued", true},
        {"tile",   tile},
        {"message", "async re-prepare launched with luminance-ordered charts; poll get_async_status until idle, then bake"}
    }).dump();
}

auto Mcp_server::query_active_scene(const json& args) -> std::string
{
    static_cast<void>(args);
    if (!m_context.selection) {
        json r = make_text_content("Selection system not available");
        r["isError"] = true;
        return r.dump();
    }
    const std::shared_ptr<Scene_root> active_scene_root = m_context.selection->get_active_scene_root();
    if (!active_scene_root) {
        return make_json_content({{"active_scene", nullptr}}).dump();
    }
    return make_json_content({
        {"active_scene", active_scene_root->get_name()},
        {"scene_id",     active_scene_root->get_scene().get_id()}
    }).dump();
}

auto Mcp_server::action_set_active_scene(const json& args) -> std::string
{
    if (!m_context.selection) {
        json r = make_text_content("Selection system not available");
        r["isError"] = true;
        return r.dump();
    }
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const sr = find_scene(scene_name);
    if (sr == nullptr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }
    // Same activation path as focusing the scene's window in the UI
    // (emits Active_scene_changed; the gizmo rebinds, the highlight moves).
    m_context.selection->set_active_scene_root(sr->shared_from_this());
    return make_json_content({{"active_scene", sr->get_name()}}).dump();
}

auto Mcp_server::action_transform_selection(const json& args) -> std::string
{
    Transform_tool* transform_tool = m_context.transform_tool;
    if (transform_tool == nullptr) {
        json r = make_text_content("Transform tool not available");
        r["isError"] = true;
        return r.dump();
    }

    Transform_tool_shared& shared = transform_tool->shared;
    // A node selection populates shared.entries; an active mesh-component selection
    // instead sets shared.component_mode (entries stays empty). Either is a valid target,
    // matching the gizmo's own "nothing to transform" condition used elsewhere.
    if (shared.entries.empty() && !shared.component_mode) {
        json r = make_text_content("Nothing to transform - select node(s) with select_items, or a mesh-component selection with select_mesh_components");
        r["isError"] = true;
        return r.dump();
    }

    const std::string space = args.value("space", "global");
    if ((space != "local") && (space != "global")) {
        json r = make_text_content("Invalid space '" + space + "' (expected 'local' or 'global')");
        r["isError"] = true;
        return r.dump();
    }
    const bool local = (space == "local");
    if (local && (shared.entries.size() != 1)) {
        json r = make_text_content("Local space edit requires exactly one selected node");
        r["isError"] = true;
        return r.dump();
    }

    std::string parse_error;
    auto read_floats = [&args, &parse_error](const char* key, float* out_values, std::size_t count) -> bool {
        if (!args.contains(key)) {
            return false;
        }
        const json& value = args.at(key);
        const bool shape_ok = value.is_array() && (value.size() == count);
        if (shape_ok) {
            for (std::size_t i = 0; i < count; ++i) {
                if (!value[i].is_number()) {
                    parse_error = std::string{key} + " must be an array of " + std::to_string(count) + " numbers";
                    return false;
                }
                out_values[i] = value[i].get<float>();
            }
            return true;
        }
        parse_error = std::string{key} + " must be an array of " + std::to_string(count) + " numbers";
        return false;
    };

    std::optional<glm::vec3> translation;
    std::optional<glm::quat> rotation;
    std::optional<glm::vec3> scale;
    std::optional<glm::vec3> skew;
    float v[4];
    if (read_floats("translation",   v, 3)) { translation = glm::vec3{v[0], v[1], v[2]};       }
    if (read_floats("rotation_xyzw", v, 4)) { rotation    = glm::quat{v[3], v[0], v[1], v[2]}; }
    if (read_floats("scale",         v, 3)) { scale       = glm::vec3{v[0], v[1], v[2]};       }
    if (read_floats("skew",          v, 3)) { skew        = glm::vec3{v[0], v[1], v[2]};       }
    if (!parse_error.empty()) {
        json r = make_text_content(parse_error);
        r["isError"] = true;
        return r.dump();
    }
    if (!translation.has_value() && !rotation.has_value() && !scale.has_value() && !skew.has_value()) {
        json r = make_text_content("Nothing to apply - provide translation, rotation_xyzw, scale and/or skew");
        r["isError"] = true;
        return r.dump();
    }

    json applied = json::object();
    if (translation.has_value()) {
        transform_tool->apply_translation_edit(translation.value(), local);
        applied["translation"] = {translation->x, translation->y, translation->z};
    }
    if (rotation.has_value()) {
        transform_tool->apply_rotation_edit(rotation.value(), local);
        applied["rotation_xyzw"] = {rotation->x, rotation->y, rotation->z, rotation->w};
    }
    if (scale.has_value()) {
        transform_tool->apply_scale_edit(scale.value(), local);
        applied["scale"] = {scale->x, scale->y, scale->z};
    }
    if (skew.has_value()) {
        transform_tool->apply_skew_edit(skew.value(), local);
        applied["skew"] = {skew->x, skew->y, skew->z};
    }

    const bool end_edit = args.value("end_edit", true);
    if (end_edit) {
        // Mirror the gizmo drag-release: the node record path is a no-op in component
        // mode (shared.entries is empty there), so a mesh-component edit (move / extrude /
        // extrude_group_normal / extrude_vertex_normal) must be finalized via
        // commit_component_edit() to queue its undoable operation. Without this, an MCP
        // component edit would deform the live geometry but never commit (and never finalize
        // extrude normals).
        transform_tool->record_transform_operation();
        if (shared.component_mode && transform_tool->is_component_edit_active()) {
            transform_tool->commit_component_edit();
        }
    }

    auto trs_to_json = [](const erhe::scene::Trs_transform& trs) -> json {
        const glm::vec3 t = trs.get_translation();
        const glm::quat r = trs.get_rotation();
        const glm::vec3 s = trs.get_scale();
        const glm::vec3 k = trs.get_skew();
        return json{
            {"translation",   {t.x, t.y, t.z}},
            {"rotation_xyzw", {r.x, r.y, r.z, r.w}},
            {"scale",         {s.x, s.y, s.z}},
            {"skew",          {k.x, k.y, k.z}}
        };
    };

    json nodes = json::array();
    for (const Transform_entry& entry : shared.entries) {
        if (!entry.node) {
            continue;
        }
        nodes.push_back({
            {"name",            entry.node->get_name()},
            {"id",              entry.node->get_id()},
            {"local_transform", trs_to_json(entry.node->parent_from_node_transform())},
            {"world_transform", trs_to_json(entry.node->world_from_node_transform())}
        });
    }

    return make_json_content({
        {"space",    space},
        {"applied",  applied},
        {"end_edit", end_edit},
        {"nodes",    nodes}
    }).dump();
}

auto Mcp_server::action_set_node_transform(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }
    const std::shared_ptr<erhe::scene::Node> node = find_node_in_scene(*sr, args, "node_id", "node_name");
    if (!node) {
        json r = make_text_content("Node not found (a node created this frame attaches on the next frame - retry)");
        r["isError"] = true;
        return r.dump();
    }

    const std::string space = args.value("space", "world");
    const bool world = (space == "world") || (space == "global");
    if (!world && (space != "local")) {
        json r = make_text_content("Invalid space '" + space + "' (expected 'world' or 'local')");
        r["isError"] = true;
        return r.dump();
    }

    std::string parse_error;
    auto read_floats = [&args, &parse_error](const char* key, float* out_values, std::size_t count) -> bool {
        if (!args.contains(key)) {
            return false;
        }
        const json& value = args.at(key);
        if (value.is_array() && (value.size() == count)) {
            for (std::size_t i = 0; i < count; ++i) {
                if (!value[i].is_number()) {
                    parse_error = std::string{key} + " must be an array of " + std::to_string(count) + " numbers";
                    return false;
                }
                out_values[i] = value[i].get<float>();
            }
            return true;
        }
        parse_error = std::string{key} + " must be an array of " + std::to_string(count) + " numbers";
        return false;
    };

    std::optional<glm::vec3> translation;
    std::optional<glm::quat> rotation;
    std::optional<glm::vec3> scale;
    float v[4];
    if (read_floats("translation",   v, 3)) { translation = glm::vec3{v[0], v[1], v[2]};       }
    if (read_floats("rotation_xyzw", v, 4)) { rotation    = glm::quat{v[3], v[0], v[1], v[2]}; }
    if (read_floats("scale",         v, 3)) { scale       = glm::vec3{v[0], v[1], v[2]};       }
    if (!parse_error.empty()) {
        json r = make_text_content(parse_error);
        r["isError"] = true;
        return r.dump();
    }
    if (!translation.has_value() && !rotation.has_value() && !scale.has_value()) {
        json r = make_text_content("Nothing to set - provide translation, rotation_xyzw and/or scale");
        r["isError"] = true;
        return r.dump();
    }

    // ABSOLUTE set semantics (unlike transform_selection's drag-delta): the
    // provided components replace those of the node's current transform in
    // the requested space, in ONE call, without touching the selection (no
    // gizmo rebind, no kinematic hold on selected dynamic bodies).
    const erhe::scene::Trs_transform parent_from_node_before = node->parent_from_node_transform();
    erhe::scene::Trs_transform trs = world ? node->world_from_node_transform() : parent_from_node_before;
    if (translation.has_value()) { trs.set_translation(translation.value()); }
    if (rotation.has_value())    { trs.set_rotation   (rotation.value());    }
    if (scale.has_value())       { trs.set_scale      (scale.value());       }
    if (world) {
        node->set_world_from_node(trs);
    } else {
        node->set_parent_from_node(trs);
    }

    // Applied immediately (so chained set_node_transform calls compose), then
    // recorded for undo: Node_transform_operation's execute is an idempotent
    // absolute re-apply, so queueing it is the record. It also snaps the
    // rigid body to the new pose (teleport, no impulse) on execute and undo.
    m_context.operation_stack->queue(
        std::make_shared<Node_transform_operation>(
            Node_transform_operation::Parameters{
                .node                    = node,
                .parent_from_node_before = parent_from_node_before,
                .parent_from_node_after  = node->parent_from_node_transform()
            }
        )
    );

    auto trs_to_json = [](const erhe::scene::Trs_transform& t) -> json {
        const glm::vec3 translation_out = t.get_translation();
        const glm::quat rotation_out    = t.get_rotation();
        const glm::vec3 scale_out       = t.get_scale();
        return json{
            {"translation",   {translation_out.x, translation_out.y, translation_out.z}},
            {"rotation_xyzw", {rotation_out.x, rotation_out.y, rotation_out.z, rotation_out.w}},
            {"scale",         {scale_out.x, scale_out.y, scale_out.z}}
        };
    };
    return make_json_content({
        {"node_name",       node->get_name()},
        {"node_id",         node->get_id()},
        {"space",           world ? "world" : "local"},
        {"local_transform", trs_to_json(node->parent_from_node_transform())},
        {"world_transform", trs_to_json(node->world_from_node_transform())}
    }).dump();
}

// Instance placement shared by place_brush and create_shape. Every placement
// of one brush shares its Primitive (and thus GPU buffers / raytrace shape) -
// this is THE reuse path: create a shape once, place it N times.
auto Mcp_server::place_brush_instance(const json& args, Scene_root& sr, Brush& brush, json& result) -> std::string
{
    auto read_vec3 = [&args](const char* key, glm::vec3& out_value) {
        const json value = args.value(key, json());
        if (value.is_array() && (value.size() == 3)) {
            out_value = glm::vec3{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
        }
    };

    auto library = sr.get_content_library();

    std::shared_ptr<erhe::primitive::Material> material;
    const std::string material_name = args.value("material_name", "");
    const std::size_t material_id   = args.value("material_id", std::size_t{0});
    if (material_id != 0) {
        // The id path reaches materials in any scene's library AND the
        // asset manager's loaded containers (which live in no scene) -
        // the R5.4 verification surface for meshes using container
        // materials.
        material = find_material_by_id(material_id);
        if (!material) {
            json r = make_text_content("Material not found with id: " + std::to_string(material_id));
            r["isError"] = true;
            return r.dump();
        }
    }
    if (!material && !material_name.empty() && library && library->materials) {
        const auto& mat_list = library->materials->get_all<erhe::primitive::Material>();
        for (const auto& mat : mat_list) {
            if (mat->get_name() == material_name) {
                material = mat;
                break;
            }
        }
    }
    if (!material && library && library->materials) {
        const auto& mat_list = library->materials->get_all<erhe::primitive::Material>();
        if (!mat_list.empty()) {
            material = mat_list.front();
        }
    }
    if (!material) {
        json r = make_text_content("No materials available");
        r["isError"] = true;
        return r.dump();
    }

    glm::vec3 position{0.0f};
    read_vec3("position", position);

    std::optional<glm::quat> rotation;
    {
        const json value = args.value("rotation_xyzw", json());
        if (value.is_array() && (value.size() == 4)) {
            rotation = glm::quat{value[3].get<float>(), value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
        }
    }

    std::shared_ptr<erhe::scene::Node> parent;
    if (args.contains("parent_node_id")) {
        const std::size_t parent_node_id = args.value("parent_node_id", std::size_t{0});
        sr.get_scene().for_each_node([&](const std::shared_ptr<erhe::scene::Node>& node) {
            if (node->get_id() == parent_node_id) {
                parent = node;
                return false;
            }
            return true;
        });
        if (!parent) {
            json r = make_text_content("Parent node not found with id: " + std::to_string(parent_node_id));
            r["isError"] = true;
            return r.dump();
        }
    }

    // "scale" as a number is the brush bake scale (geometry, collision
    // shape, volume and inertia all scale - the right choice for physics
    // parts); as an array of 3 it becomes node-space TRS scale composed
    // into the world transform (visual anisotropy - collision shapes do
    // NOT follow node scale, so use it with motion_mode "none").
    double scale = 1.0;
    std::optional<glm::vec3> node_scale;
    if (args.contains("scale")) {
        const json& value = args.at("scale");
        if (value.is_number()) {
            scale = value.get<double>();
        } else if (value.is_array() && (value.size() == 3)) {
            node_scale = glm::vec3{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
        }
    }
    std::optional<float> mass_override;
    if (args.contains("mass") && args.at("mass").is_number()) {
        mass_override = args.at("mass").get<float>();
    }
    // "none" = pure visual instance: the Node_physics attachment the brush
    // instancing creates is detached again before the node enters the
    // scene. Saves one strip pass per part on physics-driven assemblies
    // (e.g. swaying trees whose child parts must not collide).
    const std::string motion_mode_text = args.value("motion_mode", "dynamic");
    const bool skip_physics = (motion_mode_text == "none");
    const erhe::physics::Motion_mode motion_mode = parse_motion_mode(
        skip_physics ? "static" : motion_mode_text,
        erhe::physics::Motion_mode::e_dynamic
    );

    glm::mat4 world_from_node = erhe::math::create_translation<float>(position);
    if (rotation.has_value()) {
        world_from_node = world_from_node * glm::mat4_cast(rotation.value());
    }
    if (node_scale.has_value()) {
        world_from_node = world_from_node * erhe::math::create_scale<float>(node_scale.value());
    }
    auto instance_node = place_brush_in_scene(m_context, brush, sr, world_from_node, material, scale, motion_mode, parent, 0, mass_override);
    if (!instance_node) {
        json r = make_text_content("Failed to create shape instance");
        r["isError"] = true;
        return r.dump();
    }
    if (skip_physics) {
        const std::shared_ptr<Node_physics> node_physics = erhe::scene::get_attachment<Node_physics>(instance_node.get());
        if (node_physics) {
            instance_node->detach(node_physics.get());
        }
    }
    // Per-instance name renames the NODE only: the mesh keeps the brush name,
    // so instances of one brush stay content-identical for glTF export dedup.
    const std::string instance_name = args.value("name", "");
    if (!instance_name.empty()) {
        instance_node->set_name(instance_name);
    }
    result["node_name"]   = instance_node->get_name();
    result["node_id"]     = instance_node->get_id();
    result["material"]    = material->get_name();
    result["position"]    = {position.x, position.y, position.z};
    result["motion_mode"] = skip_physics ? "none" : motion_mode_to_string(motion_mode);
    result["parent"]      = parent ? parent->get_name() : "(scene root)";
    if (rotation.has_value()) {
        result["rotation_xyzw"] = {rotation->x, rotation->y, rotation->z, rotation->w};
    }
    if (node_scale.has_value()) {
        result["node_scale"] = {node_scale->x, node_scale->y, node_scale->z};
    } else {
        result["scale"] = scale;
    }
    if (mass_override.has_value()) {
        result["mass"] = mass_override.value();
    }
    return {};
}

auto Mcp_server::action_place_brush(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    auto library = sr->get_content_library();
    if (!library || !library->brushes) {
        json r = make_text_content("No brushes in scene");
        r["isError"] = true;
        return r.dump();
    }

    const std::size_t brush_id   = args.value("brush_id", std::size_t{0});
    const std::string brush_name = args.value("brush_name", "");
    std::shared_ptr<Brush> brush;
    const auto& brush_list = library->brushes->get_all<Brush>();
    for (const auto& b : brush_list) {
        if ((brush_id != 0) ? (b->get_id() == brush_id) : (b->get_name() == brush_name)) {
            brush = b;
            break;
        }
    }
    if (!brush) {
        json r = make_text_content(
            (brush_id != 0)
                ? "Brush not found with id: " + std::to_string(brush_id)
                : brush_name.empty()
                    ? std::string{"Provide brush_id or brush_name"}
                    : "Brush not found with name: " + brush_name
        );
        r["isError"] = true;
        return r.dump();
    }

    json result = {
        {"brush",    brush->get_name()},
        {"brush_id", brush->get_id()}
    };
    const std::string error = place_brush_instance(args, *sr, *brush, result);
    if (!error.empty()) {
        return error;
    }
    return make_json_content(result).dump();
}

auto Mcp_server::action_create_shape(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    const std::string shape = args.value("shape", "");
    if ((shape != "box") && (shape != "uv_sphere") && (shape != "cone") && (shape != "capsule") && (shape != "torus")) {
        json r = make_text_content("Invalid shape '" + shape + "' (expected box, uv_sphere, cone, capsule or torus)");
        r["isError"] = true;
        return r.dump();
    }

    const bool make_instance = args.value("instance", true);
    const bool add_brush     = args.value("add_brush", false);
    if (!make_instance && !add_brush) {
        json r = make_text_content("Nothing to do - enable instance and/or add_brush");
        r["isError"] = true;
        return r.dump();
    }

    auto read_vec3 = [&args](const char* key, glm::vec3& out_value) {
        const json value = args.value(key, json());
        if (value.is_array() && (value.size() == 3)) {
            out_value = glm::vec3{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
        }
    };
    auto read_ivec3 = [&args](const char* key, glm::ivec3& out_value) {
        const json value = args.value(key, json());
        if (value.is_array() && (value.size() == 3)) {
            out_value = glm::ivec3{value[0].get<int>(), value[1].get<int>(), value[2].get<int>()};
        }
    };

    Brush_data brush_create_info{
        .context      = m_context,
        .app_settings = *m_context.app_settings,
        .name         = args.value("name", shape),
        .build_info   = erhe::primitive::Build_info{
            .primitive_types = {
                .fill_triangles          = true,
                .fill_triangles_expanded = true,
                .edge_lines              = true,
                .corner_points           = true,
                .centroid_points         = true
            },
            .buffer_info     = m_context.mesh_memory->make_primitive_buffer_info()
        },
        .normal_style = erhe::primitive::Normal_style::point_normals,
        .density      = 1.0f
    };

    std::shared_ptr<Brush> brush;
    json parameters_echo = json::object();
    if (shape == "box") {
        Box_parameters parameters;
        read_vec3 ("size",  parameters.size);
        read_ivec3("steps", parameters.steps);
        parameters.power = args.value("power", parameters.power);
        brush = Create_box::create_brush(brush_create_info, parameters);
        parameters_echo = {
            {"size",  {parameters.size.x,  parameters.size.y,  parameters.size.z}},
            {"steps", {parameters.steps.x, parameters.steps.y, parameters.steps.z}},
            {"power", parameters.power}
        };
    } else if (shape == "uv_sphere") {
        Uv_sphere_parameters parameters;
        parameters.radius      = args.value("radius",      parameters.radius);
        parameters.slice_count = args.value("slice_count", parameters.slice_count);
        parameters.stack_count = args.value("stack_count", parameters.stack_count);
        brush = Create_uv_sphere::create_brush(brush_create_info, parameters);
        parameters_echo = {
            {"radius",      parameters.radius},
            {"slice_count", parameters.slice_count},
            {"stack_count", parameters.stack_count}
        };
    } else if (shape == "cone") {
        Cone_parameters parameters;
        parameters.height        = args.value("height",        parameters.height);
        parameters.bottom_radius = args.value("bottom_radius", parameters.bottom_radius);
        parameters.top_radius    = args.value("top_radius",    parameters.top_radius);
        parameters.use_top       = args.value("use_top",       parameters.use_top);
        parameters.use_bottom    = args.value("use_bottom",    parameters.use_bottom);
        parameters.slice_count   = args.value("slice_count",   parameters.slice_count);
        parameters.stack_count   = args.value("stack_count",   parameters.stack_count);
        brush = Create_cone::create_brush(brush_create_info, parameters);
        parameters_echo = {
            {"height",        parameters.height},
            {"bottom_radius", parameters.bottom_radius},
            {"top_radius",    parameters.top_radius},
            {"use_top",       parameters.use_top},
            {"use_bottom",    parameters.use_bottom},
            {"slice_count",   parameters.slice_count},
            {"stack_count",   parameters.stack_count}
        };
    } else if (shape == "capsule") {
        Capsule_parameters parameters;
        parameters.length                 = args.value("length",        parameters.length);
        parameters.bottom_radius          = args.value("bottom_radius", parameters.bottom_radius);
        parameters.top_radius             = args.value("top_radius",    parameters.top_radius);
        parameters.slice_count            = args.value("slice_count",   parameters.slice_count);
        parameters.hemisphere_stack_count = args.value("stack_count",   parameters.hemisphere_stack_count);
        // make_capsule() requires |bottom_radius - top_radius| < length when the
        // radii differ: the tangent cone between the cap spheres exists only
        // while neither sphere contains the other.
        if (
            (parameters.bottom_radius != parameters.top_radius) &&
            (parameters.length <= std::abs(parameters.bottom_radius - parameters.top_radius))
        ) {
            json r = make_text_content("Tapered capsule requires length > |bottom_radius - top_radius|");
            r["isError"] = true;
            return r.dump();
        }
        brush = Create_capsule::create_brush(brush_create_info, parameters);
        parameters_echo = {
            {"length",        parameters.length},
            {"bottom_radius", parameters.bottom_radius},
            {"top_radius",    parameters.top_radius},
            {"slice_count",   parameters.slice_count},
            {"stack_count",   parameters.hemisphere_stack_count},
            {"tapered",       parameters.bottom_radius != parameters.top_radius}
        };
    } else { // torus
        Torus_parameters parameters;
        parameters.major_radius = args.value("major_radius", parameters.major_radius);
        parameters.minor_radius = args.value("minor_radius", parameters.minor_radius);
        parameters.major_steps  = args.value("major_steps",  parameters.major_steps);
        parameters.minor_steps  = args.value("minor_steps",  parameters.minor_steps);
        brush = Create_torus::create_brush(brush_create_info, parameters);
        parameters_echo = {
            {"major_radius", parameters.major_radius},
            {"minor_radius", parameters.minor_radius},
            {"major_steps",  parameters.major_steps},
            {"minor_steps",  parameters.minor_steps}
        };
    }

    if (!brush) {
        json r = make_text_content("Failed to create shape: " + shape);
        r["isError"] = true;
        return r.dump();
    }

    json result = {
        {"shape",      shape},
        {"name",       brush->get_name()},
        {"parameters", parameters_echo}
    };

    auto library = sr->get_content_library();
    if (add_brush) {
        if (!library || !library->brushes) {
            json r = make_text_content("No brush library in scene");
            r["isError"] = true;
            return r.dump();
        }
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{library->mutex};
        library->brushes->add(brush);
        result["brush_id"] = brush->get_id();
    }

    if (make_instance) {
        const std::string error = place_brush_instance(args, *sr, *brush, result);
        if (!error.empty()) {
            return error;
        }
    }

    return make_json_content(result).dump();
}

auto Mcp_server::action_create_node(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    std::shared_ptr<erhe::scene::Node> parent{};
    if (args.contains("parent_node_id") || args.contains("parent_node_name")) {
        parent = find_node_in_scene(*sr, args, "parent_node_id", "parent_node_name");
        if (!parent) {
            json r = make_text_content("Parent node not found");
            r["isError"] = true;
            return r.dump();
        }
    } else {
        parent = sr->get_hosted_scene()->get_root_node();
    }

    const std::shared_ptr<erhe::scene::Node> node = m_context.scene_commands->create_new_empty_node(parent.get());
    if (!node) {
        json r = make_text_content("Failed to create node");
        r["isError"] = true;
        return r.dump();
    }

    const std::string name = args.value("name", "");
    if (!name.empty()) {
        node->set_name(name);
    }

    glm::vec3 position{0.0f};
    const json pos_json = args.value("position", json());
    if (pos_json.is_array() && (pos_json.size() == 3)) {
        position = glm::vec3{pos_json[0].get<float>(), pos_json[1].get<float>(), pos_json[2].get<float>()};
    }
    // The node is not yet attached (the insert executes on the next editor
    // frame); setting the world transform now is preserved by Node::set_parent.
    node->set_world_from_node(erhe::math::create_translation<float>(position));

    return make_json_content({
        {"node_name", node->get_name()},
        {"node_id",   node->get_id()},
        {"parent",    parent->get_name()},
        {"position",  {position.x, position.y, position.z}},
        {"queued",    true} // the insert operation executes on the next editor frame
    }).dump();
}

auto Mcp_server::action_create_light(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    const std::string type_str = args.value("type", "directional");
    if ((type_str != "directional") && (type_str != "point") && (type_str != "spot")) {
        json r = make_text_content("Invalid light type '" + type_str + "' (expected directional, point or spot)");
        r["isError"] = true;
        return r.dump();
    }
    const erhe::scene::Light_type type = parse_light_type(type_str, erhe::scene::Light_type::directional);

    auto read_vec3 = [&args](const char* key, glm::vec3& out_value) {
        const json value = args.value(key, json());
        if (value.is_array() && (value.size() == 3)) {
            out_value = glm::vec3{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
        }
    };

    glm::vec3 position{0.0f};
    read_vec3("position", position);
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    read_vec3("color", color);
    const float       intensity   = args.value("intensity",   1.0f);
    const bool        cast_shadow = args.value("cast_shadow", true);
    // Directional lights have no meaningful range (parallel rays); point / spot
    // default to the same 25.0 the editor's Scene_builder uses.
    const float       range       = args.value("range", (type == erhe::scene::Light_type::directional) ? 0.0f : 25.0f);
    const std::string name        = args.value("name", "MCP light");

    std::shared_ptr<erhe::scene::Node>  node;
    std::shared_ptr<erhe::scene::Light> light;
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{sr->item_host_mutex};
        node  = std::make_shared<erhe::scene::Node>(name);
        light = std::make_shared<erhe::scene::Light>(name);
        light->type        = type;
        light->color       = color;
        light->intensity   = intensity;
        light->range       = range;
        light->cast_shadow = cast_shadow;
        light->layer_id    = sr->layers().light()->id;
        if (args.contains("inner_spot_angle")) { light->inner_spot_angle = args.value("inner_spot_angle", light->inner_spot_angle); }
        if (args.contains("outer_spot_angle")) { light->outer_spot_angle = args.value("outer_spot_angle", light->outer_spot_angle); }
        light->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui | erhe::Item_flags::show_debug_visualizations);
        node->attach(light);
        node->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui);
        // The node is attached to the scene via the queued insert operation below;
        // set the world transform now (preserved by Node::set_parent).
        node->set_world_from_node(erhe::math::create_translation<float>(position));
    }

    // Insert the light node into the scene root via an undoable operation,
    // executed NOW rather than queued: the returned node_id is attached and
    // addressable by the caller's next tool call (a queued insert left the
    // node invisible to find_node_in_scene until the next frame).
    const std::shared_ptr<erhe::scene::Node>& root_node = sr->get_scene().get_root_node();
    m_context.operation_stack->execute_now(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context = m_context,
                .item    = node,
                .parent  = root_node,
                .mode    = Item_insert_remove_operation::Mode::insert
            }
        )
    );

    return make_json_content({
        {"light_name",  light->get_name()},
        {"light_id",    light->get_id()},
        {"node_name",   node->get_name()},
        {"node_id",     node->get_id()},
        {"type",        type_str},
        {"color",       {color.x, color.y, color.z}},
        {"intensity",   intensity},
        {"range",       range},
        {"cast_shadow", cast_shadow},
        {"position",    {position.x, position.y, position.z}},
        {"queued",      false} // inserted synchronously; immediately addressable
    }).dump();
}

auto Mcp_server::action_add_node_attachment(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<erhe::scene::Node> node = find_node_in_scene(*sr, args, "node_id", "node_name");
    if (!node) {
        return make_error_content("Node not found (give node_id or node_name)");
    }
    const std::string type_key = args.value("type", "");
    if (type_key.empty()) {
        return make_error_content("Missing 'type' (attachment catalog key)");
    }
    const Attachment_type_info* info = find_attachment_type(type_key);
    if (info == nullptr) {
        return make_error_content("Unknown attachment type: " + type_key);
    }
    if (!info->can_add(*node)) {
        return make_error_content(
            "Cannot add attachment '" + type_key + "' to node '" + node->get_name() +
            "' (duplicate, or precondition not met)"
        );
    }
    info->make(*m_context.scene_commands, *node);
    return make_json_content({
        {"added",   true},
        {"queued",  true}, // the attach operation executes on the next editor frame
        {"node",    node->get_name()},
        {"node_id", node->get_id()},
        {"type",    type_key}
    }).dump();
}

auto Mcp_server::action_remove_node_attachment(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<erhe::scene::Node> node = find_node_in_scene(*sr, args, "node_id", "node_name");
    if (!node) {
        return make_error_content("Node not found (give node_id or node_name)");
    }
    const std::size_t attachment_id = args.value("attachment_id", std::size_t{0});
    const std::string type_key      = args.value("type", "");
    if ((attachment_id == 0) && type_key.empty()) {
        return make_error_content("Give attachment_id or type to identify the attachment to remove");
    }

    // Match by attachment_id, or by attachment type name (as reported by
    // get_node_details, e.g. "Camera", "Node_physics"), case-insensitively.
    auto iequals = [](std::string_view a, std::string_view b) -> bool {
        if (a.size() != b.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
                return false;
            }
        }
        return true;
    };

    std::shared_ptr<erhe::scene::Node_attachment> target;
    for (const std::shared_ptr<erhe::scene::Node_attachment>& att : node->get_attachments()) {
        const bool match = (attachment_id != 0)
            ? (att->get_id() == attachment_id)
            : iequals(type_key, att->get_type_name());
        if (match) {
            target = att;
            break;
        }
    }
    if (!target) {
        return make_error_content("No matching attachment on node '" + node->get_name() + "'");
    }

    const std::string removed_type = std::string{target->get_type_name()};
    const std::size_t removed_id   = target->get_id();
    m_context.scene_commands->remove_attachment(target);
    return make_json_content({
        {"removed",       true},
        {"queued",        true}, // the detach operation executes on the next editor frame
        {"node",          node->get_name()},
        {"node_id",       node->get_id()},
        {"attachment_id", removed_id},
        {"type",          removed_type}
    }).dump();
}

auto Mcp_server::action_edit_light(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    // Accept light_id / light_name, and also bare id / name for convenience.
    std::shared_ptr<erhe::scene::Light> light = find_light_in_scene(*sr, args, "light_id", "light_name");
    if (!light) {
        light = find_light_in_scene(*sr, args, "id", "name");
    }
    if (!light) {
        json r = make_text_content("Light not found (specify light_id or light_name)");
        r["isError"] = true;
        return r.dump();
    }

    auto read_vec3 = [&args](const char* key, glm::vec3& out_value) -> bool {
        const json value = args.value(key, json());
        if (value.is_array() && (value.size() == 3)) {
            out_value = glm::vec3{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
            return true;
        }
        return false;
    };

    json changed = json::object();
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{sr->item_host_mutex};

        if (args.contains("type")) {
            const std::string type_str = args.value("type", "");
            if ((type_str != "directional") && (type_str != "point") && (type_str != "spot")) {
                json r = make_text_content("Invalid light type '" + type_str + "' (expected directional, point or spot)");
                r["isError"] = true;
                return r.dump();
            }
            // Assigning Light::type re-buckets the light for rendering (forward
            // variant + shadow technique). Other type-dependent fields (e.g.
            // range) are left exactly as provided by the caller.
            light->type = parse_light_type(type_str, light->type);
            changed["type"] = type_str;
        }
        glm::vec3 color{};
        if (read_vec3("color", color)) {
            light->color = color;
            changed["color"] = {color.x, color.y, color.z};
        }
        if (args.contains("intensity")) {
            light->intensity = args.value("intensity", light->intensity);
            changed["intensity"] = light->intensity;
        }
        if (args.contains("range")) {
            light->range = args.value("range", light->range);
            changed["range"] = light->range;
        }
        if (args.contains("cast_shadow")) {
            light->cast_shadow = args.value("cast_shadow", light->cast_shadow);
            changed["cast_shadow"] = light->cast_shadow;
        }
        if (args.contains("inner_spot_angle")) {
            light->inner_spot_angle = args.value("inner_spot_angle", light->inner_spot_angle);
            changed["inner_spot_angle"] = light->inner_spot_angle;
        }
        if (args.contains("outer_spot_angle")) {
            light->outer_spot_angle = args.value("outer_spot_angle", light->outer_spot_angle);
            changed["outer_spot_angle"] = light->outer_spot_angle;
        }
        glm::vec3 position{};
        if (read_vec3("position", position)) {
            erhe::scene::Node* node = light->get_node();
            if (node != nullptr) {
                node->set_world_from_node(erhe::math::create_translation<float>(position));
                changed["position"] = {position.x, position.y, position.z};
            } else {
                changed["position_error"] = "light has no node";
            }
        }
    }

    return make_json_content({
        {"light_id",   light->get_id()},
        {"light_name", light->get_name()},
        {"changed",    changed}
    }).dump();
}

auto Mcp_server::action_edit_camera(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    const std::size_t camera_id   = args.contains("camera_id") ? args.value("camera_id", std::size_t{0}) : args.value("id", std::size_t{0});
    const std::string camera_name = args.contains("camera_name") ? args.value("camera_name", "") : args.value("name", "");
    std::shared_ptr<erhe::scene::Camera> camera{};
    for (const std::shared_ptr<erhe::scene::Camera>& candidate : sr->get_scene().get_cameras()) {
        if ((camera_id != 0) ? (candidate->get_id() == camera_id) : (!camera_name.empty() && (candidate->get_name() == camera_name))) {
            camera = candidate;
            break;
        }
    }
    if (!camera) {
        json r = make_text_content("Camera not found (specify camera_id or camera_name)");
        r["isError"] = true;
        return r.dump();
    }

    json changed = json::object();
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{sr->item_host_mutex};
        if (args.contains("exposure")) {
            camera->set_exposure(args.value("exposure", camera->get_exposure()));
            changed["exposure"] = camera->get_exposure();
        }
        if (args.contains("shadow_range")) {
            camera->set_shadow_range(args.value("shadow_range", camera->get_shadow_range()));
            changed["shadow_range"] = camera->get_shadow_range();
        }
        if (args.contains("fov_y")) {
            erhe::scene::Projection* projection = camera->projection();
            if (projection != nullptr) {
                projection->fov_y = args.value("fov_y", projection->fov_y);
                changed["fov_y"] = projection->fov_y;
            }
        }
    }

    return make_json_content({
        {"camera_id",   camera->get_id()},
        {"camera_name", camera->get_name()},
        {"changed",     changed}
    }).dump();
}

auto Mcp_server::action_toggle_physics(const json& args) -> std::string
{
    if (!m_context.app_settings) {
        json r = make_text_content("Settings not available");
        r["isError"] = true;
        return r.dump();
    }

    // Optional explicit state; omitted = toggle. Scripts that need a known
    // state (settle-then-freeze loops) pass enabled instead of reading the
    // state first and toggling conditionally.
    bool enabled = !m_context.editor_settings->physics.dynamic_enable;
    const auto enabled_it = args.find("enabled");
    if (enabled_it != args.end()) {
        if (!enabled_it->is_boolean()) {
            json r = make_text_content("enabled must be a boolean");
            r["isError"] = true;
            return r.dump();
        }
        enabled = enabled_it->get<bool>();
    }

    m_context.editor_settings->physics.dynamic_enable = enabled;
    m_context.app_settings->settings_store().touch();

    return make_json_content({
        {"dynamic_physics_enabled", enabled}
    }).dump();
}

auto Mcp_server::action_reparent_node(const json& args) -> std::string
{
    const std::string scene_name    = args.value("scene_name", "");
    const std::size_t node_id       = args.value("node_id", std::size_t{0});
    const std::size_t parent_node_id = args.value("parent_node_id", std::size_t{0});

    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    erhe::scene::Scene& scene = sr->get_scene();

    // Find child node
    std::shared_ptr<erhe::scene::Node> child_node;
    scene.for_each_node([&](const std::shared_ptr<erhe::scene::Node>& node) {
        if (node->get_id() == node_id) {
            child_node = node;
            return false;
        }
        return true;
    });
    if (!child_node) {
        json r = make_text_content("Node not found: " + std::to_string(node_id));
        r["isError"] = true;
        return r.dump();
    }

    // Find parent node (0 means scene root)
    std::shared_ptr<erhe::scene::Node> new_parent;
    if (parent_node_id == 0) {
        new_parent = scene.get_root_node();
    } else {
        scene.for_each_node([&](const std::shared_ptr<erhe::scene::Node>& node) {
            if (node->get_id() == parent_node_id) {
                new_parent = node;
                return false;
            }
            return true;
        });
    }
    if (!new_parent) {
        json r = make_text_content("Parent node not found: " + std::to_string(parent_node_id));
        r["isError"] = true;
        return r.dump();
    }

    std::shared_ptr<Operation> op = std::make_shared<Item_parent_change_operation>(
        new_parent,
        child_node,
        std::shared_ptr<erhe::Hierarchy>{},
        std::shared_ptr<erhe::Hierarchy>{}
    );
    m_context.operation_stack->queue(op);

    return make_json_content({
        {"node",   child_node->get_name()},
        {"parent", new_parent->get_name()}
    }).dump();
}

auto Mcp_server::action_clipboard_copy_nodes(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    const json ids_json = args.value("node_ids", json::array());
    std::set<std::size_t> target_ids;
    for (const auto& v : ids_json) {
        if (v.is_number()) {
            target_ids.insert(v.get<std::size_t>());
        }
    }
    if (target_ids.empty()) {
        json r = make_text_content("node_ids must name at least one node");
        r["isError"] = true;
        return r.dump();
    }

    // Same semantics as the interactive Copy (Selection::copy_selection):
    // the clipboard holds ownerless CLONES; the clones transitively pin the
    // shared resources (e.g. mesh materials), which the scene-close leak
    // watchdog reports as intentional clipboard pins.
    std::vector<std::shared_ptr<erhe::Item_base>> clones;
    json copied = json::array();
    sr->get_scene().for_each_node([&](const std::shared_ptr<erhe::scene::Node>& node) {
        if (!target_ids.contains(node->get_id())) {
            return true;
        }
        clones.push_back(node->clone());
        copied.push_back(node->get_name());
        return true;
    });
    if (clones.empty()) {
        json r = make_text_content("No nodes found for the given node_ids");
        r["isError"] = true;
        return r.dump();
    }
    m_context.clipboard->set_contents(clones);

    return make_json_content({
        {"copied_count", static_cast<int>(clones.size())},
        {"copied",       copied}
    }).dump();
}

auto Mcp_server::action_clipboard_paste(const json& args) -> std::string
{
    const std::string scene_name     = args.value("scene_name", "");
    const std::size_t parent_node_id = args.value("parent_node_id", std::size_t{0});

    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    std::shared_ptr<erhe::scene::Node> parent_node;
    if (parent_node_id == 0) {
        parent_node = sr->get_scene().get_root_node();
    } else {
        sr->get_scene().for_each_node([&](const std::shared_ptr<erhe::scene::Node>& node) {
            if (node->get_id() == parent_node_id) {
                parent_node = node;
                return false;
            }
            return true;
        });
    }
    if (!parent_node) {
        json r = make_text_content("Parent node not found: " + std::to_string(parent_node_id));
        r["isError"] = true;
        return r.dump();
    }

    const bool pasted = m_context.clipboard->try_paste(parent_node, parent_node->get_child_count());
    if (!pasted) {
        json r = make_text_content("Paste failed (empty clipboard or no paste target)");
        r["isError"] = true;
        return r.dump();
    }

    return make_json_content({
        {"pasted_into_scene", sr->get_name()},
        {"parent",            parent_node->get_name()}
    }).dump();
}

auto Mcp_server::action_lock_items(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }
    const json ids_json = args.value("ids", json::array());
    std::set<std::size_t> target_ids;
    for (const auto& v : ids_json) {
        if (v.is_number()) target_ids.insert(v.get<std::size_t>());
    }
    auto items = find_items_by_ids(*sr, target_ids);
    for (auto& item : items) {
        item->set_lock_edit(true);
    }
    return make_json_content({{"locked_count", static_cast<int>(items.size())}}).dump();
}

auto Mcp_server::action_unlock_items(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }
    const json ids_json = args.value("ids", json::array());
    std::set<std::size_t> target_ids;
    for (const auto& v : ids_json) {
        if (v.is_number()) target_ids.insert(v.get<std::size_t>());
    }
    auto items = find_items_by_ids(*sr, target_ids);
    for (auto& item : items) {
        item->set_lock_edit(false);
    }
    return make_json_content({{"unlocked_count", static_cast<int>(items.size())}}).dump();
}

auto Mcp_server::action_add_tags(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }
    const json ids_json  = args.value("ids", json::array());
    const json tags_json = args.value("tags", json::array());
    std::set<std::size_t> target_ids;
    for (const auto& v : ids_json) {
        if (v.is_number()) target_ids.insert(v.get<std::size_t>());
    }
    std::vector<std::string> tags;
    for (const auto& v : tags_json) {
        if (v.is_string()) tags.push_back(v.get<std::string>());
    }
    auto items = find_items_by_ids(*sr, target_ids);
    for (auto& item : items) {
        for (const auto& tag : tags) {
            item->add_tag(tag);
        }
    }
    return make_json_content({{"tagged_count", static_cast<int>(items.size())}}).dump();
}

auto Mcp_server::action_remove_tags(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }
    const json ids_json  = args.value("ids", json::array());
    const json tags_json = args.value("tags", json::array());
    std::set<std::size_t> target_ids;
    for (const auto& v : ids_json) {
        if (v.is_number()) target_ids.insert(v.get<std::size_t>());
    }
    std::vector<std::string> tags;
    for (const auto& v : tags_json) {
        if (v.is_string()) tags.push_back(v.get<std::string>());
    }
    auto items = find_items_by_ids(*sr, target_ids);
    for (auto& item : items) {
        for (const auto& tag : tags) {
            item->remove_tag(tag);
        }
    }
    return make_json_content({{"untagged_count", static_cast<int>(items.size())}}).dump();
}


} // namespace editor
