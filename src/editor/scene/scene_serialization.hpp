#pragma once

#include <filesystem>
#include <memory>

namespace editor {

class App_context;
class App_message_bus;
class App_scenes;
class Content_library;
class Scene_root;

// Save a scene as a directory bundle (#241), similar to an .xcodeproj bundle.
// bundle_dir is the .erhescene directory; it is created if needed. Inside it the
// scene is written as:
//   scene.json          root JSON (Scene_file; current version in scene/definitions/scene_file.py)
//   data.glb            meshes + materials (only when the scene has meshes)
//   mesh_<i>_p<p>.geogram  geometry-normative primitives
// The companion window layout (imgui.ini) is written separately by the caller
// via scene_imgui_ini_path().
auto save_scene(
    const Scene_root&            scene_root,
    const std::filesystem::path& bundle_dir
) -> bool;

// Companion ImGui layout path inside a scene directory bundle: <bundle_dir>/imgui.ini.
// The editor saves the window-docking layout there when a scene is saved and
// restores it when the scene is loaded.
auto scene_imgui_ini_path(const std::filesystem::path& bundle_dir) -> std::filesystem::path;

// Load a scene directory bundle (#241). bundle_dir is the .erhescene directory;
// the root JSON is read from <bundle_dir>/scene.json and all referenced files are
// resolved relative to bundle_dir. Returns nullptr if scene.json is missing or
// cannot be parsed.
auto load_scene(
    App_context*                            context,
    App_message_bus*                        app_message_bus,
    App_scenes*                             app_scenes,
    const std::shared_ptr<Content_library>& content_library,
    const std::filesystem::path&            bundle_dir
) -> std::shared_ptr<Scene_root>;

}
