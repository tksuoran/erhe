#pragma once

#include "erhe_rendergraph/texture_rendergraph_node.hpp"
#include "erhe_imgui/imgui_window.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace ImViewGuizmo {
    class Context;
}
namespace erhe::imgui {
    class Imgui_host;
    class Imgui_windows;
}
namespace erhe::rendergraph {
    class Multisample_resolve_node;
    class Rendergraph_node;
}

namespace editor {

class App_context;
class App_message;
class App_message_bus;
class Post_processing_node;
class Viewport_scene_view;

class Viewport_window : public erhe::imgui::Imgui_window
{
public:
    Viewport_window(
        erhe::imgui::Imgui_renderer&                                imgui_renderer,
        erhe::imgui::Imgui_windows&                                 imgui_windows,
        const std::shared_ptr<erhe::rendergraph::Rendergraph_node>& rendergraph_output_node,
        App_context&                                                app_context,
        App_message_bus&                                            app_message_bus,
        std::string_view                                            name,
        std::string_view                                            ini_label,
        const std::shared_ptr<Viewport_scene_view>&                 viewport_scene_view
    );
    ~Viewport_window() noexcept override;

    // Implements Imgui_window
    void imgui                    () override;
    void hidden                   () override;
    auto has_toolbar              () const -> bool override { return true; }
    void toolbar                  (bool& hovered) override;
    void on_begin                 () override;
    void on_end                   () override;
    void set_imgui_host           (erhe::imgui::Imgui_host* imgui_host) override;
    auto want_mouse_events        () const -> bool override;
    auto want_keyboard_events     () const -> bool override;
    auto want_cursor_relative_hold() const -> bool override;
    void on_mouse_move            (glm::vec2 mouse_position_in_window);
    void update_hover_info        ();

    // Public API
    [[nodiscard]] auto viewport_scene_view() const -> std::shared_ptr<Viewport_scene_view>;

private:
    void on_message(App_message& message);
    void drag_and_drop_target(float min_x, float min_y, float max_x, float max_y);
    void cancel_brush_drag_and_drop();

    App_context&                                       m_app_context;
    std::weak_ptr<Viewport_scene_view>                 m_viewport_scene_view;
    std::weak_ptr<erhe::rendergraph::Rendergraph_node> m_rendergraph_output_node;
    bool                                               m_brush_drag_and_drop_active{false};

    std::unique_ptr<ImViewGuizmo::Context>             m_nagivation_gizmo;
    bool                                               m_request_cursor_relative_hold{false};
};

}
