#pragma once

#include "tools/tool.hpp"
#include "scene/node_raytrace.hpp"

#include "erhe_physics/irigid_body.hpp"
#include "erhe_commands/command.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::physics {
    class IConstraint;
    class IRigid_body;
    class IWorld;
}
namespace erhe::raytrace {
    class IScene;
}
namespace erhe::scene {
    class Mesh;
}

namespace editor {

class Editor_message;
class Editor_message_bus;
class Headset_view;
class Icon_set;
class Node_physics;
class Physics_tool;
class Scene_root;

enum class Physics_tool_mode : int {
    Drag = 0,
    Push = 1,
    Pull = 2
};

class Physics_tool_drag_command : public erhe::commands::Command
{
public:
    Physics_tool_drag_command(erhe::commands::Commands& commands, Editor_context& context);
    void try_ready  () override;
    auto try_call   () -> bool override;
    void on_inactive() override;

private:
    Editor_context& m_context;
};

class Physics_tool
    : public Tool
{
public:
    static constexpr int c_priority{2};

    Physics_tool(
        erhe::commands::Commands& commands,
        Editor_context&           editor_context,
        Editor_message_bus&       editor_message_bus,
        Headset_view&             headset_view,
        Icon_set&                 icon_set,
        Tools&                    tools
    );
    ~Physics_tool() noexcept;

    // Implements Tool
    void handle_priority_update(int old_priority, int new_priority) override;
    void tool_render    (const Render_context& context) override;
    void tool_properties()                              override;

    // Public API
    auto acquire_target() -> bool;
    void release_target();

    [[nodiscard]] auto get_mode() const -> Physics_tool_mode;
    void set_mode(Physics_tool_mode value);

    auto on_drag_ready() -> bool;
    auto on_drag      () -> bool;

private:
    void on_message(Editor_message& message);
    void tool_hover(Scene_view* scene_view);

    void move_drag_point_instant  (glm::vec3 position);
    void move_drag_point_kinematic(glm::vec3 position);

    [[nodiscard]] auto get_scene_root    () const -> Scene_root*;

    // Commands
    Physics_tool_drag_command                 m_drag_command;
#if defined(ERHE_XR_LIBRARY_OPENXR)
    erhe::commands::Redirect_command          m_drag_redirect_update_command;
    erhe::commands::Drag_enable_float_command m_drag_enable_command;
#endif

    Physics_tool_mode                           m_mode       {Physics_tool_mode::Drag};
    erhe::physics::Motion_mode                  m_motion_mode{erhe::physics::Motion_mode::e_kinematic_physical};

    std::shared_ptr<erhe::scene::Mesh>          m_hover_mesh;
    std::shared_ptr<erhe::scene::Mesh>          m_target_mesh;
    std::shared_ptr<erhe::scene::Mesh>          m_last_target_mesh;
    std::shared_ptr<Node_physics>               m_target_node_physics;
    float                                       m_target_distance                 {1.0f};
    glm::vec3                                   m_grab_position_in_node           {0.0f, 0.0f, 0.0f}; // Where object drag started in local node space
    glm::vec3                                   m_grab_position_in_collision_shape{0.0f, 0.0f, 0.0f}; // Where object drag started in local node space
    glm::vec3                                   m_grab_position_world             {0.0f, 0.0f, 0.0f}; // Where object drag started in world space
    glm::vec3                                   m_goal_position_in_world          {0.0f, 0.0f, 0.0f}; // Goal position for drag point in world space

    erhe::physics::IWorld*                      m_physics_world{nullptr};
    std::unique_ptr<erhe::physics::IConstraint> m_target_constraint;
    std::shared_ptr<erhe::physics::IRigid_body> m_constraint_world_point_rigid_body;

    float m_force_distance          {1.0f};

    float m_frequency               {0.00f};
    float m_damping                 {1.00f};
    float m_tau                     {0.001f};
    float m_impulse_clamp           {1.00f};
    float m_depth                   {0.00f}; // TODO requires transform for mouse position 0.0 = surface, 1.0 = center of gravity
    float m_override_linear_damping {0.90f};
    float m_override_angular_damping{0.90f};
    float m_override_friction_value {0.01f};
    float m_override_gravity_value  {0.40f};
    bool  m_override_gravity_enable {true};
    bool  m_override_friction_enable{true};
    bool  m_override_damping_enable {true};
    bool  m_extra_damping_enable    {true};
    float m_extra_linear_damping    {0.2f};
    float m_extra_angular_damping   {0.2f};

    float m_original_linear_damping {0.00f};
    float m_original_angular_damping{0.00f};
    float m_original_friction       {0.00f};
    float m_original_gravity        {1.00f};

    glm::vec3 m_to_end_direction  {0.0f};
    glm::vec3 m_to_start_direction{0.0f};
    float     m_target_mesh_size  {0.0f};

    bool          m_show_drag_body{true};
    Ray_hit_style m_ray_hit_style{
        .ray_color     = glm::vec4{1.0f, 0.0f, 1.0f, 1.0f},
        .ray_thickness = 8.0f,
        .ray_length    = 1.0f,
        .hit_color     = glm::vec4{0.8f, 0.2f, 0.8f, 0.75f},
        .hit_thickness = 8.0f,
        .hit_size      = 0.5f
    };
};

} // namespace editor
