#pragma once

#include "time.hpp"
#include "tools/tool.hpp"
#include "scene/frame_controller.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_imgui/imgui_window.hpp"
#include "erhe_imgui/windows/graph.hpp"
#include "erhe_imgui/windows/graph_plotter.hpp"
#include "erhe_window/window_event_handler.hpp" // keycode

#include <random>

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

class Fly_camera_tool
    : public Update_once_per_frame
    , public erhe::imgui::Imgui_window
    , public Tool
{
public:
    class Config
    {
    public:
        bool  invert_x  {false};
        bool  invert_y  {false};
        float move_power{1000.0f};
        float turn_speed{   1.0f};
    };
    Config config;

    static constexpr int c_priority{5};

    Fly_camera_tool(
        erhe::commands::Commands&    commands,
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Editor_context&              editor_context,
        Editor_message_bus&          editor_message_bus,
        Time&                        time,
        Tools&                       tools
    );

    // Implements Window
    void imgui() override;

    // Implements Update_once_per_frame
    void update_once_per_frame(const Time_context& time_context) override;
    //void tick(std::chrono::steady_clock::time_point timestamp);

    void on_frame_begin();
    void on_frame_end();

    [[nodiscard]] auto get_camera() const -> erhe::scene::Camera*;
    void set_camera (erhe::scene::Camera* camera, erhe::scene::Node* node = nullptr);
    void translation(std::chrono::steady_clock::time_point timestamp, int tx, int ty, int tz);
    void rotation   (std::chrono::steady_clock::time_point timestamp, int rx, int ry, int rz);

    // Commands
    void on_hover_viewport_change();
    auto try_ready       () -> bool;
    auto try_move        (std::chrono::steady_clock::time_point timestamp, Variable variable, erhe::math::Input_axis_control item, bool active) -> bool;
    auto adjust          (std::chrono::steady_clock::time_point timestamp, Variable variable, float value) -> bool;
    auto turn_relative   (std::chrono::steady_clock::time_point timestamp, float dx, float dy) -> bool;
    auto try_start_tumble() -> bool;
    auto tumble_relative (std::chrono::steady_clock::time_point timestamp, float dx, float dy) -> bool;
    auto try_start_track () -> bool;
    auto track           () -> bool;
    auto zoom            (std::chrono::steady_clock::time_point timestamp, float delta) -> bool;

    void capture_pointer();
    void release_pointer();

    void record_sample(std::chrono::steady_clock::time_point timestamp);

private:
    void update_camera();
    void show_input_axis_ui(erhe::math::Input_axis& input_axis) const;

    Fly_camera_turn_command           m_turn_command;
    Fly_camera_tumble_command         m_tumble_command;
    Fly_camera_track_command          m_track_command;
    Fly_camera_zoom_command           m_zoom_command;
    Fly_camera_frame_command          m_frame_command;
    Fly_camera_move_command           m_move_up_active_command;
    Fly_camera_move_command           m_move_up_inactive_command;
    Fly_camera_move_command           m_move_down_active_command;
    Fly_camera_move_command           m_move_down_inactive_command;
    Fly_camera_move_command           m_move_left_active_command;
    Fly_camera_move_command           m_move_left_inactive_command;
    Fly_camera_move_command           m_move_right_active_command;
    Fly_camera_move_command           m_move_right_inactive_command;
    Fly_camera_move_command           m_move_forward_active_command;
    Fly_camera_move_command           m_move_forward_inactive_command;
    Fly_camera_move_command           m_move_backward_active_command;
    Fly_camera_move_command           m_move_backward_inactive_command;
    Fly_camera_variable_float_command m_translate_x_command;
    Fly_camera_variable_float_command m_translate_y_command;
    Fly_camera_variable_float_command m_translate_z_command;
    Fly_camera_variable_float_command m_rotate_x_command;
    Fly_camera_variable_float_command m_rotate_y_command;
    Fly_camera_variable_float_command m_rotate_z_command;
    std::shared_ptr<Frame_controller> m_camera_controller;
    float                             m_rotate_scale_x{1.0f};
    float                             m_rotate_scale_y{1.0f};
    std::optional<glm::vec3>          m_tumble_pivot;
    std::optional<glm::vec3>          m_track_plane_point;
    std::optional<glm::vec3>          m_track_plane_normal;

    std::mutex                        m_mutex;
    bool                              m_use_viewport_camera{true};
    erhe::scene::Camera*              m_camera{nullptr};
    erhe::scene::Node*                m_node{nullptr};

    class Event
    {
    public:
        float x;
        std::string text;
    };

    bool                                      m_recording{false};
    std::chrono::steady_clock::time_point     m_recording_start_time{};
    std::vector<Event>                        m_events;
    bool                                      m_log_frame_update_details{false};
    erhe::imgui::Graph<std::array<ImVec2, 2>> m_velocity_graph;
    erhe::imgui::Graph<std::array<ImVec2, 2>> m_distance_graph;
    erhe::imgui::Graph<std::array<ImVec2, 2>> m_distance_dt_graph;
    erhe::imgui::Graph<std::array<ImVec2, 2>> m_state_time_graph; // t0 and t1
    erhe::imgui::Graph<std::array<ImVec2, 2>> m_deltatime_graph;
    erhe::imgui::Graph_plotter                m_graph_plotter;

    Jitter m_jitter;
};

} // namespace editor
