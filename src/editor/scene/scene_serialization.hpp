#pragma once

#include <filesystem>
#include <memory>

namespace editor {

class App_context;
class App_message_bus;
class App_scenes;
class Content_library;
class Scene_root;

auto save_scene(
    const Scene_root&            scene_root,
    const std::filesystem::path& path
) -> bool;

// Companion ImGui layout (.ini) path for a scene file: <dir>/<stem>_imgui.ini.
// The editor saves the window-docking layout there when a scene is saved and
// restores it when the scene is loaded.
auto scene_imgui_ini_path(const std::filesystem::path& scene_path) -> std::filesystem::path;

auto load_scene(
    App_context*                            context,
    App_message_bus*                        app_message_bus,
    App_scenes*                             app_scenes,
    const std::shared_ptr<Content_library>& content_library,
    const std::filesystem::path&            path
) -> std::shared_ptr<Scene_root>;

}
