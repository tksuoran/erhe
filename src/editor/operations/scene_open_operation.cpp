#include "operations/scene_open_operation.hpp"

#include "app_context.hpp"
#include "app_scenes.hpp"
#include "app_settings.hpp"
#include "content_library/content_library.hpp"
#include "parsers/gltf.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_scene_views.hpp"
#include "windows/item_tree_window.hpp"
#include "windows/window_placement.hpp"

#include "erhe_file/file.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

namespace editor {

Scene_open_operation::Scene_open_operation(const std::filesystem::path& path)
    : m_path{path}
{
    set_description(
        fmt::format("[{}] Scene_open_operation(path = {})", get_serial(), m_path.string())
    );
}

void Scene_open_operation::execute(App_context& context)
{
    ERHE_PROFILE_FUNCTION();

    const bool first_time = !m_scene_root;
    if (first_time) {
        m_content_library = std::make_shared<Content_library>();

        const bool enable_physics = context.editor_settings->physics.static_enable;
        m_scene_root = std::make_shared<Scene_root>(
            context.app_message_bus,
            m_content_library,
            erhe::file::to_string(m_path.filename()),
            enable_physics
        );
        // Remember which glTF file the scene came from (canonical, matching
        // Prefab_library keys): Save Scene writes back here and propagates
        // prefab edits to instances when the file is a loaded prefab.
        std::error_code error_code;
        const std::filesystem::path canonical_path = std::filesystem::weakly_canonical(m_path, error_code);
        m_scene_root->set_source_path(error_code ? m_path : canonical_path);
    }
    m_scene_root->register_to_editor_scenes(*context.app_scenes);

    // The content library is shown nested under the Scene row in the Hierarchy
    // (browser) window (#240); the standalone Content Library window was removed
    // (#241). Re-create the browser window every time the operation executes
    // (initial run + redo); undo() drops it via remove_browser_window().
    auto browser_window = m_scene_root->make_browser_window(
        *context.imgui_renderer,
        *context.imgui_windows,
        context,
        *context.app_settings
    );
    browser_window->show_window();
    // Dock (tab) the new scene's Hierarchy window with the existing one
    // instead of leaving it floating at ImGui's default cascade position
    // (#258). ImGuiCond_FirstUseEver keeps a remembered layout intact when
    // the window identity already has persisted ini state (e.g. redo).
    apply_hierarchy_window_placement(*context.imgui_windows, *browser_window);

    if (first_time) {
        // Execute the import compound inline as part of this operation
        // instead of queueing it: an executing operation must not re-enter
        // the Operation_stack (except to queue()), and "Open scene" must be
        // a single undo entry. The compound is one-shot: undo() only
        // unregisters the scene -- the imported content stays alive inside
        // m_scene_root, so redo re-registers it without re-importing.
        std::shared_ptr<Operation> import_operation = make_import_gltf_operation(context, make_import_build_info(context), m_scene_root, m_path);
        import_operation->execute(context);

        // Show the opened scene in a NEW viewport window, like "Create Scene"
        // does. Pre-existing viewport windows keep the scene and camera they
        // were showing. (Historically an Open_scene_message rebound every
        // live viewport to the opened scene -- while keeping its old camera,
        // leaving a cross-scene camera binding.) Only on the first execute:
        // redo re-registers the scene, and the viewport window from the
        // first execute is still alive.
        if (context.scene_views != nullptr) {
            context.scene_views->open_new_viewport_scene_view_node(m_scene_root);
        }
    }
}

void Scene_open_operation::undo(App_context& context)
{
    ERHE_VERIFY(m_scene_root);
    m_scene_root->unregister_from_editor_scenes(*context.app_scenes);
    m_scene_root->remove_browser_window();
}

}
