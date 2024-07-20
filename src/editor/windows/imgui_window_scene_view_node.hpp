#pragma once

#include "erhe_rendergraph/texture_rendergraph_node.hpp"
#include "erhe_imgui/imgui_window.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::imgui {
    class Imgui_host;
    class Imgui_windows;
}
namespace erhe::rendergraph {
    class Multisample_resolve_node;
    class Rendergraph_node;
}

namespace editor {

class Editor_context;
class Post_processing_node;
class Viewport_scene_view;

// Rendergraph sink node for showing contents originating from Viewport_scene_view
class Imgui_window_scene_view_node
    : public erhe::imgui::Imgui_window
    , public erhe::rendergraph::Texture_rendergraph_node
{
public:
    Imgui_window_scene_view_node(
        erhe::imgui::Imgui_renderer&                imgui_renderer,
        erhe::imgui::Imgui_windows&                 imgui_windows,
        erhe::rendergraph::Rendergraph&             rendergraph,
        Editor_context&                             editor_context,
        const std::string_view                      name,
        const std::string_view                      ini_label,
        const std::shared_ptr<Viewport_scene_view>& viewport_scene_view
    );

    // Implements Imgui_window
    void imgui               () override;
    void hidden              () override;
    auto has_toolbar         () const -> bool override { return true; }
    void toolbar             (bool& hovered) override;
    void on_begin            () override;
    void on_end              () override;
    void set_imgui_host      (erhe::imgui::Imgui_host* imgui_host) override;
    auto want_mouse_events   () const -> bool override;
    auto want_keyboard_events() const -> bool override;
    void on_mouse_move       (glm::vec2 mouse_position_in_window);

    // Implements Rendergraph_node
    auto get_consumer_input_viewport(erhe::rendergraph::Routing resource_routing, int key, int depth = 0) const -> erhe::math::Viewport override;

    // Overridden to source viewport size from ImGui window
    auto get_producer_output_viewport(erhe::rendergraph::Routing resource_routing, int key, int depth = 0) const -> erhe::math::Viewport override;

    // Public API
    [[nodiscard]] auto viewport_scene_view() const -> std::shared_ptr<Viewport_scene_view>;

private:
    void drag_and_drop_target(float min_x, float min_y, float max_x, float max_y);
    void cancel_brush_drag_and_drop();

    Editor_context&                    m_editor_context;
    std::weak_ptr<Viewport_scene_view> m_viewport_scene_view;
    erhe::math::Viewport               m_viewport;
    bool                               m_brush_drag_and_drop_active{false};
};

} // namespace editor
