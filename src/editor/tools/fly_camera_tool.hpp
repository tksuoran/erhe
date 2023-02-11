#pragma once

#include "tools/tool.hpp"
#include "scene/frame_controller.hpp"

#include "erhe/application/commands/command.hpp"
#include "erhe/application/application_view.hpp"
#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/toolkit/view.hpp" // keycode

#define ERHE_ENABLE_3D_CONNEXION_SPACE_MOUSE 1

#if defined(ERHE_ENABLE_3D_CONNEXION_SPACE_MOUSE)
#   include "erhe/toolkit/space_mouse.hpp"
#endif

namespace erhe::scene
{
    class Camera;
}

namespace editor
{

#if defined(ERHE_ENABLE_3D_CONNEXION_SPACE_MOUSE)
class Fly_camera_space_mouse_listener
    : public erhe::toolkit::Space_mouse_listener
{
public:
    Fly_camera_space_mouse_listener();
    ~Fly_camera_space_mouse_listener() noexcept;

    auto is_active     () -> bool                                 override;
    void set_active    (const bool value)                         override;
    void on_translation(const int tx, const int ty, const int tz) override;
    void on_rotation   (const int rx, const int ry, const int rz) override;
    void on_button     (const int id)                             override;

private:
    bool m_is_active{false};
};
#endif

class Fly_camera_turn_command
    : public erhe::application::Command
{
public:
    Fly_camera_turn_command();
    void try_ready          () override;
    auto try_call_with_input(erhe::application::Input_arguments& input) -> bool override;
};

class Fly_camera_move_command
    : public erhe::application::Command
{
public:
    Fly_camera_move_command(
        Control                            control,
        erhe::application::Controller_item item,
        bool                               active
    );

    auto try_call() -> bool override;

private:
    Control                            m_control;
    erhe::application::Controller_item m_item;
    bool                               m_active;
};

class Fly_camera_tool
    : public erhe::components::Component
    , public erhe::components::IUpdate_fixed_step
    , public erhe::components::IUpdate_once_per_frame
    , public Tool
    , public erhe::application::Imgui_window
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

    static constexpr int              c_priority {5};
    static constexpr std::string_view c_type_name{"Fly_camera_tool"};
    static constexpr std::string_view c_title    {"Fly Camera"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Fly_camera_tool ();
    ~Fly_camera_tool() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void deinitialize_component     () override;

    // Implements Window
    void imgui() override;

    // Implements IUpdate_fixed_step
    void update_fixed_step(const erhe::components::Time_context& time_context) override;

    // Implements IUpdate_once_per_frame
    void update_once_per_frame(const erhe::components::Time_context& time_context) override;

    // Public API

    [[nodiscard]] auto get_camera() const -> erhe::scene::Camera*;
    void set_camera (erhe::scene::Camera* camera);
    void translation(int tx, int ty, int tz);
    void rotation   (int rx, int ry, int rz);

    // Commands
    auto try_ready() -> bool;
    auto try_move(
        Control                            control,
        erhe::application::Controller_item item,
        bool                               active
    ) -> bool;
    auto turn_relative (float dx, float dy) -> bool;

private:
    void update_camera();

    Fly_camera_turn_command           m_turn_command;
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

extern Fly_camera_tool* g_fly_camera_tool;

} // namespace editor
