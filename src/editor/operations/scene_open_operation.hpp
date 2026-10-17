#pragma once

#include "assets/asset_load_task.hpp"
#include "operations/operation.hpp"

#include <filesystem>
#include <functional>
#include <memory>

namespace editor {

class App_context;
class Content_library;
class Prepared_gltf_parse;
class Scene_root;

// Opens a glTF file as a new scene: creates the Scene_root + content
// library, registers it with App_scenes, creates the browser window, and
// (first execution only) runs the glTF import compound inline -- see
// make_import_gltf_operation(). undo() unregisters the scene and drops the
// browser window; the imported content stays alive inside the kept
// Scene_root, so redo re-registers without re-importing. One entry on the
// undo stack covers the whole open.
class Scene_open_operation : public Operation
{
public:
    // prepared_parse, when given, is a parse an asynchronous load already
    // produced (doc/editor/async_asset_loading_design.md step 7): the operation then
    // does no file I/O at all and stays the cheap, synchronous thing an
    // undoable operation is supposed to be. It is consumed by the FIRST
    // execute; a redo re-registers the kept Scene_root without re-importing,
    // exactly as before.
    explicit Scene_open_operation(
        const std::filesystem::path&                path,
        const std::shared_ptr<Prepared_gltf_parse>& prepared_parse = {}
    );

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

private:
    std::filesystem::path                m_path;
    std::shared_ptr<Prepared_gltf_parse> m_prepared_parse;
    std::shared_ptr<Scene_root>      m_scene_root;
    std::shared_ptr<Content_library> m_content_library;
};

// The one glTF new-scene open path; which entry point passes which
// Erhe_scene_open is listed in doc/editor/async_asset_loading.md "Entry
// points". Queues the load with
// Asset_manager::queue_load - scan and parse on workers - and, when it
// finishes, queues Scene_open_operation with the prepared parse for a foreign
// glTF, or hands an erhe-authored scene opened in full (Erhe_scene_open::
// full_scene) to on_erhe_scene_opened; a cancelled or failed load opens
// nothing. With asynchronous loading off
// (queue_load returns null) the same decision runs on the blocking path.
void queue_open_scene_gltf(
    App_context&                                            context,
    const std::filesystem::path&                            path,
    Erhe_scene_open                                         erhe_scene_open,
    std::function<void(const std::shared_ptr<Scene_root>&)> on_erhe_scene_opened = {}
);

}
