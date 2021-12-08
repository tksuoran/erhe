#pragma once

#include "command.hpp"
#include "tools/tool.hpp"
#include "windows/imgui_window.hpp"

#include "erhe/components/component.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <memory>

namespace erhe::scene {
    class Mesh;
}

namespace erhe::physics {
    class IConstraint;
}

namespace editor
{

class Line_renderer;
class Node_physics;
class Physics_tool;
class Pointer_context;
class Scene_root;

class Physics_tool_drag_command
    : public Command
{
public:
    Physics_tool_drag_command(Physics_tool& physics_tool)
        : Command       {"Physics_tool.drag"}
        , m_physics_tool{physics_tool}
    {
    }

    auto try_call   (Command_context& context) -> bool override;
    void try_ready  (Command_context& context) override;
    void on_inactive(Command_context& context) override;

private:
    Physics_tool& m_physics_tool;
};

class Physics_tool
    : public erhe::components::Component
    , public Tool
{
public:
    static constexpr int              c_priority   {2};
    static constexpr std::string_view c_name       {"Physics_tool"};
    static constexpr std::string_view c_description{"Physics Tool"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Physics_tool ();
    ~Physics_tool() override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements Tool
    [[nodiscard]] auto tool_priority() const -> int     override { return c_priority; }
    [[nodiscard]] auto description  () -> const char*   override;
    void tool_render    (const Render_context& context) override;
    void tool_properties() override;

    // Commands
    auto on_drag_ready() -> bool;
    auto on_drag      () -> bool;
    void end_drag     ();

private:
    Physics_tool_drag_command                   m_drag_command;

    Line_renderer*                              m_line_renderer  {nullptr};
    Pointer_context*                            m_pointer_context{nullptr};
    Scene_root*                                 m_scene_root     {nullptr};

    std::shared_ptr<erhe::scene::Mesh>          m_drag_mesh;
    std::shared_ptr<Node_physics>               m_drag_node_physics;
    float                                       m_drag_depth{0.5f};
    glm::vec3                                   m_drag_position_in_mesh{0.0f, 0.0f, 0.0f};
    glm::vec3                                   m_drag_position_start  {0.0f, 0.0f, 0.0f};
    glm::vec3                                   m_drag_position_end    {0.0f, 0.0f, 0.0f};
    std::unique_ptr<erhe::physics::IConstraint> m_drag_constraint;

    double   m_mouse_x                 { 0.0};
    double   m_mouse_y                 { 0.0};
    float    m_tau                     { 0.001f};
    float    m_damping                 { 1.00f};
    float    m_impulse_clamp           { 1.00f};
    float    m_linear_damping          { 0.99f};
    float    m_angular_damping         { 0.99f};
    float    m_original_linear_damping { 0.00f};
    float    m_original_angular_damping{ 0.00f};
    uint64_t m_last_update_frame_number{0};
};

} // namespace editor
