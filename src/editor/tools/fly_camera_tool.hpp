#pragma once

#include "scene/frame_controller.hpp"
#include "tools/tool.hpp"

namespace editor
{

class Scene_manager;

class Fly_camera_tool
    : public erhe::components::Component
    , public Tool
    , public Window
{
public:
    static constexpr const char* c_name = "Fly_camera_tool";
    Fly_camera_tool() : erhe::components::Component{c_name} {}
    virtual ~Fly_camera_tool() = default;

    // Implements Component
    void connect() override;
    void initialize_component() override;
    auto description() -> const char* override { return c_name; }

    // Tool
    auto update(Pointer_context& pointer_context) -> bool override;
    auto state() const -> State override;
    void cancel_ready() override;

    // Window
    void window(Pointer_context& pointer_context) override;

    auto state_update(Pointer_context& pointer_context) -> bool;

    void x_pos_control(bool pressed);
    void x_neg_control(bool pressed);
    void y_pos_control(bool pressed);
    void y_neg_control(bool pressed);
    void z_neg_control(bool pressed);
    void z_pos_control(bool pressed);

    void update_fixed_step();

    void update_once_per_frame();

private:
    auto begin(Pointer_context& pointer_context) -> bool;

    auto end(Pointer_context& pointer_context) -> bool;

    std::shared_ptr<Scene_manager> m_scene_manager;
    Frame_controller               m_camera_controller;
    State                          m_state      {State::passive};
    double                         m_mouse_x    {0.0};
    double                         m_mouse_y    {0.0};
    float                          m_sensitivity{1.0f};
};

} // namespace editor
