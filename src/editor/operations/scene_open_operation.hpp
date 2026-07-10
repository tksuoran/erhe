#pragma once

#include "operations/operation.hpp"

#include <filesystem>
#include <memory>

namespace editor {

class Content_library;
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
    explicit Scene_open_operation(const std::filesystem::path& path);

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

private:
    std::filesystem::path            m_path;
    std::shared_ptr<Scene_root>      m_scene_root;
    std::shared_ptr<Content_library> m_content_library;
};

}
