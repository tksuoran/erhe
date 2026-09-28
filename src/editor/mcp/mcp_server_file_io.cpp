// Mcp_server file I/O tools (save/load scene, glTF export/import, screenshot capture).
// Split out of mcp_server.cpp; shares helpers via mcp_server_shared.hpp.

#include "mcp/mcp_server.hpp"
#include "mcp/mcp_server_shared.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "app_rendering.hpp"
#include "app_scenes.hpp"
#include "app_settings.hpp"
#include "graphics/thumbnails.hpp"
#include "editor_log.hpp"
#include "operations/operation_stack.hpp"
#include "operations/scene_open_operation.hpp"
#include "parsers/gltf.hpp"
#include "parsers/usd.hpp"
#include "parsers/gltf_extensions_export.hpp"
#include "parsers/physics_export.hpp"
#include "prefabs/prefab_library.hpp"
#include "scene/scene_image_capture.hpp"
#include "scene/scene_root.hpp"
#include "tools/clipboard.hpp"
#include "tools/mesh_component_selection.hpp"
#include "tools/selection_tool.hpp"
#if defined(ERHE_XR_LIBRARY_OPENXR)
#include "xr/headset_view.hpp"
#endif

#include "erhe_dataformat/dataformat.hpp"
#include "erhe_file/file.hpp"
#include "erhe_gltf/gltf.hpp"
#include "erhe_imgui/imgui_host.hpp"
#include "erhe_imgui/imgui_window.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_graphics/image_writer.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_item/hierarchy.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_string.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene_renderer/shader_key.hpp"
#if defined(ERHE_USD_LIBRARY_LIGHTUSD)
#include "erhe_usd/usd.hpp"
#endif

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace editor {

using namespace mcp_server_detail;

auto Mcp_server::action_save_scene(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    const std::string path_str   = args.value("path", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }
    // Save follows the scene's own format, the way File > Save Scene does
    // (doc/erhe/usd_compatibility_design.md E1): a USD-backed scene writes a USDA
    // layer, every other scene writes a single erhe-authored glTF file
    // (doc/editor/gltf_scene_roundtrip.md phase 4). Without 'path' the scene
    // saves to its own source file when it was opened/loaded from one, else
    // to res/editor/scenes/<scene name> with the format's extension. An
    // explicit path is normalized to carry an extension of the scene's
    // format (.glb appended when missing; .gltf selects the text form,
    // .usda / .usd / .usdc / .usdz are honored for a USD scene).
    const bool usd = (sr->get_source_format() == Scene_source_format::usd);
    std::filesystem::path path;
    if (path_str.empty()) {
        path = resolve_scene_save_path(*sr);
    } else {
        path = std::filesystem::path{path_str};
        if (usd) {
            if (!editor::is_usd_file_extension(path)) {
                path = std::filesystem::path{path.string() + ".usda"};
            }
        } else if (
            (path.extension() != std::filesystem::path{".glb"}) &&
            (path.extension() != std::filesystem::path{".gltf"})
        ) {
            path = std::filesystem::path{path.string() + ".glb"};
        }
    }
    // save_scene_gltf also reloads the prefab when 'path' is a loaded prefab
    // source, refreshing every instance in every scene (this subsumed the
    // former save_prefab tool).
    const bool ok = usd
        ? editor::save_scene_usd (m_context, *sr, path)
        : editor::save_scene_gltf(m_context, *sr, path);
    if (!ok) {
        json r = make_text_content("save_scene failed: " + path.string());
        r["isError"] = true;
        return r.dump();
    }
    // set_as_source gives the save "Save As" semantics: the scene (and its
    // R5.3 container record, which follows set_source_path) now lives in
    // this file. Without it an explicit-path save is an export that leaves
    // the scene's source binding untouched; a path-less first save writes
    // back like File > Save Scene always has.
    const bool set_as_source = args.value("set_as_source", false);
    if (set_as_source || (path_str.empty() && sr->get_source_path().empty())) {
        sr->set_source_path(path, usd ? Scene_source_format::usd : Scene_source_format::gltf);
    }
    return make_json_content({
        {"saved",  true},
        {"path",   path.string()},
        {"format", c_str(sr->get_source_format())}
    }).dump();
}

auto Mcp_server::action_close_scene(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }
    // Same path as the Scene row's Close context menu entry: the close is
    // queued to the message bus and runs on a following frame. A scene whose
    // close is already pending is refused here, at the request.
    if (!sr->request_close(*m_context.app_message_bus)) {
        json r = make_text_content("Scene close already pending: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }
    return make_json_content({
        {"queued",     true},
        {"scene_name", sr->get_name()}
    }).dump();
}

auto Mcp_server::make_reset_result() -> std::string
{
    json result = m_reset_counts;
    result["reset"]       = true;
    result["scenes_open"] = 0;
    return make_json_content(result).dump();
}

auto Mcp_server::action_reset_editor_state(const json& args) -> std::string
{
    static_cast<void>(args);

    // Second and later passes: the scene closes queued below run from the
    // message bus pump, one frame per pass; keep deferring until every scene
    // is gone. The k_request_timeout expiry check bounds the wait.
    if (m_reset_pending) {
        if ((m_context.app_scenes != nullptr) && !m_context.app_scenes->get_scene_roots().empty()) {
            m_defer_current_request = true;
            return {};
        }
        m_reset_pending = false;
        log_mcp->info("MCP server: reset_editor_state - every scene closed");
        return make_reset_result();
    }

    // Transient editor state first, so nothing below keeps content of the
    // closing scenes alive (the scene-close leak watchdog would report it).
    if (m_context.selection != nullptr) {
        m_context.selection->clear_selection();
        // The active item lives beside the selection and survives a plain
        // clear, so it is forgotten explicitly (doc/editor/active_item.md D2).
        m_context.selection->set_active_item({});
    }
    if (m_context.mesh_component_selection != nullptr) {
        m_context.mesh_component_selection->clear_all();
        m_context.mesh_component_selection->set_mode(Mesh_component_mode::object);
    }
    if (m_context.clipboard != nullptr) {
        m_context.clipboard->set_contents(std::vector<std::shared_ptr<erhe::Item_base>>{});
    }
    // Undo / redo stacks and the not-yet-executed operation queue: every
    // recorded operation holds shared_ptrs to scene content, and a queued one
    // would execute into a scene that is being closed.
    std::size_t history_dropped = 0;
    std::size_t queued_dropped  = 0;
    if (m_context.operation_stack != nullptr) {
        history_dropped =
            m_context.operation_stack->get_undo_stack().size() +
            m_context.operation_stack->get_redo_stack().size();
        queued_dropped = m_context.operation_stack->discard_queued();
        m_context.operation_stack->clear_history();
    }
    m_shader_debug_stack.clear();
    // Thumbnail slots: pending render callbacks own the item they would
    // render, and a slot keeps showing its last image for a reused id.
    if (m_context.thumbnails != nullptr) {
        m_context.thumbnails->flush();
    }

    // Windows: back to the visibility the editor started with (the persisted
    // window state read at startup; a window the state does not name starts
    // open). Docking is left alone - the layout is not test state.
    int windows_shown  = 0;
    int windows_hidden = 0;
    if (m_context.imgui_windows != nullptr) {
        for (erhe::imgui::Imgui_window* window : m_context.imgui_windows->get_windows()) {
            if (window == nullptr) {
                continue;
            }
            const bool startup_visible = m_context.imgui_windows->get_persistent_window_open(window->get_ini_label());
            if (startup_visible == window->is_window_visible()) {
                continue;
            }
            if (startup_visible) {
                window->show_window();
                ++windows_shown;
            } else {
                window->hide_window();
                ++windows_hidden;
            }
        }
    }

    // Scenes: request the close of every one (the Close context menu path,
    // see action_close_scene) and defer this request until they are gone. A
    // scene whose close is already pending (an earlier close_scene call)
    // closes from that request; it is still waited for below.
    int scenes_closing = 0;
    if ((m_context.app_scenes != nullptr) && (m_context.app_message_bus != nullptr)) {
        for (const std::shared_ptr<Scene_root>& scene_root : m_context.app_scenes->get_scene_roots()) {
            if (scene_root->is_close_requested() || scene_root->request_close(*m_context.app_message_bus)) {
                ++scenes_closing;
            }
        }
    }
    log_mcp->info(
        "MCP server: reset_editor_state - closing {} scene(s), {} window(s) shown, {} hidden, {} history + {} queued operation(s) dropped",
        scenes_closing, windows_shown, windows_hidden, history_dropped, queued_dropped
    );
    m_reset_counts = {
        {"scenes_closed",      scenes_closing},
        {"windows_shown",      windows_shown},
        {"windows_hidden",     windows_hidden},
        {"history_dropped",    history_dropped},
        {"queued_ops_dropped", queued_dropped}
    };
    if (scenes_closing > 0) {
        m_reset_pending         = true;
        m_defer_current_request = true;
        return {};
    }
    return make_reset_result();
}

auto Mcp_server::action_create_scene(const json& args) -> std::string
{
    static_cast<void>(args);
    // Same path as the Create > Scene menu command: queue the request so the
    // scene (and its ImGui windows) is built from the message-bus pump on a
    // following frame, outside ImGui window iteration. The new scene name
    // ("Scene N") is assigned there; callers can discover it via list_scenes.
    m_context.app_message_bus->create_scene.queue_message(Create_scene_message{});
    return make_json_content({
        {"queued", true}
    }).dump();
}

auto Mcp_server::action_load_scene(const json& args) -> std::string
{
    const std::string path_str = args.value("path", "");
    if (path_str.empty()) {
        json r = make_text_content("Missing required argument: path");
        r["isError"] = true;
        return r.dump();
    }
    const std::filesystem::path path{path_str};
    std::error_code error_code;
    if (!std::filesystem::exists(path, error_code)) {
        json r = make_text_content("File not found: " + path_str);
        r["isError"] = true;
        return r.dump();
    }
    // Queue the exact File > Load Scene path: the message handler opens an
    // erhe-authored glTF file as a full scene (fresh content library, browser
    // + viewport windows, ERHE_scene state applied; not undoable), a USD file
    // as a USD-backed scene, and routes a foreign glTF to
    // Scene_open_operation. Queued so the window setup runs from the message
    // pump on a following frame, outside ImGui iteration.
    m_context.app_message_bus->load_scene_file.queue_message(
        Load_scene_file_message{
            .path = path
        }
    );
    return make_json_content({
        {"queued",     true},
        {"path",       path_str},
        {"scene_name", erhe::file::to_string(path.stem())}
    }).dump();
}

auto Mcp_server::action_open_scene(const json& args) -> std::string
{
    const std::string path_str = args.value("path", "");
    if (path_str.empty()) {
        json r = make_text_content("Missing required argument: path");
        r["isError"] = true;
        return r.dump();
    }
    const std::filesystem::path path{path_str};
    std::error_code error_code;
    if (!std::filesystem::exists(path, error_code)) {
        json r = make_text_content("File not found: " + path_str);
        r["isError"] = true;
        return r.dump();
    }
    // A USD file has no undoable open path of its own: it takes the same
    // route File > Load Scene takes, which builds the USD-backed scene
    // (doc/erhe/usd_compatibility_design.md E1).
    if (editor::is_usd_file_extension(path)) {
        m_context.app_message_bus->load_scene_file.queue_message(
            Load_scene_file_message{
                .path = path
            }
        );
        return make_json_content({
            {"queued",     true},
            {"path",       path_str},
            {"scene_name", erhe::file::to_string(path.stem())},
            {"format",     "usd"}
        }).dump();
    }
    // Same path as the Asset Browser's "Open" context menu entry: queue a
    // Scene_open_operation (new scene root + content library + browser
    // window + inline glTF import, all one undo entry). It executes on a
    // following Operation_stack::update(); discover the scene afterwards
    // via list_scenes.
    m_context.operation_stack->queue(std::make_shared<Scene_open_operation>(path));
    return make_json_content({
        {"queued",     true},
        {"path",       path_str},
        {"scene_name", erhe::file::to_string(path.filename())}
    }).dump();
}

auto Mcp_server::action_export_gltf(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    const std::string path_str   = args.value("path", "");
    const bool        binary     = args.value("binary", true);
    if (path_str.empty()) {
        json r = make_text_content("Missing required argument: path");
        r["isError"] = true;
        return r.dump();
    }
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }
    std::shared_ptr<erhe::scene::Node> root_node = sr->get_scene().get_root_node();
    if (!root_node) {
        json r = make_text_content("Scene has no root node: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }
    const bool editor_state = args.value("editor_state", false);
    Physics_description_items physics_items;
    const erhe::scene::Physics_description physics_data = build_physics_description(sr->get_scene(), sr->get_content_library().get(), &physics_items);
    // Prefab instances export as glTF 2.1 externalAsset references instead
    // of flattened content; URIs are relativized against the export
    // directory.
    const std::filesystem::path export_path{path_str};
    erhe::gltf::Gltf_export_arguments export_arguments{
        .root_node             = *root_node,
        .binary                = binary,
        .physics_data          = &physics_data,
        .external_assets       = collect_prefab_external_assets(*root_node, export_path.parent_path()),
        .image_source_provider = make_gltf_image_source_provider(sr->get_content_library()),
        .animations            = collect_gltf_export_animations(sr->get_content_library())
    };
    if (editor_state) {
        // Full scene persistence: editor-domain ERHE_* extensions + baked
        // graph-mesh exclusion (doc/editor/gltf_scene_roundtrip.md phase 3).
        // The default export stays plain interchange.
        add_gltf_editor_state(export_arguments, *sr, export_path, physics_items);
    }
    const std::string gltf = erhe::gltf::export_gltf(export_arguments);
    if (!erhe::file::write_file(std::filesystem::path{path_str}, gltf)) {
        json r = make_text_content("Failed to write file: " + path_str);
        r["isError"] = true;
        return r.dump();
    }
    return make_json_content({
        {"exported", true},
        {"path",     path_str},
        {"bytes",    gltf.size()}
    }).dump();
}

auto Mcp_server::action_import_gltf(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    const std::string path_str   = args.value("path", "");
    if (path_str.empty()) {
        json r = make_text_content("Missing required argument: path");
        r["isError"] = true;
        return r.dump();
    }

    std::shared_ptr<Scene_root> scene_root;
    if (m_context.app_scenes != nullptr) {
        for (const std::shared_ptr<Scene_root>& candidate : m_context.app_scenes->get_scene_roots()) {
            if (candidate->get_name() == scene_name) {
                scene_root = candidate;
                break;
            }
        }
    }
    if (!scene_root) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    const std::filesystem::path path{path_str};
    std::error_code error_code;
    if (!std::filesystem::exists(path, error_code)) {
        json r = make_text_content("File not found: " + path_str);
        r["isError"] = true;
        return r.dump();
    }

    const bool materials_as_references = args.value("materials_as_references", false);
    editor::import_gltf(m_context, make_import_build_info(m_context), scene_root, path, materials_as_references);
    return make_json_content({
        {"imported",                true},
        {"path",                    path_str},
        {"materials_as_references", materials_as_references}
    }).dump();
}

auto Mcp_server::query_scan_gltf(const json& args) -> std::string
{
    const std::string path_str = args.value("path", "");
    if (path_str.empty()) {
        json r = make_text_content("Missing required argument: path");
        r["isError"] = true;
        return r.dump();
    }
    const std::filesystem::path path{path_str};
    std::error_code error_code;
    if (!std::filesystem::exists(path, error_code)) {
        json r = make_text_content("File not found: " + path_str);
        r["isError"] = true;
        return r.dump();
    }

    const erhe::gltf::Gltf_scan scan = erhe::gltf::scan_gltf(path);
    const auto entries = [](const std::vector<std::string>& names, const std::vector<std::string>& uids) -> json {
        json category = json::array();
        for (std::size_t i = 0, end = names.size(); i < end; ++i) {
            json entry{{"name", names[i]}};
            if ((i < uids.size()) && !uids[i].empty()) {
                entry["uid"] = uids[i];
            }
            category.push_back(std::move(entry));
        }
        return category;
    };
    return make_json_content({
        {"scenes",          entries(scan.scenes,          scan.scene_uids)},
        {"nodes",           entries(scan.nodes,           scan.node_uids)},
        {"meshes",          entries(scan.meshes,          scan.mesh_uids)},
        {"cameras",         entries(scan.cameras,         scan.camera_uids)},
        {"materials",       entries(scan.materials,       scan.material_uids)},
        {"images",          entries(scan.images,          scan.image_uids)},
        {"samplers",        entries(scan.samplers,        scan.sampler_uids)},
        {"skins",           entries(scan.skins,           scan.skin_uids)},
        {"animations",      entries(scan.animations,      scan.animation_uids)},
        {"files",           entries(scan.files,           scan.file_uids)},
        {"external_assets", entries(scan.external_assets, scan.external_asset_uids)},
        {"extensions_used", scan.extensions_used},
        {"errors",          scan.errors}
    }).dump();
}

auto Mcp_server::action_import_usd(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    const std::string path_str   = args.value("path", "");
    if (path_str.empty()) {
        return make_error_content("Missing required argument: path");
    }

    std::shared_ptr<Scene_root> scene_root;
    if (m_context.app_scenes != nullptr) {
        for (const std::shared_ptr<Scene_root>& candidate : m_context.app_scenes->get_scene_roots()) {
            if (candidate->get_name() == scene_name) {
                scene_root = candidate;
                break;
            }
        }
    }
    if (!scene_root) {
        return make_error_content("Scene not found: " + scene_name);
    }

    const std::filesystem::path path{path_str};
    std::error_code error_code;
    if (!std::filesystem::exists(path, error_code)) {
        return make_error_content("File not found: " + path_str);
    }
    if (!editor::is_usd_file_extension(path)) {
        return make_error_content("Not a USD file (.usd/.usda/.usdc/.usdz): " + path_str);
    }

    // Built here rather than through import_usd() so a failure - no USD
    // support, an unreadable file, a conversion error - reaches the caller as
    // an isError reply instead of a cheerful "imported": true.
    const Usd_import_result import_result = editor::make_import_usd_operation(
        m_context,
        make_import_build_info(m_context),
        scene_root,
        path
    );
    if (!import_result.operation) {
        return make_error_content("USD import failed: " + import_result.error);
    }
    m_context.operation_stack->queue(import_result.operation);
    return make_json_content({
        {"imported",       true},
        {"path",           path_str},
        {"node_count",     import_result.node_count},
        {"mesh_count",     import_result.mesh_count},
        {"material_count", import_result.material_count},
        {"texture_count",  import_result.texture_count}
    }).dump();
}

auto Mcp_server::query_describe_usd_file(const json& args) -> std::string
{
#if defined(ERHE_USD_LIBRARY_LIGHTUSD)
    const std::string path_str = args.value("path", "");
    if (path_str.empty()) {
        return make_error_content("Missing required argument: path");
    }
    const std::filesystem::path path{path_str};
    std::error_code error_code;
    if (!std::filesystem::exists(path, error_code)) {
        return make_error_content("File not found: " + path_str);
    }

    erhe::usd::Load_stage_result load_result = erhe::usd::load_stage(path);
    if (load_result.stage == nullptr) {
        return make_error_content("USD load failed: " + load_result.error);
    }

    const erhe::usd::Stage_description description = erhe::usd::describe_stage(*load_result.stage.get());

    json prim_types = json::array();
    for (const erhe::usd::Prim_type_count& entry : description.prim_types) {
        prim_types.push_back(json{{"type_name", entry.type_name}, {"count", entry.count}});
    }
    json layers = json::array();
    for (const erhe::usd::Layer_reference& layer : description.layers) {
        layers.push_back(json{{"kind", layer.kind}, {"asset_path", layer.asset_path}});
    }
    return make_json_content({
        {"path",            path_str},
        {"prim_count",      description.prim_count},
        {"prim_types",      prim_types},
        {"layers",          layers},
        {"up_axis",         description.up_axis},
        {"default_prim",    description.default_prim},
        {"meters_per_unit", description.meters_per_unit},
        {"warning",         load_result.warning}
    }).dump();
#else
    static_cast<void>(args);
    return make_error_content("USD support not built (ERHE_USD_LIBRARY=none)");
#endif
}


namespace {

// Result slot for an asynchronous prefab template load. The on_ready
// callback runs inline when the template is already cached, and a later
// frame otherwise - after action_instantiate_prefab has returned and its
// stack frame is gone. The callback therefore writes into this shared
// object, which both the handler and the callback own.
class Prefab_instantiate_result
{
public:
    bool                               ran_inline {false};
    bool                               load_failed{false};
    std::shared_ptr<Prefab>            prefab;
    std::shared_ptr<erhe::scene::Node> node;
};

} // anonymous namespace

auto Mcp_server::action_instantiate_prefab(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    const std::string path_str   = args.value("path", "");
    if (path_str.empty()) {
        return make_error_content("Missing required argument: path");
    }
    Scene_root* sr = find_scene(scene_name);
    if (sr == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    if (m_context.prefab_library == nullptr) {
        return make_error_content("Prefab library not available");
    }

    const std::filesystem::path path{path_str};
    const glm::vec3 position        = get_vec3(args, "position", glm::vec3{0.0f});
    const glm::mat4 world_from_node = erhe::math::create_translation<float>(position);

    // Asynchronous template load (doc/editor/async_asset_loading_design.md 2.12): the
    // callback runs INLINE when the prefab is already cached, and only then
    // can this report the created node_id. A first load of a file defers, so
    // the response says so and the caller polls get_async_status.
    const std::shared_ptr<Prefab_instantiate_result> result = std::make_shared<Prefab_instantiate_result>();
    const std::weak_ptr<Scene_root>                  weak_scene_root = sr->shared_from_this();
    App_context&                                     context         = m_context;
    m_context.prefab_library->get_or_load_async(
        path,
        [&context, weak_scene_root, world_from_node, result](const std::shared_ptr<Prefab>& prefab)
        {
            result->ran_inline = true; // only observed while still inside the call below
            if (!prefab) {
                result->load_failed = true;
                return;
            }
            const std::shared_ptr<Scene_root> target = weak_scene_root.lock();
            if (!target) {
                result->load_failed = true;
                return;
            }
            result->prefab = prefab;
            result->node   = instantiate_prefab(context, prefab, *target, world_from_node);
        }
    );
    if (!result->ran_inline) {
        // The template is loading on a worker; the instance is created when
        // it lands.
        return make_json_content({
            {"instantiated", false},
            {"queued",       true},
            {"loading",      true},
            {"path",         path.generic_string()},
            {"message",      "prefab template is loading - poll get_async_status until asset_loads and queued_operations are 0, then find the node via get_scene_nodes"}
        }).dump();
    }
    if (result->load_failed || !result->prefab) {
        return make_error_content("Failed to load prefab (missing file, no nodes, or reference cycle - see log): " + path_str);
    }
    if (!result->node) {
        return make_error_content("Failed to instantiate prefab: " + path_str);
    }
    // The insertion is queued as an undoable operation; the node enters the
    // scene when the operation executes (same frame, after this call).
    return make_json_content({
        {"instantiated", true},
        {"queued",       true},
        {"path",         result->prefab->source_path.generic_string()},
        {"node_id",      result->node->get_id()},
        {"node_name",    result->node->get_name()}
    }).dump();
}

auto Mcp_server::action_reload_prefab(const json& args) -> std::string
{
    const std::string path_str = args.value("path", "");
    if (path_str.empty()) {
        return make_error_content("Missing required argument: path");
    }
    if (m_context.prefab_library == nullptr) {
        return make_error_content("Prefab library not available");
    }
    const bool ok = m_context.prefab_library->reload(std::filesystem::path{path_str});
    if (!ok) {
        return make_error_content("reload_prefab failed (not a loaded prefab, or re-parse failed - see log): " + path_str);
    }
    return make_json_content({
        {"reloaded", true},
        {"path",     path_str}
    }).dump();
}

// Edit one property of a prefab TEMPLATE (doc/erhe/usd_compatibility_design.md X2):
// the templates live in Prefab::holding_scene, which no scene lookup
// reaches, and a template edit is what every instance of it reads through
// its reference layer (D33). Deliberately not undoable - a template is a
// projection of a file, like reload_prefab.
auto Mcp_server::action_set_prefab_template_property(const json& args) -> std::string
{
    const std::string path_str  = args.value("source_path", "");
    const std::string prim_path = args.value("prim_path", "");
    const std::string item_path = args.value("item_path", "");
    const std::string property_name = args.value("property", "");
    if (path_str.empty()) {
        return make_error_content("Missing required argument: source_path");
    }
    if (property_name.empty()) {
        return make_error_content("Missing required argument: property");
    }
    if (m_context.prefab_library == nullptr) {
        return make_error_content("Prefab library not available");
    }

    std::error_code             error_code;
    const std::filesystem::path wanted_path      = std::filesystem::weakly_canonical(std::filesystem::path{path_str}, error_code);
    const std::filesystem::path source_path      = error_code ? std::filesystem::path{path_str} : wanted_path;
    std::shared_ptr<Prefab>     prefab;
    for (const auto& [key, candidate] : m_context.prefab_library->get_prefabs()) {
        if ((key.source_path == source_path) && (key.prim_path == prim_path)) {
            prefab = candidate;
            break;
        }
    }
    if (!prefab) {
        return make_error_content("No loaded prefab template for: " + path_str + (prim_path.empty() ? std::string{} : prim_path));
    }
    if (!prefab->template_root) {
        return make_error_content("Prefab template has no root: " + path_str);
    }

    erhe::Hierarchy* item = item_path.empty()
        ? prefab->template_root.get()
        : erhe::find_by_path(*prefab->template_root, item_path);
    if (item == nullptr) {
        return make_error_content("Template item not found at path: " + item_path);
    }

    const erhe::property::Dependency_property* property = erhe::property::Property_registry::get().find_for_object(*item, property_name);
    if (property == nullptr) {
        return make_error_content("Template item '" + item->get_name() + "' has no property '" + property_name + "'");
    }
    if (property->is_read_only()) {
        return make_error_content("Property '" + property_name + "' is read-only");
    }

    const auto value_it = args.find("value");
    if ((value_it == args.end()) || value_it->is_null()) {
        // No value clears the template's local value.
        const bool cleared = item->clear_value(*property);
        return make_json_content({
            {"template",  path_str},
            {"item",      item->get_name()},
            {"item_path", item_path},
            {"property",  property_name},
            {"cleared",   cleared},
            {"value",     erhe::property::to_string(*property, item->get_value(*property))},
            {"users",     item->get_reference_user_count()}
        }).dump();
    }

    std::string text;
    if (value_it->is_string()) {
        text = value_it->get<std::string>();
    } else if (value_it->is_boolean()) {
        text = value_it->get<bool>() ? "true" : "false";
    } else if (value_it->is_number()) {
        text = value_it->dump();
    } else if (value_it->is_array()) {
        for (const json& component : *value_it) {
            if (!component.is_number()) {
                return make_error_content("value array entries must be numbers");
            }
            if (!text.empty()) {
                text += " ";
            }
            text += component.dump();
        }
    } else {
        return make_error_content("value must be a string, number, bool, array of numbers, or null (clear the template's local value)");
    }
    if (erhe::property::is_object_reference_type(property->get_type())) {
        return make_error_content("Object reference properties are not settable on a template through this tool");
    }
    const std::optional<erhe::property::Property_value> value = erhe::property::parse_value(*property, text);
    if (!value.has_value()) {
        return make_error_content("'" + text + "' is not a valid " + erhe::property::c_str(property->get_type()) + " for property '" + property_name + "'");
    }
    std::string validation_error;
    if (!item->validate_value(*property, value.value(), validation_error)) {
        return make_error_content("'" + text + "' was rejected by property '" + property_name + "': " + validation_error);
    }
    if (!item->set_value(*property, value.value())) {
        return make_error_content("Property '" + property_name + "' refused the value '" + text + "'");
    }

    return make_json_content({
        {"template",  path_str},
        {"item",      item->get_name()},
        {"item_path", item_path},
        {"property",  property_name},
        {"value",     erhe::property::to_string(*property, item->get_value(*property))},
        {"users",     item->get_reference_user_count()}
    }).dump();
}

auto Mcp_server::query_prefabs(const json& args) -> std::string
{
    static_cast<void>(args);
    if (m_context.prefab_library == nullptr) {
        return make_error_content("Prefab library not available");
    }
    json prefabs = json::array();
    for (const auto& [key, prefab] : m_context.prefab_library->get_prefabs()) {
        std::size_t node_count = 0;
        for (const std::shared_ptr<erhe::scene::Node>& node : prefab->gltf_data.nodes) {
            if (node) {
                ++node_count;
            }
        }
        prefabs.push_back({
            {"path",            key.source_path.generic_string()},
            {"prim_path",       key.prim_path},
            {"name",            prefab->name},
            {"nodes",           node_count},
            {"meshes",          prefab->gltf_data.meshes.size()},
            {"materials",       prefab->materials.size()},
            {"textures",        prefab->gltf_data.images.size()},
            {"skins",           prefab->gltf_data.skins.size()},
            {"animations",      prefab->gltf_data.animations.size()},
            {"external_assets", prefab->gltf_data.external_assets.size()}
        });
    }
    return make_json_content({{"prefabs", prefabs}}).dump();
}

// Triggers the in-application RenderDoc frame capture that Headset_view wraps
// around its multiview render. This exists because an OpenXR app never calls
// vkQueuePresentKHR, so RenderDoc's usual frame delimiter is absent and a
// capture has to be bracketed by the app itself. Driving that from MCP lets the
// capture be taken at a chosen moment from the host, instead of the user having
// to find and click the Developer menu item while wearing the headset.
//
// Requires RenderDoc to be attached, which on Quest means the app was launched
// from RenderDoc Meta Fork with injection - see doc/agents/quest_renderdoc_capture.md.
auto Mcp_server::action_request_renderdoc_capture(const json& args) -> std::string
{
    static_cast<void>(args);
#if defined(ERHE_XR_LIBRARY_OPENXR)
    if (!m_context.renderdoc) {
        json r = make_text_content(
            "RenderDoc is not attached to this process, so there is nothing to capture with. "
            "Launch the app from RenderDoc Meta Fork so it injects its capture layer."
        );
        r["isError"] = true;
        return r.dump();
    }
    if ((m_context.app_rendering == nullptr) || (m_context.headset_view == nullptr) || !m_context.headset_view->is_active()) {
        json r = make_text_content(
            "No active OpenXR headset view: the in-application capture brackets the multiview "
            "render, so it only applies while the headset is rendering."
        );
        r["isError"] = true;
        return r.dump();
    }
    // Takes effect on the next headset frame; the capture is written on device
    // and collected by the RenderDoc host.
    m_context.app_rendering->request_renderdoc_capture();
    return make_json_content({{"requested", true}}).dump();
#else
    json r = make_text_content("This build has no OpenXR support, so there is no headset frame to capture.");
    r["isError"] = true;
    return r.dump();
#endif
}

auto Mcp_server::action_capture_screenshot(const json& args) -> std::string
{
    if (m_context.graphics_device == nullptr) {
        json r = make_text_content("Graphics device not available");
        r["isError"] = true;
        return r.dump();
    }

    const std::string path_str = args.value("path", std::string{"logs/mcp_screenshot.png"});

    // doc/agents/mcp_ui_driving.md: the recorded items of the desktop ImGui
    // host are drawn over the captured pixels as numbered rectangles and
    // reported as a number -> item table. Recording is asked for first,
    // because the frame that records has to be the captured frame (headless)
    // or the one right before it (windowed, where the readback costs a frame
    // of its own) - see the note in mcp_server_ui.cpp.
    json annotations = json::array();
    const bool annotate = args.value("annotate_imgui_items", false);
    if (annotate) {
        if (m_screenshot_annotation_request != m_current_request) {
            std::string                    error;
            erhe::imgui::Imgui_host* const host = resolve_imgui_host(args, error);
            if (host == nullptr) {
                m_screenshot_annotation_request = nullptr;
                json r = make_text_content(error);
                r["isError"] = true;
                return r.dump();
            }
            if (request_recorded_imgui_frame(*host, error)) {
                return {};
            }
            if (!error.empty()) {
                m_screenshot_annotation_request = nullptr;
                json r = make_text_content(error);
                r["isError"] = true;
                return r.dump();
            }
            m_screenshot_annotations        = collect_imgui_annotations(*host, args);
            m_screenshot_annotation_request = m_current_request;
        }
        annotations = m_screenshot_annotations;
    }

    int                      width  = 0;
    int                      height = 0;
    erhe::dataformat::Format format = erhe::dataformat::Format::format_8_vec4_srgb;
    std::vector<std::byte>   pixels;
    if (!m_context.graphics_device->capture_last_frame(width, height, format, pixels)) {
        // Windowed build: the last presented WSI image is owned by the
        // presentation engine and cannot be read synchronously. Arm a
        // one-shot capture that the upcoming frame's swapchain render pass
        // records, and defer this request to the next MCP pass - the retry
        // (this same handler, next frame) then collects the pixels above.
        if (m_context.graphics_device->request_frame_capture()) {
            m_defer_current_request = true;
            return {};
        }
        m_screenshot_annotation_request = nullptr;
        json r = make_text_content(
            "Frame capture not available: the swapchain does not support reading its images back, "
            "and at least one rendered frame is required."
        );
        r["isError"] = true;
        return r.dump();
    }
    m_screenshot_annotation_request = nullptr;

    if (annotate) {
        draw_imgui_annotations(width, height, std::span<std::byte>{pixels});
    }

    std::unique_ptr<erhe::graphics::Image_writer> writer = erhe::graphics::Image_writer::create();
    const int row_stride = width * 4;
    if (!writer->write_png(std::filesystem::path{path_str}, width, height, row_stride, format, pixels)) {
        json r = make_text_content("Failed to write PNG '" + path_str + "' (image writer backend may be disabled)");
        r["isError"] = true;
        return r.dump();
    }

    json result = {
        {"path",   path_str},
        {"width",  width},
        {"height", height}
    };
    if (annotate) {
        result["annotations"]      = annotations;
        result["annotation_count"] = static_cast<int>(annotations.size());
    }
    return make_json_content(result).dump();
}


namespace {

[[nodiscard]] auto parse_json_vec3(const json& value) -> std::optional<glm::vec3>
{
    if (!value.is_array() || (value.size() != 3)) {
        return std::nullopt;
    }
    glm::vec3 result{0.0f};
    for (std::size_t i = 0; i < 3; ++i) {
        if (!value[i].is_number()) {
            return std::nullopt;
        }
        const float component = value[i].get<float>();
        if (!std::isfinite(component)) {
            return std::nullopt;
        }
        result[static_cast<glm::vec3::length_type>(i)] = component;
    }
    return result;
}

// IEC 61966-2-1 encode of one linear channel, clamped to [0, 1], to 8 bits:
// what the sRGB swapchain format does to a viewport's post-processed color.
[[nodiscard]] auto srgb_encode_8(const float linear) -> std::byte
{
    const float c = std::clamp(std::isfinite(linear) ? linear : 0.0f, 0.0f, 1.0f);
    const float s = (c <= 0.0031308f) ? (12.92f * c) : ((1.055f * std::pow(c, 1.0f / 2.4f)) - 0.055f);
    return static_cast<std::byte>(static_cast<unsigned int>(std::lround(s * 255.0f)));
}

// Portable float map ("PF": RGB float32, little-endian as the negative scale
// says, rows bottom to top per the format).
[[nodiscard]] auto write_pfm(const std::filesystem::path& path, const int width, const int height, const std::span<const glm::vec4> pixels) -> bool
{
    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    if (!file) {
        return false;
    }
    const std::string header = fmt::format("PF\n{} {}\n-1.0\n", width, height);
    file.write(header.data(), static_cast<std::streamsize>(header.size()));
    std::vector<float> row(static_cast<std::size_t>(width) * 3);
    for (int y = height - 1; y >= 0; --y) {
        for (int x = 0; x < width; ++x) {
            const glm::vec4& p = pixels[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(x)];
            row[(static_cast<std::size_t>(x) * 3) + 0] = p.r;
            row[(static_cast<std::size_t>(x) * 3) + 1] = p.g;
            row[(static_cast<std::size_t>(x) * 3) + 2] = p.b;
        }
        file.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size() * sizeof(float)));
    }
    return static_cast<bool>(file);
}

} // anonymous namespace

void Mcp_server::release_abandoned_scene_image_capture()
{
    if (!m_scene_image_capture || (m_scene_image_request != nullptr)) {
        return;
    }
    // A copy recorded into a frame still in flight writes the readback
    // buffer; the chain's textures and buffer go only after it retires. Not
    // yet rendered (rendering), completed or failed: nothing references them.
    if (m_scene_image_capture->poll() == Scene_image_capture_state::in_flight) {
        return;
    }
    m_scene_image_capture.reset();
    log_mcp->info("MCP server: released the capture of an abandoned render_scene_image request");
}

// Offscreen render of a scene through an explicit camera into a temporary
// render target owned by the request (doc/editor/rendergraph.md "Scene image
// capture"): independent of viewport windows and the ImGui layout. The first
// pass builds the capture chain and defers; the rendergraph runs it on the
// next frame; later passes poll until the recorded frame has retired, write
// the file and tear the chain down.
auto Mcp_server::action_render_scene_image(const json& args) -> std::string
{
    const bool continuation =
        m_scene_image_capture &&
        (m_scene_image_request == m_current_request) &&
        (m_scene_image_enqueued_at == m_current_request->enqueued_at);
    if (continuation) {
        const Scene_image_capture_state state = m_scene_image_capture->poll();
        if ((state == Scene_image_capture_state::rendering) || (state == Scene_image_capture_state::in_flight)) {
            m_defer_current_request = true;
            return {};
        }
        m_scene_image_request = nullptr;
        if (state == Scene_image_capture_state::failed) {
            const std::string error = m_scene_image_capture->get_error();
            m_scene_image_capture.reset();
            return make_error_content("render_scene_image: " + error);
        }

        const int                        width  = m_scene_image_capture->get_width();
        const int                        height = m_scene_image_capture->get_height();
        const std::span<const glm::vec4> pixels = m_scene_image_capture->get_pixels();
        const std::filesystem::path      path{m_scene_image_header.value("path", std::string{})};
        json result = m_scene_image_header;
        if (result.contains("shadow_debug_light")) {
            // The light slot the render used; null when the light got none
            // (not shaded in that frame's light set).
            const std::optional<uint32_t> slot = m_scene_image_capture->get_shadow_debug_light_index();
            result["shadow_debug_light"]["light_index"] = slot.has_value() ? json(slot.value()) : json(nullptr);
        }
        if (path.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
        }
        bool written = false;
        if (m_scene_image_capture->get_output() == Scene_image_output::png) {
            std::vector<std::byte> bytes(pixels.size() * 4);
            for (std::size_t i = 0; i < pixels.size(); ++i) {
                bytes[(i * 4) + 0] = srgb_encode_8(pixels[i].r);
                bytes[(i * 4) + 1] = srgb_encode_8(pixels[i].g);
                bytes[(i * 4) + 2] = srgb_encode_8(pixels[i].b);
                bytes[(i * 4) + 3] = std::byte{0xffu};
            }
            std::unique_ptr<erhe::graphics::Image_writer> writer = erhe::graphics::Image_writer::create();
            written = writer->write_png(path, width, height, width * 4, erhe::dataformat::Format::format_8_vec4_srgb, bytes);
        } else {
            written = write_pfm(path, width, height, pixels);
            float  min_luminance = std::numeric_limits<float>::max();
            float  max_luminance = std::numeric_limits<float>::lowest();
            double sum_luminance = 0.0;
            for (const glm::vec4& p : pixels) {
                const float luminance = (0.2126f * p.r) + (0.7152f * p.g) + (0.0722f * p.b);
                min_luminance = std::min(min_luminance, luminance);
                max_luminance = std::max(max_luminance, luminance);
                sum_luminance += static_cast<double>(luminance);
            }
            result["min_luminance"]  = min_luminance;
            result["max_luminance"]  = max_luminance;
            result["mean_luminance"] = pixels.empty() ? 0.0 : (sum_luminance / static_cast<double>(pixels.size()));
        }
        m_scene_image_capture.reset();
        if (!written) {
            return make_error_content("render_scene_image: failed to write '" + path.string() + "'");
        }
        return make_json_content(result).dump();
    }

    // The capture of a dropped request whose copy is still in flight (see
    // release_abandoned_scene_image_capture()): one capture at a time, so wait
    // for it to retire.
    if (m_scene_image_capture) {
        release_abandoned_scene_image_capture();
        if (m_scene_image_capture) {
            m_defer_current_request = true;
            return {};
        }
    }

    if ((m_context.graphics_device == nullptr) || (m_context.rendergraph == nullptr) || (m_context.app_scenes == nullptr) || (m_context.scene_views == nullptr)) {
        return make_error_content("render_scene_image: rendering is not available");
    }

    // Scene: by name, else the single open scene.
    std::shared_ptr<Scene_root> scene_root{};
    const std::string scene_name = args.value("scene", std::string{});
    if (!scene_name.empty()) {
        for (const std::shared_ptr<Scene_root>& candidate : m_context.app_scenes->get_scene_roots()) {
            if (candidate->get_name() == scene_name) {
                scene_root = candidate;
                break;
            }
        }
        if (!scene_root) {
            return make_error_content("render_scene_image: scene not found: " + scene_name);
        }
    } else {
        scene_root = m_context.app_scenes->get_single_scene_root();
        if (!scene_root) {
            return make_error_content("render_scene_image: 'scene' is required unless exactly one scene is open");
        }
    }

    // Size, output, options.
    constexpr int c_max_size = 8192;
    const int width  = args.value("width",  1280);
    const int height = args.value("height", 720);
    if ((width < 1) || (height < 1) || (width > c_max_size) || (height > c_max_size)) {
        return make_error_content(fmt::format("render_scene_image: width and height must be in [1, {}]", c_max_size));
    }
    const std::string output_name = args.value("output", std::string{"png"});
    Scene_image_output output = Scene_image_output::png;
    if (output_name == "linear") {
        output = Scene_image_output::linear;
    } else if (output_name != "png") {
        return make_error_content("render_scene_image: 'output' must be \"png\" or \"linear\"");
    }
    const std::string path = args.value(
        "path",
        std::string{(output == Scene_image_output::png) ? "logs/render_scene_image.png" : "logs/render_scene_image.pfm"}
    );
    const int shader_debug_value = args.value("shader_debug", 0);
    if ((shader_debug_value < 0) || (shader_debug_value >= static_cast<int>(std::size(erhe::scene_renderer::c_shader_debug_strings)))) {
        return make_error_content("render_scene_image: 'shader_debug' is out of range");
    }
    const int preset_msaa  = (m_context.app_settings != nullptr) ? m_context.app_settings->graphics.current_graphics_preset.msaa_sample_count : 0;
    const int msaa_samples = args.value("msaa_samples", preset_msaa);
    if ((msaa_samples < 0) || (msaa_samples > 16)) {
        return make_error_content("render_scene_image: 'msaa_samples' must be in [0, 16]");
    }

    // Camera: an existing scene camera, or an explicit pose + projection.
    std::shared_ptr<erhe::scene::Camera> camera{};
    json camera_json{};
    const bool has_camera_node = args.contains("camera_node");
    const bool has_camera      = args.contains("camera");
    if (has_camera_node == has_camera) {
        return make_error_content("render_scene_image: give exactly one of 'camera' {eye, target, ...} and 'camera_node' (camera name or id)");
    }
    if (has_camera_node) {
        const json& selector = args["camera_node"];
        for (const std::shared_ptr<erhe::scene::Camera>& candidate : scene_root->get_scene().get_cameras()) {
            const bool match = selector.is_number_integer()
                ? (candidate->get_id() == selector.get<std::size_t>())
                : (selector.is_string() && (candidate->get_name() == selector.get<std::string>()));
            if (match) {
                camera = candidate;
                break;
            }
        }
        if (!camera) {
            return make_error_content("render_scene_image: camera not found in scene: " + selector.dump());
        }
        camera_json = json{{"source", "camera_node"}, {"name", camera->get_name()}, {"id", camera->get_id()}};
    } else {
        const json& camera_args = args["camera"];
        if (!camera_args.is_object()) {
            return make_error_content("render_scene_image: 'camera' must be an object {eye, target, up, fov_y_degrees, near, far, exposure, shadow_range}");
        }
        const std::optional<glm::vec3> eye    = parse_json_vec3(camera_args.value("eye",    json{}));
        const std::optional<glm::vec3> target = parse_json_vec3(camera_args.value("target", json{}));
        const std::optional<glm::vec3> up     = camera_args.contains("up") ? parse_json_vec3(camera_args["up"]) : std::optional<glm::vec3>{glm::vec3{0.0f, 1.0f, 0.0f}};
        if (!eye.has_value() || !target.has_value() || !up.has_value()) {
            return make_error_content("render_scene_image: camera 'eye' and 'target' (and 'up' when given) must be finite [x, y, z]");
        }
        if (glm::length(target.value() - eye.value()) < 1.0e-6f) {
            return make_error_content("render_scene_image: camera 'eye' and 'target' coincide");
        }
        const float fov_y_degrees = camera_args.value("fov_y_degrees", 60.0f);
        const float z_near        = camera_args.value("near", 0.03f);
        const float z_far         = camera_args.value("far", 200.0f);
        const float exposure      = camera_args.value("exposure", 1.0f);
        const float shadow_range  = camera_args.value("shadow_range", 22.0f);
        if (!(fov_y_degrees > 0.0f) || !(fov_y_degrees < 180.0f) || !(z_near > 0.0f) || !(z_far > z_near) || !(shadow_range > 0.0f)) {
            return make_error_content("render_scene_image: need 0 < fov_y_degrees < 180, 0 < near < far and shadow_range > 0");
        }
        camera = std::make_shared<erhe::scene::Camera>("render_scene_image camera");
        camera->set_projection_type   (erhe::scene::Projection::Type::perspective_vertical);
        camera->set_fov_y             (glm::radians(fov_y_degrees));
        camera->set_perspective_z_near(z_near);
        camera->set_perspective_z_far (z_far);
        camera->set_exposure          (exposure);
        camera->set_shadow_range      (shadow_range);
        camera->set_parent_from_node  (erhe::math::create_look_at(eye.value(), target.value(), up.value()));
        camera_json = json{
            {"source",        "explicit"},
            {"eye",           json::array({eye->x, eye->y, eye->z})},
            {"target",        json::array({target->x, target->y, target->z})},
            {"up",            json::array({up->x, up->y, up->z})},
            {"fov_y_degrees", fov_y_degrees},
            {"near",          z_near},
            {"far",           z_far},
            {"exposure",      exposure},
            {"shadow_range",  shadow_range}
        };
    }

    // Light for Shader_debug::shadow_visibility: by light name or id. The
    // render resolves it to its light slot (the result reports that slot).
    std::shared_ptr<erhe::scene::Light> shadow_debug_light{};
    json shadow_debug_light_json{};
    if (args.contains("shadow_debug_light")) {
        const json& selector = args["shadow_debug_light"];
        for (const std::shared_ptr<erhe::scene::Light_layer>& light_layer : scene_root->get_scene().get_light_layers()) {
            for (const std::shared_ptr<erhe::scene::Light>& candidate : light_layer->lights) {
                const bool match = selector.is_number_integer()
                    ? (candidate->get_id() == selector.get<std::size_t>())
                    : (selector.is_string() && (candidate->get_name() == selector.get<std::string>()));
                if (match) {
                    shadow_debug_light = candidate;
                    break;
                }
            }
            if (shadow_debug_light) {
                break;
            }
        }
        if (!shadow_debug_light) {
            return make_error_content("render_scene_image: light not found in scene: " + selector.dump());
        }
        shadow_debug_light_json = json{{"name", shadow_debug_light->get_name()}, {"id", shadow_debug_light->get_id()}};
    }

    m_scene_image_capture = std::make_unique<Scene_image_capture>(
        m_context,
        Scene_image_capture_create_info{
            .scene_root        = scene_root,
            .camera            = camera,
            .width             = width,
            .height            = height,
            .msaa_sample_count = msaa_samples,
            .output            = output,
            .shader_debug      = static_cast<erhe::scene_renderer::Shader_debug>(shader_debug_value),
            .shadow_debug_light = shadow_debug_light
        }
    );
    m_scene_image_header = json{
        {"path",         path},
        {"width",        width},
        {"height",       height},
        {"output",       output_name},
        {"scene",        scene_root->get_name()},
        {"camera",       camera_json},
        {"msaa_samples", msaa_samples},
        {"shader_debug", shader_debug_value}
    };
    if (shadow_debug_light) {
        m_scene_image_header["shadow_debug_light"] = shadow_debug_light_json;
    }
    m_scene_image_request     = m_current_request;
    m_scene_image_enqueued_at = m_current_request->enqueued_at;
    m_defer_current_request   = true;
    return {};
}

} // namespace editor
