#pragma once

#include "tools/tool.hpp"

#include "erhe/application/commands/command.hpp"
#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <memory>

namespace erhe::scene

{
    class Mesh;
}

namespace erhe::physics
{
    class IConstraint;
    class IWorld;
}

namespace erhe::application
{
    class Line_renderer_set;
}

namespace editor
{

class Editor_scenes;
class Fly_camera_tool;
class Node_physics;
class Physics_tool;
class Scene_root;
class Viewport_windows;

class Physics_tool_drag_command
    : public erhe::application::Command
{
public:
    explicit Physics_tool_drag_command(Physics_tool& physics_tool)
        : Command       {"Physics_tool.drag"}
        , m_physics_tool{physics_tool}
    {
    }

    auto try_call   (erhe::application::Command_context& context) -> bool override;
    void try_ready  (erhe::application::Command_context& context) override;
    void on_inactive(erhe::application::Command_context& context) override;

private:
    Physics_tool& m_physics_tool;
};

class Physics_tool_force_command
    : public erhe::application::Command
{
public:
    explicit Physics_tool_force_command(Physics_tool& physics_tool)
        : Command       {"Physics_tool.force"}
        , m_physics_tool{physics_tool}
    {
    }

    auto try_call   (erhe::application::Command_context& context) -> bool override;
    void try_ready  (erhe::application::Command_context& context) override;
    void on_inactive(erhe::application::Command_context& context) override;

private:
    Physics_tool& m_physics_tool;
};

class Physics_tool
    : public erhe::components::Component
    , public erhe::components::IUpdate_fixed_step
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
    void post_initialize            () override;

    // Implements Tool
    [[nodiscard]] auto tool_priority() const -> int     override { return c_priority; }
    [[nodiscard]] auto description  () -> const char*   override;
    void tool_render    (const Render_context& context) override;
    void tool_properties() override;

    // Implements IUpdate_fixed_step
    void update_fixed_step(const erhe::components::Time_context&) override;

    auto acquire_target() -> bool;
    void release_target();
    void begin_point_to_point_constraint();

    // Commands
    void set_active_command(const int command);

    // Drag
    auto on_drag_ready() -> bool;
    auto on_drag      () -> bool;

    // Force
    auto on_force_ready() -> bool;
    auto on_force      () -> bool;

private:
    [[nodiscard]] auto physics_world() const -> erhe::physics::IWorld*;

    // Commands
    Physics_tool_drag_command  m_drag_command;
    Physics_tool_force_command m_force_command;

    // Component dependencies
    std::shared_ptr<erhe::application::Line_renderer_set> m_line_renderer_set;
    std::shared_ptr<Editor_scenes>                        m_editor_scenes;
    std::shared_ptr<Fly_camera_tool>                      m_fly_camera;
    std::shared_ptr<Viewport_windows>                     m_viewport_windows;

    static const int c_command_drag {0};
    static const int c_command_force{1};

    int m_active_command{c_command_drag};

    std::shared_ptr<erhe::scene::Mesh>          m_target_mesh;
    std::shared_ptr<Node_physics>               m_target_node_physics;
    double                                      m_target_distance        {1.0};
    glm::dvec3                                  m_target_position_in_mesh{0.0, 0.0, 0.0};
    glm::dvec3                                  m_target_position_start  {0.0, 0.0, 0.0};
    glm::dvec3                                  m_target_position_end    {0.0, 0.0, 0.0};
    std::unique_ptr<erhe::physics::IConstraint> m_target_constraint;

    float m_force_distance          {1.000f};
    float m_tau                     {0.001f};
    float m_damping                 {1.00f};
    float m_impulse_clamp           {1.00f};
    float m_linear_damping          {0.99f};
    float m_angular_damping         {0.99f};
    float m_original_linear_damping {0.00f};
    float m_original_angular_damping{0.00f};

    glm::dvec3 m_to_end_direction  {0.0};
    glm::dvec3 m_to_start_direction{0.0};
    double     m_target_mesh_size  {0.0};
};

} // namespace editor
