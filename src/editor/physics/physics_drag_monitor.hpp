#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::physics {
    class IWorld;
}
namespace erhe::scene {
    class Xformable; using Node = Xformable;
}

namespace editor {

class Node_joint;
class Physics_drag_constraint;
class Physics_drag_monitor;

// Body values a drag tool may override for the duration of a drag.
class Physics_drag_body_values
{
public:
    float linear_damping {0.0f};
    float angular_damping{0.0f};
    float friction       {0.0f};
    float gravity_factor {0.0f};
};

// What a drag tool tells the monitor at drag start.
class Physics_drag_monitor_info
{
public:
    Physics_drag_monitor*    monitor     {nullptr}; // the dragged body's scene monitor; nullptr = not monitored
    std::string_view         tool_name   {};
    const erhe::scene::Node* node        {nullptr};
    Physics_drag_body_values values_before_tool{};  // before the tool applied its overrides
    std::string              tool_details{};        // tool specific settings, one line
};

// Diagnostics of interactive physics drags (editor.physics_drag log
// category). A drag is announced by Physics_drag_constraint::attach() and
// withdrawn by detach(); while at least one drag is active, every rendered
// frame logs the drag and the error of every live Node_joint of the scene:
// the world-space distance between the two joint anchors and the angle of
// their relative rotation, together with how far each exceeds the joint's
// axis limits ("violation"). The anchors are computed from the NODE world
// transforms of the body nodes (rotation + translation), the same transforms
// Node_joint::try_create_constraint() built the joint frames from.
//
// With no active drag every entry point returns immediately. Steady-state
// frames of a drag allocate nothing but the log formatting.
class Physics_drag_monitor
{
public:
    explicit Physics_drag_monitor(const std::vector<std::shared_ptr<Node_joint>>& node_joints);
    ~Physics_drag_monitor() noexcept;
    Physics_drag_monitor(const Physics_drag_monitor&) = delete;
    auto operator=(const Physics_drag_monitor&) -> Physics_drag_monitor& = delete;

    void begin(const Physics_drag_constraint& drag, erhe::physics::IWorld& world, const Physics_drag_monitor_info& info);
    void end  (const Physics_drag_constraint& drag);

    // Scene_root::update_physics_simulation_fixed_step()
    void on_fixed_step(double dt);
    // Scene_root::after_physics_simulation_steps(), once per rendered frame
    // after the body poses were written to the nodes.
    void after_physics_simulation_steps();

private:
    class Joint_measurement
    {
    public:
        const Node_joint* joint            {nullptr};
        float             separation_mm    {0.0f};
        float             angle_deg        {0.0f};
        float             violation_mm     {0.0f};
        float             violation_deg    {0.0f};
        glm::vec3         offset_in_a_mm   {0.0f}; // anchor B - anchor A, in anchor A axes
        glm::vec3         rotation_in_a_deg{0.0f}; // rotation vector of A -> B, in anchor A axes
        float             score            {0.0f}; // max(violation_mm, violation_deg)
    };

    class Joint_max
    {
    public:
        const Node_joint* joint        {nullptr};
        std::string       name;
        float             separation_mm{0.0f};
        float             angle_deg    {0.0f};
        float             violation_mm {0.0f};
        float             violation_deg{0.0f};
    };

    class Drag_record
    {
    public:
        const Physics_drag_constraint* drag{nullptr};
        std::string                    tool_name;
        std::string                    node_name;
        uint64_t                       frame_count     {0};
        uint64_t                       fixed_step_count{0};
        float                          max_spring_mm   {0.0f};
        std::vector<Joint_max>         joint_max;
        Joint_measurement              last_worst{};
        bool                           has_last_worst{false};
    };

    void measure_joints();
    void log_joint_dump(const Node_joint& joint) const;

    const std::vector<std::shared_ptr<Node_joint>>& m_node_joints;
    std::vector<Drag_record>                        m_drags;
    std::vector<Joint_measurement>                  m_measurements;
    uint64_t                                        m_frame_fixed_steps{0};
    double                                          m_last_fixed_step_dt{0.0};
};

}
