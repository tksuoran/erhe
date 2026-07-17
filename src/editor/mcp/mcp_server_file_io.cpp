// Mcp_server file I/O tools (save/load scene, glTF export/import, screenshot capture).
// Split out of mcp_server.cpp; shares helpers via mcp_server_shared.hpp.

#include "mcp/mcp_server_shared.hpp"

#include "app_message_bus.hpp"
#include "operations/operation_stack.hpp"
#include "operations/scene_open_operation.hpp"
#include "parsers/gltf.hpp"
#include "parsers/gltf_extensions_export.hpp"
#include "prefabs/prefab_library.hpp"

#include "erhe_file/file.hpp"
#include "erhe_gltf/gltf.hpp"

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
    // Scenes are saved as single erhe-authored glTF files
    // (doc/gltf-scene-roundtrip-plan.md phase 4), matching File > Save Scene:
    // without 'path' the scene saves to its own source file when it was
    // opened/loaded from one, else to res/editor/scenes/<scene name>.glb.
    // An explicit path is normalized to carry a glTF extension (.glb
    // appended when missing; .gltf is honored and selects the text form).
    std::filesystem::path path;
    if (path_str.empty()) {
        path = resolve_scene_save_path(*sr);
    } else {
        path = std::filesystem::path{path_str};
        if (
            (path.extension() != std::filesystem::path{".glb"}) &&
            (path.extension() != std::filesystem::path{".gltf"})
        ) {
            path = std::filesystem::path{path.string() + ".glb"};
        }
    }
    // save_scene_gltf also reloads the prefab when 'path' is a loaded prefab
    // source, refreshing every instance in every scene (this subsumed the
    // former save_prefab tool).
    const bool ok = editor::save_scene_gltf(m_context, *sr, path);
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
        sr->set_source_path(path);
    }
    return make_json_content({
        {"saved", true},
        {"path",  path.string()}
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
    // Queue the close (same path as the Scene row's Close context menu entry):
    // the teardown destroys ImGui windows, so it runs from the message bus pump
    // on a following frame, outside ImGui iteration.
    m_context.app_message_bus->close_scene.queue_message(
        Close_scene_message{
            .scene_root = std::dynamic_pointer_cast<Scene_root>(sr->shared_from_this())
        }
    );
    return make_json_content({
        {"queued",     true},
        {"scene_name", sr->get_name()}
    }).dump();
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
    // + viewport windows, ERHE_scene state applied; not undoable) and routes
    // a foreign glTF to Scene_open_operation. Queued so the window setup runs
    // from the message pump on a following frame, outside ImGui iteration.
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
    const erhe::gltf::Gltf_physics_data physics_data = build_gltf_physics_data(sr->get_scene(), sr->get_content_library().get());
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
        // graph-mesh exclusion (doc/gltf-scene-roundtrip-plan.md phase 3).
        // The default export stays plain interchange.
        add_gltf_editor_state(export_arguments, *sr);
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

    editor::import_gltf(m_context, make_import_build_info(m_context), scene_root, path);
    return make_json_content({
        {"imported", true},
        {"path",     path_str}
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
    const std::shared_ptr<Prefab> prefab = m_context.prefab_library->get_or_load(path);
    if (!prefab) {
        return make_error_content("Failed to load prefab (missing file, no nodes, or reference cycle - see log): " + path_str);
    }

    const glm::vec3 position        = get_vec3(args, "position", glm::vec3{0.0f});
    const glm::mat4 world_from_node = erhe::math::create_translation<float>(position);

    const std::shared_ptr<erhe::scene::Node> node = instantiate_prefab(m_context, prefab, *sr, world_from_node);
    if (!node) {
        return make_error_content("Failed to instantiate prefab: " + path_str);
    }
    // The insertion is queued as an undoable operation; the node enters the
    // scene when the operation executes (same frame, after this call).
    return make_json_content({
        {"instantiated", true},
        {"queued",       true},
        {"path",         prefab->source_path.generic_string()},
        {"node_id",      node->get_id()},
        {"node_name",    node->get_name()}
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

auto Mcp_server::query_prefabs(const json& args) -> std::string
{
    static_cast<void>(args);
    if (m_context.prefab_library == nullptr) {
        return make_error_content("Prefab library not available");
    }
    json prefabs = json::array();
    for (const auto& [path, prefab] : m_context.prefab_library->get_prefabs()) {
        std::size_t node_count = 0;
        for (const std::shared_ptr<erhe::scene::Node>& node : prefab->gltf_data.nodes) {
            if (node) {
                ++node_count;
            }
        }
        prefabs.push_back({
            {"path",            path.generic_string()},
            {"name",            prefab->name},
            {"nodes",           node_count},
            {"meshes",          prefab->gltf_data.meshes.size()},
            {"materials",       prefab->gltf_data.materials.size()},
            {"textures",        prefab->gltf_data.images.size()},
            {"skins",           prefab->gltf_data.skins.size()},
            {"animations",      prefab->gltf_data.animations.size()},
            {"external_assets", prefab->gltf_data.external_assets.size()}
        });
    }
    return make_json_content({{"prefabs", prefabs}}).dump();
}

auto Mcp_server::action_capture_screenshot(const json& args) -> std::string
{
    if (m_context.graphics_device == nullptr) {
        json r = make_text_content("Graphics device not available");
        r["isError"] = true;
        return r.dump();
    }

    const std::string path_str = args.value("path", std::string{"logs/mcp_screenshot.png"});

    int                      width  = 0;
    int                      height = 0;
    erhe::dataformat::Format format = erhe::dataformat::Format::format_8_vec4_srgb;
    std::vector<std::byte>   pixels;
    if (!m_context.graphics_device->capture_last_frame(width, height, format, pixels)) {
        json r = make_text_content(
            "Frame capture not available. Screenshots are currently only supported in the headless "
            "Vulkan configuration (emulated swapchain), and require at least one rendered frame."
        );
        r["isError"] = true;
        return r.dump();
    }

    std::unique_ptr<erhe::graphics::Image_writer> writer = erhe::graphics::Image_writer::create();
    const int row_stride = width * 4;
    if (!writer->write_png(std::filesystem::path{path_str}, width, height, row_stride, format, pixels)) {
        json r = make_text_content("Failed to write PNG '" + path_str + "' (image writer backend may be disabled)");
        r["isError"] = true;
        return r.dump();
    }

    return make_json_content({
        {"path",   path_str},
        {"width",  width},
        {"height", height}
    }).dump();
}


} // namespace editor
