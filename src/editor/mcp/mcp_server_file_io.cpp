// Mcp_server file I/O tools (save/load scene, glTF export/import, screenshot capture).
// Split out of mcp_server.cpp; shares helpers via mcp_server_shared.hpp.

#include "mcp/mcp_server_shared.hpp"

#include "app_message_bus.hpp"
#include "operations/operation_stack.hpp"
#include "operations/scene_open_operation.hpp"
#include "prefabs/prefab_library.hpp"

#include "erhe_file/file.hpp"

namespace editor {

using namespace mcp_server_detail;

auto Mcp_server::action_save_scene(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    const std::string path_str   = args.value("path", "");
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
    // Scenes are saved as directory bundles (#241): treat path as the bundle
    // directory and normalize its name to carry the .erhescene extension, matching
    // the File > Save Scene dialog behavior.
    std::filesystem::path bundle{path_str};
    if (bundle.extension() != std::filesystem::path{".erhescene"}) {
        bundle = std::filesystem::path{bundle.string() + ".erhescene"};
    }
    const bool ok = editor::save_scene(*sr, bundle);
    if (!ok) {
        json r = make_text_content("save_scene failed: " + bundle.string());
        r["isError"] = true;
        return r.dump();
    }
    // Persist the current window-docking layout inside the bundle so a later load
    // can restore how the windows were docked at save time.
    if (m_context.imgui_windows != nullptr) {
        m_context.imgui_windows->save_imgui_ini(editor::scene_imgui_ini_path(bundle).string());
    }
    // Rescan the asset browser so the freshly saved bundle appears without a
    // manual Scan (#256).
    m_context.app_message_bus->scene_saved.send_message(Scene_saved_message{.path = bundle});
    return make_json_content({
        {"saved", true},
        {"path",  bundle.string()}
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
    // path is the .erhescene bundle directory (#241); load_scene reads scene.json
    // from inside it. Each loaded scene gets its own content library, mirroring the
    // file-dialog load path in Operations::load_scene.
    const std::filesystem::path path{path_str};
    std::shared_ptr<Content_library> content_library = std::make_shared<Content_library>();
    std::shared_ptr<Scene_root> scene_root = editor::load_scene(
        &m_context,
        m_context.app_message_bus,
        m_context.app_scenes,
        content_library,
        path
    );
    if (!scene_root) {
        json r = make_text_content("load_scene failed: " + path_str);
        r["isError"] = true;
        return r.dump();
    }
    return make_json_content({
        {"loaded",     true},
        {"path",       path_str},
        {"scene_name", scene_root->get_name()}
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
    const erhe::gltf::Gltf_physics_data physics_data = build_gltf_physics_data(sr->get_scene());
    // Prefab instances export as glTF 2.1 externalAsset references instead
    // of flattened content; URIs are relativized against the export
    // directory.
    const std::filesystem::path export_path{path_str};
    const std::string gltf = erhe::gltf::export_gltf(
        erhe::gltf::Gltf_export_arguments{
            .root_node       = *root_node,
            .binary          = binary,
            .physics_data    = &physics_data,
            .external_assets = collect_prefab_external_assets(*root_node, export_path.parent_path())
        }
    );
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
