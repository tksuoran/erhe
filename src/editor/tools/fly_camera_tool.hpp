#pragma once

#include "time.hpp"
#include "tools/tool.hpp"
#include "scene/frame_controller.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_imgui/imgui_window.hpp"
#include "erhe_imgui/windows/graph.hpp"
#include "erhe_imgui/windows/graph_plotter.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_window/window_event_handler.hpp" // keycode

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <deque>
#include <random>
#include <vector>

namespace erhe::commands {
    class Commands;
}
namespace erhe::imgui {
    class Imgui_windows;
}
namespace erhe::scene {
    class Camera;
}

namespace editor {

class Editor_message_bus;
class Fly_camera_tool;
class Tools;
class Scene_views;

class Jitter
{
public:
    Jitter();

    void imgui();
    void sleep();

private:
    bool                               m_enabled{false};
    int                                m_min{0};
    int                                m_max{0};
    std::random_device                 m_random_device;
    std::default_random_engine         m_random_engine;
    std::uniform_int_distribution<int> m_distribution;
};

class Fly_camera_turn_command : public erhe::commands::Command
{
public:
    Fly_camera_turn_command(erhe::commands::Commands& commands, Editor_context& context);
    void try_ready          () override;
    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;
    void on_inactive        () override;

private:
    Editor_context& m_context;
};

class Fly_camera_tumble_command : public erhe::commands::Command
{
public:
    Fly_camera_tumble_command(erhe::commands::Commands& commands, Editor_context& context);
    void try_ready          () override;
    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;
    void on_inactive        () override;

private:
    Editor_context& m_context;
};

class Fly_camera_track_command : public erhe::commands::Command
{
public:
    Fly_camera_track_command(erhe::commands::Commands& commands, Editor_context& context);
    void try_ready() override;
    auto try_call () -> bool override;

private:
    Editor_context& m_context;
};

class Fly_camera_zoom_command : public erhe::commands::Command
{
public:
    Fly_camera_zoom_command(erhe::commands::Commands& commands, Editor_context& context);
    void try_ready          () override;
    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;

private:
    Editor_context& m_context;
};

class Fly_camera_frame_command : public erhe::commands::Command
{
public:
    Fly_camera_frame_command(erhe::commands::Commands& commands, Editor_context& context);
    auto try_call() -> bool override;

private:
    Editor_context& m_context;
};

class Fly_camera_move_command : public erhe::commands::Command
{
public:
    Fly_camera_move_command(
        erhe::commands::Commands&      commands,
        Editor_context&                context,
        Variable                       variable,
        erhe::math::Input_axis_control control,
        bool                           active
    );

    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;

private:
    Editor_context&                m_context;
    Variable                       m_variable;
    erhe::math::Input_axis_control m_control;
    bool                           m_active;
};

class Fly_camera_variable_float_command : public erhe::commands::Command
{
public:
    Fly_camera_variable_float_command(
        erhe::commands::Commands& commands,
        Editor_context&           context,
        Variable                  variable,
        float                     scale
    );

    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;

private:
    Editor_context& m_context;
    Variable        m_variable;
    float           m_scale;
};

class Fly_camera_active_axis_float_command : public erhe::commands::Command
{
public:
    Fly_camera_active_axis_float_command(
        erhe::commands::Commands& commands,
        Editor_context&           context,
        Variable                  variable,
        float                     scale
    );

    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;

private:
    Editor_context& m_context;
    Variable        m_variable;
    float           m_scale;
};

class Fly_camera_serialization_command : public erhe::commands::Command
{
public:
    Fly_camera_serialization_command(erhe::commands::Commands& commands, Editor_context& context, bool store);

    auto try_call() -> bool override;

private:
    Editor_context& m_context;
    bool            m_store;
};

class Fly_camera_tool
    : public erhe::imgui::Imgui_window
    , public Tool
{
public:
    class Config
    {
    public:
        bool  invert_x          {false};
        bool  invert_y          {false};
        float velocity_damp     {0.92f};
        float velocity_max_delta{0.004f};
    };
    Config config;

    static constexpr int c_priority{5};

    Fly_camera_tool(
        erhe::commands::Commands&    commands,
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Editor_context&              editor_context,
        Editor_message_bus&          editor_message_bus,
        Tools&                       tools
    );

    // Implements Window
    void imgui() override;

    void on_frame_begin       ();
    void on_frame_end         ();
    void update_fixed_step    (const Time_context& time_context);

    [[nodiscard]] auto get_camera() const -> erhe::scene::Camera*;
    void set_camera (erhe::scene::Camera* camera, erhe::scene::Node* node = nullptr);
    void translation(int64_t timestamp_ns, int tx, int ty, int tz);
    void rotation   (int64_t timestamp_ns, int rx, int ry, int rz);

    // API for commands
    void on_hover_viewport_change();
    auto try_ready               () -> bool;
    auto try_move                (int64_t timestamp_ns, Variable variable, erhe::math::Input_axis_control item, bool active) -> bool;
    auto adjust                  (int64_t timestamp_ns, Variable variable, float value) -> bool;
    auto set_active_control_value(int64_t timestamp_ns, Variable variable, float value) -> bool;
    auto turn_relative           (int64_t timestamp_ns, float dx, float dy) -> bool;
    auto try_start_tumble        () -> bool;
    auto tumble_relative         (int64_t timestamp_ns, float dx, float dy) -> bool;
    auto try_start_track         () -> bool;
    auto track                   () -> bool;
    auto zoom                    (int64_t timestamp_ns, float delta) -> bool;
    void serialize_transform     (bool store);

    void synthesize_input();

    void set_cursor_relative_mode(bool relative_mode_enabled);

    void record_translation_sample(int64_t timestamp_ns);
    void record_heading_sample(int64_t timestamp_ns);

private:
    void update_camera();
    void show_input_axis_ui(const char* label, erhe::math::Input_axis& input_axis) const;

    Fly_camera_turn_command               m_turn_command;
    Fly_camera_tumble_command             m_tumble_command;
    Fly_camera_track_command              m_track_command;
    Fly_camera_zoom_command               m_zoom_command;
    Fly_camera_frame_command              m_frame_command;
    Fly_camera_move_command               m_move_up_active_command;
    Fly_camera_move_command               m_move_up_inactive_command;
    Fly_camera_move_command               m_move_down_active_command;
    Fly_camera_move_command               m_move_down_inactive_command;
    Fly_camera_move_command               m_move_left_active_command;
    Fly_camera_move_command               m_move_left_inactive_command;
    Fly_camera_move_command               m_move_right_active_command;
    Fly_camera_move_command               m_move_right_inactive_command;
    Fly_camera_move_command               m_move_forward_active_command;
    Fly_camera_move_command               m_move_forward_inactive_command;
    Fly_camera_move_command               m_move_backward_active_command;
    Fly_camera_move_command               m_move_backward_inactive_command;
#if defined(ERHE_WINDOW_LIBRARY_GLFW)
    Fly_camera_variable_float_command     m_translate_x_command;
    Fly_camera_variable_float_command     m_translate_y_command;
    Fly_camera_variable_float_command     m_translate_z_command;
    Fly_camera_variable_float_command     m_rotate_x_command;
    Fly_camera_variable_float_command     m_rotate_y_command;
    Fly_camera_variable_float_command     m_rotate_z_command;
#endif
#if defined(ERHE_WINDOW_LIBRARY_SDL)
    Fly_camera_active_axis_float_command  m_active_translate_x_command;
    Fly_camera_active_axis_float_command  m_active_translate_y_command;
    Fly_camera_active_axis_float_command  m_active_translate_z_command;
    Fly_camera_active_axis_float_command  m_active_rotate_x_command;
    Fly_camera_active_axis_float_command  m_active_rotate_y_command;
    Fly_camera_active_axis_float_command  m_active_rotate_z_command;
#endif
    Fly_camera_serialization_command      m_serialize_transform_command;
    Fly_camera_serialization_command      m_deserialize_transform_command;
    std::shared_ptr<Frame_controller>     m_camera_controller;
    float                                 m_rotate_scale_x{4.0f};
    float                                 m_rotate_scale_y{4.0f};
    std::optional<glm::vec3>              m_tumble_pivot;
    std::optional<glm::vec3>              m_track_plane_point;
    std::optional<glm::vec3>              m_track_plane_normal;

    ERHE_PROFILE_MUTEX(std::mutex,    m_mutex);
    float                             m_sensitivity        {1.0f};
    bool                              m_use_viewport_camera{true};
    erhe::scene::Camera*              m_camera{nullptr};
    erhe::scene::Node*                m_node{nullptr};

    class Event
    {
    public:
        float x;
        std::string text;
    };

    std::size_t                m_max_samples{8000};
    std::size_t                m_sample_count{0};

    bool                       m_recording{false};
    int64_t                    m_recording_start_time_ns{};
    std::vector<Event>         m_events;
    erhe::imgui::Graph<ImVec2> m_tx_graph;
    erhe::imgui::Graph<ImVec2> m_ty_graph;
    erhe::imgui::Graph<ImVec2> m_tz_graph;
    erhe::imgui::Graph<ImVec2> m_px_graph;
    erhe::imgui::Graph<ImVec2> m_py_graph;
    erhe::imgui::Graph<ImVec2> m_pz_graph;
    erhe::imgui::Graph<ImVec2> m_heading_graph;
    erhe::imgui::Graph_plotter m_graph_plotter;

    Jitter m_jitter;
    float  m_synth_distance{1000.0f};
    std::deque<erhe::window::Input_event> m_synthetic_input_events;
    glm::vec3                             m_before_position   {0.0f, 0.0f, 0.0f};
    glm::vec3                             m_after_position    {0.0f, 0.0f, 0.0f};
    glm::quat                             m_before_orientation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::quat                             m_after_orientation {1.0f, 0.0f, 0.0f, 0.0f};
};

} // namespace editor
