#pragma once

#include <filesystem>

namespace editor {

class App_context;
class Scene_root;

// Main thread: the installed navigation mesh as the navigation mesh item of the editor asset of source_path
// (record_path: the editor asset that file records), replacing the previous one and writing the record
// navigation.key in one write transaction (doc/editor/navigation.md "Saving"); Scene_navigation::get_item() then
// names it. False when nothing is stored: no mesh installed, no source file, or (logged) no writable editor asset.
auto store_navigation_item(
    App_context&                 context,
    Scene_root&                  scene_root,
    const std::filesystem::path& source_path,
    const std::filesystem::path& record_path
) -> bool;

// Main thread: removes the navigation mesh items of the scene's editor asset and clears the record.
void remove_navigation_item(App_context& context, Scene_root& scene_root);

// Main thread, outside the ImGui phase: Clear - the mesh, its visualization node and the saved item.
void clear_navigation(App_context& context, Scene_root& scene_root);

// Main thread, with the upload command buffer recording, once the opened scene's nodes are in it: installs the
// item the record names and judges it against the opened scene. A missing, unreadable or older item installs nothing.
void load_navigation_item(App_context& context, Scene_root& scene_root);

}
