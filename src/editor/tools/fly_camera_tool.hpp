#pragma once

#include "tools/tool.hpp"
#include "scene/frame_controller.hpp"

#include "erhe/application/commands/command.hpp"
#include "erhe/application/view.hpp"
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

class Editor_scenes;
class Fly_camera_tool;
class Scene_root;
class Tools;
class Trs_tool;
class Viewport_window;
class Viewport_windows;

#if defined(ERHE_ENABLE_3D_CONNEXION_SPACE_MOUSE)
class Fly_camera_space_mouse_listener
    : public erhe::toolkit::Space_mouse_listener
{
public:
    explicit Fly_camera_space_mouse_listener(Fly_camera_tool& fly_camera_tool);
    ~Fly_camera_space_mouse_listener() noexcept;

    auto is_active     () -> bool                                 override;
    void set_active    (const bool value)                         override;
    void on_translation(const int tx, const int ty, const int tz) override;
    void on_rotation   (const int rx, const int ry, const int rz) override;
    void on_button     (const int id)                             override;

private:
    bool             m_is_active{false};
    Fly_camera_tool& m_fly_camera_tool;
};
#endif

class Fly_camera_turn_command
    : public erhe::application::Command
{
public:
    explicit Fly_camera_turn_command(Fly_camera_tool& fly_camera_tool)
        : Command          {"Fly_camera.turn_camera"}
        , m_fly_camera_tool{fly_camera_tool}
    {
    }

    auto try_call (erhe::application::Command_context& context) -> bool override;
    void try_ready(erhe::application::Command_context& context) override;

private:
    Fly_camera_tool& m_fly_camera_tool;
};

class Fly_camera_move_command
    : public erhe::application::Command
{
public:
    Fly_camera_move_command(
        Fly_camera_tool&                         fly_camera_tool,
        const Control                            control,
        const erhe::application::Controller_item item,
        const bool                               active
    )
        : Command          {"Fly_camera.move"}
        , m_fly_camera_tool{fly_camera_tool  }
        , m_control        {control          }
        , m_item           {item             }
        , m_active         {active           }
    {
    }

    auto try_call(erhe::application::Command_context& context) -> bool override;

private:
    Fly_camera_tool&                   m_fly_camera_tool;
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
    static constexpr int              c_priority{5};
    static constexpr std::string_view c_type_name   {"Fly_camera_tool"};
    static constexpr std::string_view c_title   {"Fly Camera"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Fly_camera_tool ();
    ~Fly_camera_tool() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements Tool
    [[nodiscard]] auto tool_priority() const -> int   override { return c_priority; }
    [[nodiscard]] auto description  () -> const char* override;

    // Implements Window
    void imgui() override;

    // Implements IUpdate_fixed_step
    void update_fixed_step(const erhe::components::Time_context& time_context) override;

    // Implements IUpdate_once_per_frame
    void update_once_per_frame(const erhe::components::Time_context& time_context) override;

    // Public API

    [[nodiscard]] auto get_camera() const -> erhe::scene::Camera*;
    void set_camera (erhe::scene::Camera* const camera);
    void translation(int tx, int ty, int tz);
    void rotation   (int rx, int ry, int rz);

    // Commands
    auto try_ready() -> bool;
    auto try_move(
        Control                            control,
        erhe::application::Controller_item item,
        bool                               active
    ) -> bool;
    auto turn_relative (double dx, double dy) -> bool;

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

    // Component dependencies
    std::shared_ptr<Tools>             m_editor_tools;
    std::shared_ptr<Editor_scenes>     m_editor_scenes;
    std::shared_ptr<Scene_root>        m_scene_root;
    std::shared_ptr<Trs_tool>          m_trs_tool;
    std::shared_ptr<Viewport_windows>  m_viewport_windows;

    std::mutex                         m_mutex;
    float                              m_sensitivity        {1.0f};
    bool                               m_use_viewport_camera{true};

#if defined(ERHE_ENABLE_3D_CONNEXION_SPACE_MOUSE)
    Fly_camera_space_mouse_listener       m_space_mouse_listener;
    erhe::toolkit::Space_mouse_controller m_space_mouse_controller;
#endif
};

} // namespace editor
