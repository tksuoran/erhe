#pragma once

#include <filesystem>
#include <memory>

namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_windows;
}
namespace erhe::scene {
    class Scene_message_bus;
}

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

auto load_scene(
    erhe::imgui::Imgui_renderer*            imgui_renderer,
    erhe::imgui::Imgui_windows*             imgui_windows,
    erhe::scene::Scene_message_bus&         scene_message_bus,
    App_context*                            context,
    App_message_bus*                        app_message_bus,
    App_scenes*                             app_scenes,
    const std::shared_ptr<Content_library>& content_library,
    const std::filesystem::path&            path
) -> std::shared_ptr<Scene_root>;

}
