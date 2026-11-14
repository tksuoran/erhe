#pragma once

#include "scene/scene_save_service.hpp"

#include <memory>
#include <string>

namespace editor {

class App_context;
class Scene_root;

// Who runs the build: the status line title and the error prefix name it.
enum class Navigation_build_purpose : unsigned int
{
    build_command = 0, // Build Navigation Mesh
    export_stage       // the navigation stage of an Export (doc/editor/navigation.md "Export")
};

// Why Build Navigation Mesh refuses the scene: no export settings, or no navigation seed; empty when it does not.
[[nodiscard]] auto get_navigation_build_refusal(const Scene_root& scene_root) -> std::string;

// Main thread: Build Navigation Mesh with its settings, seeds and input list (doc/editor/navigation.md "Job");
// null, with out_refusal set, when get_navigation_build_refusal() refuses the scene.
[[nodiscard]] auto make_navigation_build_job(
    App_context&                       context,
    const std::shared_ptr<Scene_root>& scene_root,
    Navigation_build_purpose           purpose,
    std::string&                       out_refusal
) -> std::shared_ptr<Scene_service_job>;

}
