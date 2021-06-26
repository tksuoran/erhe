#pragma once

#include "scene/frame_controller.hpp"
#include "tools/tool.hpp"
#include "erhe/toolkit/space_mouse.hpp"

namespace erhe::scene
{

class Camera;

}

namespace editor
{

class Fly_camera_tool;
class Scene_manager;
class Scene_root;

class Fly_camera_space_mouse_listener
    : public erhe::toolkit::Space_mouse_listener
{
public:
    Fly_camera_space_mouse_listener (Fly_camera_tool& fly_camera_tool);
    ~Fly_camera_space_mouse_listener();

    bool is_active     ()                                         override;
    void set_active    (bool value)                               override;
    void on_translation(const int tx, const int ty, const int tz) override;
    void on_rotation   (const int rx, const int ry, const int rz) override;
    void on_button     (const int id)                             override;

private:
    bool             m_is_active{false};
    Fly_camera_tool& m_fly_camera_tool;
};

class Fly_camera_tool
    : public erhe::components::Component
    , public erhe::components::IUpdate_fixed_step
    , public erhe::components::IUpdate_once_per_frame
    , public Tool
    , public Window
{
public:
    static constexpr const char* c_name = "Fly_camera_tool";

    Fly_camera_tool ();
    ~Fly_camera_tool() override;

    // Implements Component
    void connect             () override;
    void initialize_component() override;

    // Implements Tool
    auto update      (Pointer_context& pointer_context) -> bool override;
    auto state       () const -> State                          override;
    void cancel_ready()                                         override;
    auto description () -> const char*                          override;

    // Implements Window
    void window(Pointer_context& pointer_context) override;

    // Implements IUpdate_fixed_step
    void update_fixed_step    (const erhe::components::Time_context& time_context) override;

    // Implements IUpdate_once_per_frame
    void update_once_per_frame(const erhe::components::Time_context& time_context) override;

    void x_pos_control(bool pressed);
    void x_neg_control(bool pressed);
    void y_pos_control(bool pressed);
    void y_neg_control(bool pressed);
    void z_neg_control(bool pressed);
    void z_pos_control(bool pressed);

    void translation(int tx, int ty, int tz);
    void rotation(int rx, int ry, int rz);

private:
    auto begin(Pointer_context& pointer_context) -> bool;
    auto end  (Pointer_context& pointer_context) -> bool;

    std::mutex                            m_mutex;
    std::shared_ptr<Scene_root>           m_scene_root;
    Frame_controller                      m_camera_controller;
    Fly_camera_space_mouse_listener       m_space_mouse_listener;
    erhe::toolkit::Space_mouse_controller m_space_mouse_controller;
    State                                 m_state      {State::Passive};
    double                                m_mouse_x    {0.0};
    double                                m_mouse_y    {0.0};
    float                                 m_sensitivity{1.0f};
};

} // namespace editor
