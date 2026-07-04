// Mcp_server file I/O tools (save/load scene, glTF export/import, screenshot capture).
// Split out of mcp_server.cpp; shares helpers via mcp_server_shared.hpp.

#include "mcp/mcp_server_shared.hpp"

#include "app_message_bus.hpp"

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
    const std::string gltf = erhe::gltf::export_gltf(*root_node, binary, &physics_data);
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

    editor::import_gltf(
        m_context,
        erhe::primitive::Build_info{
            .primitive_types = {
                .fill_triangles          = true,
                .fill_triangles_expanded = true,
                .edge_lines              = true,
                .corner_points           = true,
                .centroid_points         = true
            },
            .buffer_info = m_context.mesh_memory->make_primitive_buffer_info()
        },
        scene_root,
        path
    );
    return make_json_content({
        {"imported", true},
        {"path",     path_str}
    }).dump();
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
