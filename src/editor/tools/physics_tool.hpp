#pragma once

#include "tools/tool.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <memory>

class btPoint2PointConstraint;

namespace erhe::scene {
    class Mesh;
}

namespace editor
{

class Node_physics;
class Scene_root;

class Physics_tool
    : public erhe::components::Component
    , public Tool
    , public Window
{
public:
    static constexpr const char* c_name = "Physics_tool";

    Physics_tool ();
    ~Physics_tool() override;

    // Implements Component
    void connect             () override;
    void initialize_component() override;

    // Implements Tool
    auto update      (Pointer_context& pointer_context) -> bool override;
    void render      (const Render_context& render_context)     override;
    auto state       () const -> State                          override;
    void cancel_ready()                                         override;
    auto description () -> const char*                          override;

    // Implements Window
    void window(Pointer_context& pointer_context) override;

private:
    State                                    m_state{State::Passive};
    std::shared_ptr<Scene_root>              m_scene_root;

    std::shared_ptr<erhe::scene::Mesh>       m_drag_mesh;
    std::shared_ptr<Node_physics>            m_drag_node_physics;
    float                                    m_drag_depth{0.5f};
    glm::vec3                                m_drag_position_in_mesh{0.0f, 0.0f, 0.0f};
    glm::vec3                                m_drag_position_start  {0.0f, 0.0f, 0.0f};
    glm::vec3                                m_drag_position_end    {0.0f, 0.0f, 0.0f};
    std::unique_ptr<btPoint2PointConstraint> m_drag_constraint;

    float m_tau                     { 0.001f};
    float m_damping                 { 1.00f};
    float m_impulse_clamp           { 1.00f};
    float m_linear_damping          { 0.99f};
    float m_angular_damping         { 0.99f};
    float m_original_linear_damping { 0.00f};
    float m_original_angular_damping{ 0.00f};
};

} // namespace editor
