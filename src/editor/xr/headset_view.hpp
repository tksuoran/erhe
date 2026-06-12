#pragma once

#if defined(ERHE_XR_LIBRARY_OPENXR)
#include "renderers/programs.hpp"
#include "scene/scene_view.hpp"
#include "xr/hand_tracker.hpp"
#include "xr/controller_visualization.hpp"
#include "xr/headset_view_resources.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_commands/commands.hpp"
#include "erhe_commands/input_arguments.hpp"
#include "erhe_imgui/imgui_window.hpp"
#include "erhe_math/input_axis.hpp"
#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_xr/headset.hpp"

#include <span>

namespace erhe::graphics {
    class Command_buffer;
    class Device;
    class Swapchain;
}
namespace erhe::imgui {
    class Imgui_windows;
}
namespace erhe::renderer {
    class Line_renderer_set;
    class Text_renderer;
}
namespace erhe::scene {
    class Camera;
    class Node;
}
namespace erhe::scene_renderer {
    class Forward_renderer;
    class Mesh_memory;
    class Shadow_renderer;
}
namespace erhe::window {
    class Context_window;
}

struct Viewport_config_data;

namespace editor {

class App_context;
class Xr_perf_metric_plot;
class App_message_bus;
class App_rendering;
class App_settings;
class Fly_camera_tool;
class Hud;
class Scene_builder;
class Scene_root;
class Time;
class Tools;

class Controller_input
{;
public:
    glm::vec3 position     {0.0f, 0.0f, 0.0f};
    glm::vec3 direction    {0.0f, 0.0f, 0.0f};
    float     trigger_value{0.0f};
};

class Headset_view_node : public erhe::rendergraph::Rendergraph_node
{
public:
    Headset_view_node(erhe::rendergraph::Rendergraph& rendergraph, Headset_view& headset_view);
    ~Headset_view_node() override;

    // Implements Rendergraph_node
    void execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer) override;

private:
    Headset_view& m_headset_view;
};

class Headset_camera_offset_move_command : public erhe::commands::Command
{
public:
    Headset_camera_offset_move_command(erhe::commands::Commands& commands, erhe::math::Input_axis& variable, char axis);

    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;

private:
    erhe::math::Input_axis& m_variable;
    char                    m_axis;
};

class Headset_view
    : public std::enable_shared_from_this<Headset_view>
    , public Scene_view
    , public Renderable
    , public erhe::imgui::Imgui_window
{
public:
    Headset_view(
        const Viewport_config_data&     viewport_config_data,
        erhe::commands::Commands&       commands,
        erhe::graphics::Device&         graphics_device,
        erhe::imgui::Imgui_renderer&    imgui_renderer,
        erhe::imgui::Imgui_windows&     imgui_windows,
        erhe::rendergraph::Rendergraph& rendergraph,
        erhe::window::Context_window&   context_window,
#if defined(ERHE_XR_LIBRARY_OPENXR)
        erhe::xr::Headset*              headset,
#endif
        App_context&                    context,
        App_rendering&                  app_rendering,
        App_settings&                   app_settings
    );
    // Defined out-of-line so the unique_ptr<Xr_perf_metric_plot> deleter
    // can see the full plot definition (instead of forcing every TU that
    // includes this header to also include xr_perf_metric_plot.hpp).
    ~Headset_view() override;

    void attach_to_scene(std::shared_ptr<Scene_root> scene_root, erhe::scene_renderer::Mesh_memory& mesh_memory);

    // Public API
    auto begin_frame   () -> bool;
    auto poll_events   () -> bool;
    auto update_actions() -> bool;
    auto render_headset(erhe::graphics::Command_buffer& command_buffer) -> bool;

    void end_frame                ();
    void request_renderdoc_capture();

    void update_fixed_step();

    // Implements Scene_view
    auto get_camera            () const -> std::shared_ptr<erhe::scene::Camera> override;
    auto get_perspective_scale () const -> float                                override;
    auto get_shadow_render_node() const -> Shadow_render_node*                  override;
    auto get_rendergraph_node  () -> erhe::rendergraph::Rendergraph_node*       override;

    // Implements Imgui_window
    void imgui() override;

    void add_finger_input(const Finger_point& finger_point);

    [[nodiscard]] auto finger_to_viewport_distance_threshold() const -> float;
    [[nodiscard]] auto get_headset                          () const -> erhe::xr::Headset*;
    [[nodiscard]] auto get_root_node                        () const -> std::shared_ptr<erhe::scene::Node>;
    [[nodiscard]] auto is_active                            () const -> bool;
    [[nodiscard]] auto get_camera_offset                    () const -> glm::vec3;

    void render(const Render_context&) override;

private:
    [[nodiscard]] auto get_headset_view_resources(erhe::xr::Render_view& render_view) -> std::shared_ptr<Headset_view_resources>;
    // Multiview path uses one Headset_view_resources per view slot. The
    // per-eye fallback path matches by color_texture, but on the
    // multiview path every Render_view has color_texture = nullptr (the
    // shared layered swapchain is exposed via Render_views_frame::shared_*),
    // so a color-texture lookup would collapse all eyes onto the same
    // resources object and both eyes would share one Camera/Node.
    [[nodiscard]] auto get_multiview_view_resources(erhe::xr::Render_view& render_view) -> std::shared_ptr<Headset_view_resources>;

    void setup_root_camera();
    void setup_pointer_pick_camera();
    void update_camera_node();
    void update_pointer_context_from_controller();

    // Combined stereo culling frustum for the shadow fit. OpenXR has no API
    // for a single frustum bounding both eyes, so the headset builds one from
    // the per-eye views: update_camera_node() locates the current frame's views
    // early (Headset::locate_views, valid after begin_frame() and before the
    // rendergraph runs), cache_combined_eye_frustum() reduces them to the union
    // of the eye fov sides, and update_root_camera_projection() applies that to
    // m_root_camera as a perspective_xr projection. This is the same frame the
    // shadow node fits, with no latency. If a locate fails, the last good
    // frustum is kept (m_combined_eye_fov_valid stays set).
    void cache_combined_eye_frustum(std::span<const erhe::xr::Render_view> views);
    void update_root_camera_projection();

    // VR skinned-mesh picking. The raytrace BVH used by
    // update_hover_with_raytrace() is rest-pose only, so it cannot pick the
    // GPU-skinned (posed) surface the user sees. update_id_render() drives
    // the Id_renderer once per frame from a dedicated single-view camera
    // placed at the controller aim pose (looking down the control ray);
    // update_hover_with_id_render() reads the resulting ID buffer at the
    // ray (the framebuffer centre) and merges the hit into the matching
    // hover slot. Mirrors the desktop hybrid picker in Viewport_scene_view.
    void update_id_render(erhe::graphics::Command_buffer& command_buffer);
    void update_hover_with_id_render();
    [[nodiscard]] auto get_pick_position_in_world(float depth) const -> std::optional<glm::vec3>;

    erhe::math::Input_axis                               m_translate_x;
    erhe::math::Input_axis                               m_translate_y;
    erhe::math::Input_axis                               m_translate_z;
    glm::vec3                                            m_camera_offset{0.0f, 0.0f, 0.0f};
    Headset_camera_offset_move_command                   m_offset_x_command;
    Headset_camera_offset_move_command                   m_offset_y_command;
    Headset_camera_offset_move_command                   m_offset_z_command;

    App_context&                                         m_app_context;
    erhe::window::Context_window&                        m_context_window;
    std::shared_ptr<Headset_view_node>                   m_rendergraph_node;
    std::shared_ptr<Shadow_render_node>                  m_shadow_render_node;
#if defined(ERHE_XR_LIBRARY_OPENXR)
    erhe::xr::Headset*                                   m_headset;
#endif
    std::unique_ptr<erhe::graphics::Render_pass>         m_mirror_mode_window_render_pass;
    std::shared_ptr<erhe::scene::Node>                   m_root_node; // scene root node
    std::shared_ptr<erhe::scene::Node>                   m_headset_node; // transform set by headset
    std::shared_ptr<erhe::scene::Camera>                 m_root_camera;
    // Combined eye frustum cache (see cache_combined_eye_frustum). Filled from
    // the located per-eye Render_views during render, applied to m_root_camera
    // at the next frame's update_camera_node(). Until the first eye views are
    // located, m_combined_eye_fov_valid is false and m_root_camera keeps the
    // setup_root_camera() default.
    erhe::scene::Projection::Fov_sides                   m_combined_eye_fov_sides{-0.6f, 0.6f, 0.6f, -0.6f};
    float                                                m_combined_eye_z_near{0.03f};
    float                                                m_combined_eye_z_far {200.0f};
    bool                                                 m_combined_eye_fov_valid{false};
    // Reused scratch for the early per-frame view locate (cleared + refilled by
    // Headset::locate_views; persists only to keep its capacity across frames).
    std::vector<erhe::xr::Render_view>                   m_located_eye_views;
    // Single-view camera positioned at the controller aim pose each frame,
    // used solely to drive the Id_renderer for skinned-mesh picking (see
    // update_id_render). Not flagged visible / show_in_ui -- it is never
    // composited, only used as a projection source.
    std::shared_ptr<erhe::scene::Camera>                 m_pointer_pick_camera;
    std::shared_ptr<erhe::scene::Node>                   m_pointer_pick_node;
    std::vector<std::shared_ptr<Headset_view_resources>> m_view_resources;
    // Indexed by Render_view::slot; entries are created lazily on first
    // use. Disjoint from m_view_resources so the per-eye fallback path
    // and the multiview path do not stomp on each other if a session
    // switches modes (e.g. during shader reloads).
    std::vector<std::shared_ptr<Headset_view_resources>> m_multiview_view_resources;
    std::unique_ptr<Controller_visualization>            m_controller_visualization;
    std::vector<Finger_point>                            m_finger_inputs;

    // TODO Shader_debug selection
    //Shader_stages_variant                                m_shader_stages_variant{Shader_stages_variant::not_set};    
    int                                                  m_selected_shader_debug{0};                                

    float                                                m_finger_to_viewport_distance_threshold{0.1f};

    bool                                                 m_poll_events_ok{false};
    bool                                                 m_update_actions_ok{false};
    bool                                                 m_request_renderdoc_capture{false};
    bool                                                 m_renderdoc_capture_started{false};
    // Latched at construction from Xr_session::is_multiview_enabled().
    // When true, render_headset() drives Xr_session::render_frame_multiview()
    // (single shared layered swapchain, single render pass with view_mask =
    // 0b11) instead of the per-eye render_frame() loop. The multiview
    // callback drives forward composition, content wide lines, and
    // debug lines through the multiview pipeline pair so all three
    // appear in both eyes from one render pass; mirror mode is still gated
    // to the per-eye path. The ID pick pass (update_id_render) is single-
    // view and runs once per frame regardless of this flag.
    bool                                                 m_use_multiview{false};
    erhe::xr::Frame_timing                               m_frame_timing{};
    uint64_t                                             m_frame_number{0};

    // Last XR_PERF_SETTINGS_LEVEL_*_EXT value (or -1 for unset) actually
    // sent to xrPerfSettingsSetPerformanceLevelEXT. Tracked here so the
    // editor only re-applies when the configured level changes.
    int                                                  m_applied_cpu_level{-2};
    int                                                  m_applied_gpu_level{-2};

    // One Plot per XR_FB_performance_metrics counter the runtime exposed.
    // Allocated lazily after the session is up; registered with the
    // Performance window. Unregistered + destroyed in ~Headset_view.
    std::vector<std::unique_ptr<Xr_perf_metric_plot>>    m_xr_perf_metric_plots;
    bool                                                 m_xr_perf_plots_registered{false};
};

}
#else
#   include "xr/null_headset_view.hpp"
#endif
