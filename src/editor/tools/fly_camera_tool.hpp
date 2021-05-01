#pragma once

#include "scene/frame_controller.hpp"
#include "tools/tool.hpp"

namespace sample
{

class Scene_manager;

class Fly_camera_tool
    : public Tool
{
public:
    explicit Fly_camera_tool(const std::shared_ptr<Scene_manager>& scene_manager);

    virtual ~Fly_camera_tool() = default;

    auto name() -> const char* override { return "Fly_camera_tool"; }

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

} // namespace sample
