#pragma once

#include "tools/tool.hpp"
#include "scene/node_raytrace.hpp"

#include "erhe/application/commands/command.hpp"
#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/physics/imotion_state.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <memory>

namespace erhe::raytrace
{
    class IScene;
}

namespace erhe::scene
{
    class Mesh;
}

namespace erhe::physics
{
    class IConstraint;
    class IRigid_body;
    class IWorld;
}

namespace editor
{

class Editor_message;
class Node_physics;
class Scene_root;

enum class Physics_tool_mode : int
{
    Drag = 0,
    Push = 1,
    Pull = 2
};

class Physics_tool_drag_command
    : public erhe::application::Command
{
public:
    explicit Physics_tool_drag_command();
    auto try_call   (erhe::application::Command_context& context) -> bool override;
    void try_ready  (erhe::application::Command_context& context) override;
    void on_inactive(erhe::application::Command_context& context) override;
};

class Physics_tool
    : public erhe::components::Component
    , public erhe::physics::IMotion_state
    , public Tool
{
public:
    static constexpr int              c_priority{2};
    static constexpr std::string_view c_type_name   {"Physics_tool"};
    static constexpr std::string_view c_title   {"Physics Tool"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Physics_tool ();
    ~Physics_tool() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void deinitialize_component     () override;

    // Implements Tool
    void handle_priority_update(int old_priority, int new_priority) override;
    void tool_render    (const Render_context& context) override;
    void tool_properties()                              override;

    // Implements erhe::physics::IMotion_state
    [[nodiscard]] auto get_world_from_rigidbody() const -> erhe::physics::Transform   override;
    [[nodiscard]] auto get_motion_mode         () const -> erhe::physics::Motion_mode override;
    void set_world_from_rigidbody(const glm::mat4&                 transform  ) override;
    void set_world_from_rigidbody(const erhe::physics::Transform   transform  ) override;
    void set_motion_mode         (const erhe::physics::Motion_mode motion_mode) override;

    // Public API
    auto acquire_target() -> bool;
    void release_target();
    void begin_point_to_point_constraint();

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
    [[nodiscard]] auto get_raytrace_scene() const -> erhe::raytrace::IScene*;
    [[nodiscard]] auto get_physics_world () const -> erhe::physics::IWorld*;

    // Commands
    Physics_tool_drag_command m_drag_command;

    Physics_tool_mode                           m_mode{Physics_tool_mode::Drag};
    erhe::physics::Motion_mode                  m_motion_mode{erhe::physics::Motion_mode::e_kinematic_physical};

    std::shared_ptr<erhe::scene::Mesh>          m_hover_mesh;
    std::shared_ptr<erhe::scene::Mesh>          m_target_mesh;
    std::shared_ptr<erhe::scene::Mesh>          m_last_target_mesh;
    std::shared_ptr<Node_physics>               m_target_node_physics;
    double                                      m_target_distance        {1.0};
    glm::dvec3                                  m_target_position_in_mesh{0.0, 0.0, 0.0};
    glm::dvec3                                  m_target_position_start  {0.0, 0.0, 0.0};
    glm::dvec3                                  m_target_position_end    {0.0, 0.0, 0.0};
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
    float m_override_friction       {0.01f};
    float m_override_gravity        {0.40f};

    float m_original_linear_damping {0.00f};
    float m_original_angular_damping{0.00f};
    float m_original_friction       {0.00f};
    float m_original_gravity        {1.00f};

    glm::dvec3 m_to_end_direction  {0.0};
    glm::dvec3 m_to_start_direction{0.0};
    double     m_target_mesh_size  {0.0};

    bool       m_show_drag_body{false};

    Ray_hit_style m_ray_hit_style
    {
        .ray_color     = glm::vec4{1.0f, 0.0f, 1.0f, 1.0f},
        .ray_thickness = 8.0f,
        .ray_length    = 1.0f,
        .hit_color     = glm::vec4{0.8f, 0.2f, 0.8f, 0.75f},
        .hit_thickness = 8.0f,
        .hit_size      = 0.5f
    };
};

extern Physics_tool* g_physics_tool;

} // namespace editor
