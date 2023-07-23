#pragma once

#include "time.hpp"
#include "tools/tool.hpp"
#include "scene/frame_controller.hpp"

#include "erhe/commands/command.hpp"
#include "erhe/imgui/imgui_window.hpp"
#include "erhe/toolkit/window_event_handler.hpp" // keycode

namespace erhe::commands {
    class Commands;
}
namespace erhe::imgui {
    class Imgui_windows;
}
namespace erhe::scene {
    class Camera;
}

namespace editor
{

class Editor_message_bus;
class Fly_camera_tool;
class Tools;
class Viewport_windows;

class Fly_camera_turn_command
    : public erhe::commands::Command
{
public:
    Fly_camera_turn_command(
        erhe::commands::Commands& commands,
        Editor_context&           context
    );
    void try_ready          () override;
    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;

private:
    Editor_context& m_context;
};

class Fly_camera_zoom_command
    : public erhe::commands::Command
{
public:
    Fly_camera_zoom_command(
        erhe::commands::Commands& commands,
        Editor_context&           context
    );
    void try_ready          () override;
    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;

private:
    Editor_context& m_context;
};

class Fly_camera_move_command
    : public erhe::commands::Command
{
public:
    Fly_camera_move_command(
        erhe::commands::Commands&                  commands,
        Editor_context&                            context,
        Variable                                   variable,
        erhe::toolkit::Simulation_variable_control control,
        bool                                       active
    );

    auto try_call() -> bool override;

private:
    Editor_context&                            m_context;
    Variable                                   m_variable;
    erhe::toolkit::Simulation_variable_control m_control;
    bool                                       m_active;
};

class Fly_camera_tool
    : public Update_fixed_step
    , public Update_once_per_frame
    , public erhe::imgui::Imgui_window
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
        float sensitivity       {1.0f};
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

    void update_fixed_step    (const Time_context& time_context) override;
    void update_once_per_frame(const Time_context& time_context) override;

    [[nodiscard]] auto get_camera() const -> erhe::scene::Camera*;
    void set_camera (erhe::scene::Camera* camera);
    void translation(int tx, int ty, int tz);
    void rotation   (int rx, int ry, int rz);

    // Commands
    void on_hover_viewport_change();
    auto try_ready() -> bool;
    auto try_move(
        Variable                                   variable,
        erhe::toolkit::Simulation_variable_control item,
        bool                                       active
    ) -> bool;
    auto turn_relative(float dx, float dy) -> bool;
    auto zoom         (float delta) -> bool;

private:
    void update_camera();

    Fly_camera_turn_command           m_turn_command;
    Fly_camera_zoom_command           m_zoom_command;
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
    std::shared_ptr<Frame_controller> m_camera_controller;
    float                             m_rotate_scale_x{1.0f};
    float                             m_rotate_scale_y{1.0f};

    std::mutex                         m_mutex;
    float                              m_sensitivity        {1.0f};
    bool                               m_use_viewport_camera{true};

#if defined(ERHE_ENABLE_3D_CONNEXION_SPACE_MOUSE)
    Fly_camera_space_mouse_listener       m_space_mouse_listener;
    erhe::toolkit::Space_mouse_controller m_space_mouse_controller;
#endif
};

} // namespace editor
