#pragma once

#include "animation/animation_edit.hpp"

#include "erhe_imgui/imgui_window.hpp"

#include <imgui/imgui.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace erhe::imgui { class Imgui_windows; }
namespace erhe::scene { class Animation; }

namespace editor {

class App_context;

// Animation timeline / curve graph editor (issue #243), in the spirit of
// Blender's Graph Editor. Shows the keyframe curves of a glTF animation as
// editable 2D curves over time:
//   - animation selector + transport controls (play / pause / loop / speed)
//   - channel list with per-component curve visibility
//   - curve canvas: pan / zoom, adaptive grid, time ruler with scrubbing
//     playhead, keyframe selection (click / box select), keyframe dragging
//     (time and value), keyframe insert (Ctrl+click near a curve) and delete
//     (Delete / X), all undoable through the operation stack.
//
// Playback state lives in Animation_player (updated once per frame from
// Editor::tick()); this window is the UI in front of it.
class Animation_window : public erhe::imgui::Imgui_window
{
public:
    Animation_window(
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 app_context
    );

    // Implements Imgui_window
    void imgui() override;

    // Points the editor (and Animation_player) at an animation. Used by the
    // animation combo, the Properties window and the MCP animation tools.
    void set_animation(const std::shared_ptr<erhe::scene::Animation>& animation);
    [[nodiscard]] auto get_animation() const -> const std::shared_ptr<erhe::scene::Animation>&;

    // Frames all visible curves in the view on the next draw.
    void frame_all();

    // Curve visibility (used by the UI and by MCP for deterministic
    // screenshots): one bit per component, bit index == component index.
    void set_channel_visibility(std::size_t channel_index, uint32_t component_mask);

private:
    // One selectable / draggable point on a curve: a (channel, component)
    // curve's key. The same underlying sampler key appears once per visible
    // component.
    class Curve_point
    {
    public:
        std::size_t channel_index{0};
        std::size_t component    {0};
        std::size_t key_index    {0};

        auto operator==(const Curve_point&) const -> bool = default;
    };

    enum class Drag_mode : int {
        none = 0,
        pan,
        scrub,
        box_select,
        move_keys
    };

    void animation_combo    ();
    void transport_toolbar  ();
    void channel_list_pane  ();
    void channel_row        (std::size_t channel_index);
    void curve_canvas       ();

    [[nodiscard]] auto channel_label        (std::size_t channel_index) const -> std::string;
    [[nodiscard]] auto channel_passes_filter(std::size_t channel_index) const -> bool;

    void ensure_visibility_size ();
    void prune_stale_selection  ();
    void compute_frame_all_view (float canvas_height_px);
    void apply_key_drag         (float time_delta, float value_delta);
    void finish_key_drag        (bool moved);
    void delete_selected_keys   ();
    void insert_key_at          (float time, ImVec2 mouse_px);

    [[nodiscard]] auto is_point_selected   (const Curve_point& point) const -> bool;
    [[nodiscard]] auto is_component_visible(std::size_t channel_index, std::size_t component) const -> bool;
    [[nodiscard]] auto get_component_color (std::size_t component) const -> ImU32;
    [[nodiscard]] auto find_curve_near(
        ImVec2       mouse_px,
        float        max_distance_px,
        std::size_t& out_channel_index,
        std::size_t& out_component
    ) const -> bool;

    // View mapping (set up at the start of curve_canvas() each frame)
    [[nodiscard]] auto time_to_x (float time)  const -> float;
    [[nodiscard]] auto value_to_y(float value) const -> float;
    [[nodiscard]] auto x_to_time (float x)     const -> float;
    [[nodiscard]] auto y_to_value(float y)     const -> float;

    App_context& m_context;

    std::shared_ptr<erhe::scene::Animation> m_animation;

    // Per-channel component visibility bitmask (bit i == component i).
    std::vector<uint32_t> m_channel_visibility;

    // Name filter for the "Channels" list; All / None / Animated apply only
    // to channels passing it.
    ImGuiTextFilter m_channel_filter;

    // View rectangle in curve space.
    float m_view_time_min  {0.0f};
    float m_view_time_max  {1.0f};
    float m_view_value_min {-1.0f};
    float m_view_value_max {1.0f};
    bool  m_frame_all_queued{true};

    // Canvas pixel rectangle of the current frame.
    ImVec2 m_canvas_min{0.0f, 0.0f};
    ImVec2 m_canvas_max{0.0f, 0.0f};
    float  m_ruler_height{22.0f};

    // Selection and drag state
    std::vector<Curve_point> m_selection;
    Drag_mode                m_drag_mode{Drag_mode::none};
    ImVec2                   m_drag_start_px{0.0f, 0.0f};
    bool                     m_drag_moved{false};
    Curve_point              m_pressed_point{};
    bool                     m_pressed_point_was_selected{false};
    int                      m_hovered_point{-1}; // index into m_visible_points, -1 = none

    // Initial (time, value) of each selected point at drag start, parallel
    // to m_selection.
    std::vector<ImVec2> m_drag_initial;

    // Undo snapshots of the samplers affected by the current gesture.
    std::vector<Animation_sampler_state> m_edit_before;

    // Scratch: all visible curve points of the current frame and their pixel
    // positions (rebuilt each frame, capacity kept).
    std::vector<Curve_point> m_visible_points;
    std::vector<ImVec2>      m_visible_point_px;

    float m_channel_pane_width{260.0f};
};

}
