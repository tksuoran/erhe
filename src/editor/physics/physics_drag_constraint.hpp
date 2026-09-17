#pragma once

#include "physics/physics_drag_monitor.hpp"

#include <glm/glm.hpp>

#include <limits>
#include <memory>

namespace erhe::physics {
    class IConstraint;
    class IRigid_body;
    class IWorld;
}

namespace editor {

// How the drag point body follows a new drag point position.
enum class Drag_point_motion : unsigned int {
    teleport  = 0, // kinematic non-physical: placed instantly, carries no velocity
    kinematic = 1  // kinematic physical: moved over a step, carries the velocity of the move
};

// How hard the drag pulls; see erhe::physics::Point_to_point_constraint_settings.
class Physics_drag_constraint_settings
{
public:
    float        frequency                 {0.0f}; // Hz; 0 = rigid
    float        damping                   {1.0f}; // damping ratio
    float        max_force                 {std::numeric_limits<float>::infinity()}; // newtons, spring only
    unsigned int solver_velocity_iterations{0};    // 0 = world default
    unsigned int solver_position_iterations{0};    // 0 = world default
};

// The spring pull of an interactive physics drag: a collisionless kinematic
// drag point body and a point-to-point constraint from a pivot on the dragged
// dynamic body to it. The dragged body stays dynamic, so the simulation
// (gravity, contacts, the body's joints) decides where it actually goes.
// Shared by the Physics tool (right-drag) and the Transform tool (gizmo drag
// of a jointed dynamic body).
//
// attach() announces the drag to the scene's Physics_drag_monitor named by
// the monitor info (editor.physics_drag diagnostics); detach() withdraws it.
//
// The pivot follows erhe::physics::Point_to_point_constraint_settings.
//
// The owner detaches before the world or the dragged body goes away: on scene
// close and when the dragged node is removed (AGENTS.md "Scene-hosted
// references in editor parts").
class Physics_drag_constraint
{
public:
    Physics_drag_constraint();
    ~Physics_drag_constraint() noexcept;
    Physics_drag_constraint(const Physics_drag_constraint&) = delete;
    auto operator=(const Physics_drag_constraint&) -> Physics_drag_constraint& = delete;
    Physics_drag_constraint(Physics_drag_constraint&&) = delete;
    auto operator=(Physics_drag_constraint&&) -> Physics_drag_constraint& = delete;

    // Creates the drag point body at drag_point_in_world, keeps the dragged
    // body awake (begin_move) and adds the constraint. Detaches a previous
    // attachment first.
    auto attach(
        erhe::physics::IWorld&                  world,
        erhe::physics::IRigid_body&             body,
        glm::vec3                               pivot_in_body,
        glm::vec3                               drag_point_in_world,
        const Physics_drag_constraint_settings& settings,
        const Physics_drag_monitor_info&        monitor_info
    ) -> bool;

    void move_drag_point(glm::vec3 position_in_world, Drag_point_motion motion);

    // Removes the constraint and the drag point body and lets the dragged
    // body sleep again (end_move). The dragged body keeps its velocity.
    void detach();

    [[nodiscard]] auto is_attached         () const -> bool;
    [[nodiscard]] auto get_body            () const -> erhe::physics::IRigid_body*;
    [[nodiscard]] auto get_drag_point_body () const -> erhe::physics::IRigid_body*;
    [[nodiscard]] auto get_pivot_in_body   () const -> glm::vec3;
    [[nodiscard]] auto get_settings        () const -> const Physics_drag_constraint_settings&;

private:
    erhe::physics::IWorld*                      m_world{nullptr};
    erhe::physics::IRigid_body*                 m_body {nullptr};
    std::unique_ptr<erhe::physics::IConstraint> m_constraint;
    std::shared_ptr<erhe::physics::IRigid_body> m_drag_point_body;
    glm::vec3                                   m_pivot_in_body{0.0f, 0.0f, 0.0f};
    Physics_drag_constraint_settings            m_settings{};
    Physics_drag_monitor*                       m_monitor{nullptr};
};

}
