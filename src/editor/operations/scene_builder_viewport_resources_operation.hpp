#pragma once

#include "operations/operation.hpp"

#include <memory>
#include <string>

namespace erhe::scene {
    class Camera;
}

namespace editor {

class Post_processing_node;
class Scene_root;
class Shadow_render_node;
class Viewport_scene_view;
class Viewport_window;

// Wraps the eager Scene_views / App_rendering side-effects that
// Scene_builder::add_cameras used to perform after queuing the
// camera-insertion Compound_operation. execute() builds the desktop
// viewport (Viewport_scene_view, Viewport_window, optional
// Post_processing_node, Shadow_render_node) via the Scene_views and
// App_rendering APIs; undo() drops them via the corresponding
// destroy_* tear-down APIs and resets the captured shared_ptrs so the
// Rendergraph_node destructors run and unregister the nodes.
//
// Redo re-creates fresh instances; the user-visible window name
// ("Default Viewport") and bound camera stay the same, but the
// Viewport_scene_view / Viewport_window / Post_processing_node
// identities (Unique_id<...>) change between redo cycles. That is
// fine for the add_cameras use case where nothing in saved state
// (including imgui.ini layout) keys off those identities.
class Scene_builder_viewport_resources_operation : public Operation
{
public:
    class Parameters
    {
    public:
        std::shared_ptr<Scene_root>          scene_root;
        std::shared_ptr<erhe::scene::Camera> camera;
        std::string                          name;
        bool                                 enable_post_processing{true};
    };

    explicit Scene_builder_viewport_resources_operation(Parameters parameters);
    ~Scene_builder_viewport_resources_operation() noexcept override;

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

private:
    Parameters                            m_parameters;
    std::shared_ptr<Viewport_scene_view>  m_viewport_scene_view;
    std::shared_ptr<Viewport_window>      m_viewport_window;
    std::shared_ptr<Post_processing_node> m_post_processing_node;
    std::shared_ptr<Shadow_render_node>   m_shadow_render_node;
};

} // namespace editor
