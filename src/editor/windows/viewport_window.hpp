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

class App_context;
class Asset_file_gltf;
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
        std::string_view                                            name,
        std::string_view                                            ini_label,
        int                                                         window_slot,
        const std::shared_ptr<Viewport_scene_view>&                 viewport_scene_view
    );
    ~Viewport_window() noexcept override;

    // Implements Imgui_window
    void imgui                    () override;
    void hidden                   () override;
    auto flags                    () -> ImGuiWindowFlags override;
    void on_begin                 () override;
    void on_end                   () override;
    void set_imgui_host           (erhe::imgui::Imgui_host* imgui_host) override;
    auto want_mouse_events        () const -> bool override;
    auto want_keyboard_events     () const -> bool override;
    auto want_cursor_relative_hold() const -> bool override;

    void draw_toolbar             ();
    void on_mouse_move            (glm::vec2 mouse_position_in_window);
    void update_hover_info        ();

    // Public API
    [[nodiscard]] auto viewport_scene_view() const -> std::shared_ptr<Viewport_scene_view>;
    // Stable per-window slot (1, 2, ...) carried in the "###Viewport_window N"
    // ImGui ID and the "Viewport_window N" ini label. The lowest free slot is
    // reused after a viewport window is destroyed, so window layout and open
    // state persist across sessions independent of scene names and creation
    // order (issue #265).
    [[nodiscard]] auto get_window_slot() const -> int { return m_window_slot; }
    [[nodiscard]] auto is_viewport_focused() const -> bool { return m_viewport_child_window_focused; }
    [[nodiscard]] auto is_viewport_hovered() const -> bool { return m_viewport_child_window_hovered; }
    // ImGui focus state (window or any of its children) as of this window's
    // last imgui() frame. Used by Scene_views to skip focusing a viewport of
    // the newly active scene when one is already focused.
    [[nodiscard]] auto is_window_focused  () const -> bool { return m_was_focused; }

private:
    void imgui_viewport            ();
    void update_title_from_scene   ();
    void drag_and_drop_target      (float min_x, float min_y, float max_x, float max_y);
    void cancel_brush_drag_and_drop();
    // glTF dragged from the Asset browser: while hovering (preview) draw the
    // asset's cached AABB as a wireframe box at the drop position; on drop
    // (delivery) instantiate the file as a prefab there. The position is the
    // hovered surface point (content hit or grid, camera-front fallback),
    // with the AABB bottom-center snapped onto it when bounds are known.
    void gltf_drag_preview_and_drop(Asset_file_gltf& gltf, bool preview, bool delivery);

    App_context&                                       m_app_context;
    std::weak_ptr<Viewport_scene_view>                 m_viewport_scene_view;
    std::weak_ptr<erhe::rendergraph::Rendergraph_node> m_rendergraph_output_node;
    int                                                m_window_slot{0};
    int                                                m_active_scene_tint_count{0};
    bool                                               m_was_focused{false};
    bool                                               m_brush_drag_and_drop_active{false};
    bool                                               m_viewport_child_window_focused{false};
    bool                                               m_viewport_child_window_hovered{false};

    bool                                               m_request_cursor_relative_hold{false};

    // Scene name currently shown in the window title; see update_title_from_scene().
    std::string                                        m_shown_scene_name;

#if !defined(NDEBUG)
    // Regression guard state for imgui_viewport(): detects the output
    // texture size drifting from the image quad size on stable frames.
    int                                                m_last_viewport_width    {0};
    int                                                m_last_viewport_height   {0};
    int                                                m_reported_mismatch_width {0};
    int                                                m_reported_mismatch_height{0};
#endif
};

}
