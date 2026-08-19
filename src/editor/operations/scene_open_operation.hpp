#pragma once

#include "operations/operation.hpp"

#include <filesystem>
#include <memory>

namespace editor {

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
    // produced (doc/async-asset-loading-plan.md step 7): the operation then
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

}
