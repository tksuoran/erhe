#pragma once

#include "erhe_physics/joint_reach.hpp"

#include <glm/glm.hpp>

#include <limits>
#include <memory>
#include <span>
#include <string>

namespace erhe::physics {
    class IConstraint;
    class IRigid_body;
    class IWorld;
}

namespace editor {

class Node_joint;
class Scene_root;

// How the drag point body follows a new drag point position.
enum class Drag_point_motion : unsigned int {
    teleport  = 0, // kinematic non-physical: placed instantly, carries no velocity
    kinematic = 1  // kinematic physical: moved over a step, carries the velocity of the move
};

// How the drag point body reaches the projected drag target.
enum class Drag_point_speed : unsigned int {
    immediate = 0, // move_drag_point() places it at once
    braking   = 1  // each fixed physics step advances it along the joint reach at most
                   // the drag's speed limit * dt; see Physics_drag_constraint
};

// How hard the drag pulls; see erhe::physics::Point_to_point_constraint_settings.
class Physics_drag_constraint_settings
{
public:
    float            frequency                 {0.0f}; // Hz; 0 = rigid
    float            damping                   {1.0f}; // damping ratio
    float            max_force                 {std::numeric_limits<float>::infinity()}; // newtons, spring only
    unsigned int     solver_velocity_iterations{0};    // 0 = world default
    unsigned int     solver_position_iterations{0};    // 0 = world default
    Drag_point_speed drag_point_speed          {Drag_point_speed::immediate};
    float            limit_margin              {0.0f}; // radians the projected target stays inside a limited rotation range
};

// The pull of a drag of a dynamic body held by a live joint, shared by the
// Physics tool and the Transform tool: a spring of c_jointed_drag_frequency
// (critically damped) whose force is bounded to c_jointed_drag_max_force_in_body_weights
// times the body's weight, with raised Jolt solver iterations.
//
// The static sag of a spring-hung body under gravity is g / (2 pi f)^2
// independent of mass: 2.5 mm at 10 Hz, so a jointed body follows the drag
// closely. The force bound is enough to lift the body and push what it leans
// on, while the joints never carry more: pulling against a joint deflects the
// joint by the pull over the joint's stiffness. Unbounded, a 10 Hz pull
// dragged 0.25 m out of a cradle ball's swing plane tore its hinge 48 mm
// (Jolt) / 112 mm (Box3D) apart. Jolt's default solver iterations (10
// velocity / 2 position) leave the joints visibly strained under a steady
// pull; the drag raises the dragged body's island for the constraint's
// lifetime.
constexpr float        c_jointed_drag_frequency                 = 10.0f;
constexpr float        c_jointed_drag_damping                   = 1.0f;
constexpr float        c_jointed_drag_max_force_in_body_weights = 3.0f;
constexpr float        c_standard_gravity                       = 9.81f;
constexpr unsigned int c_jointed_drag_solver_velocity_iterations = 40;
constexpr unsigned int c_jointed_drag_solver_position_iterations = 20;

// A drag point that jumps (a fast cursor) lets the bounded pull accelerate the
// body until it slams into a joint limit: measured 4-6 m/s cradle balls
// hitting the +-80 deg hinge limit with 12-25 mm hinge separation. The
// jointed drag therefore keeps its target c_jointed_drag_limit_margin inside
// a limited rotation range and advances the drag point at a speed the bounded
// force can brake to rest within that margin:
//   v_max = sqrt(2 * a_brake * d),  a_brake = max_force / mass - g,
//   d = radius * margin for a limited circle reach, else c_jointed_drag_brake_distance.
constexpr float        c_jointed_drag_limit_margin              = 0.1f;  // radians
constexpr float        c_jointed_drag_brake_distance            = 0.05f; // meters

[[nodiscard]] auto make_jointed_body_drag_settings(float body_mass) -> Physics_drag_constraint_settings;

// The spring pull of an interactive physics drag: a collisionless kinematic
// drag point body and a point-to-point constraint from a pivot on the dragged
// dynamic body to it. The dragged body stays dynamic, so the simulation
// (gravity, contacts, the body's joints) decides where it actually goes.
// Shared by the Physics tool (right-drag) and the Transform tool (gizmo drag
// of a jointed dynamic body).
//
// The pivot follows erhe::physics::Point_to_point_constraint_settings.
//
// Joint-space projection: attach() looks at the live joints (of node_joints)
// that constrain the dragged body. When exactly one does and its other side
// is the world or a non-dynamic body (a fixed anchor frame), every drag point
// requested by move_drag_point() is first projected onto the positions the
// joint lets the drag pivot reach (erhe::physics::Joint_reach: circle for a
// hinge, sphere for a ball joint, box for a slider, point when nothing
// moves), so the pull never fights the joint. Joints to dynamic bodies,
// several joints on the body, and limit combinations Joint_reach does not
// handle leave the drag point unprojected; the choice is reported by
// get_projection_description() (the MCP physics_drag tool returns it). The
// anchor frame is captured at attach().
//
// With Drag_point_speed::braking the drag point body does not jump to the
// projected target: Scene_root calls on_fixed_step() before every fixed
// physics step, which advances it along the reach
// (erhe::physics::Joint_reach::step_toward) by at most
// its speed limit * dt (see c_jointed_drag_limit_margin).
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
    // drag_point_in_world is where the pivot is at attach time (the
    // projection's reach is built from it). The scene's physics world and
    // joints are used; the drag registers for on_fixed_step() with the scene
    // until detach().
    auto attach(
        Scene_root&                             scene_root,
        erhe::physics::IRigid_body&             body,
        glm::vec3                               pivot_in_body,
        glm::vec3                               drag_point_in_world,
        const Physics_drag_constraint_settings& settings
    ) -> bool;

    // Projects the requested position (see the class comment); an immediate
    // drag moves the drag point body there, a braking drag makes it the
    // target on_fixed_step() advances toward.
    void move_drag_point(glm::vec3 requested_position_in_world, Drag_point_motion motion);

    // Scene_root::update_physics_simulation_fixed_step(), before the step.
    void on_fixed_step(float dt);

    // Removes the constraint and the drag point body and lets the dragged
    // body sleep again (end_move). The dragged body keeps its velocity.
    void detach();

    [[nodiscard]] auto is_attached         () const -> bool;
    [[nodiscard]] auto get_body            () const -> erhe::physics::IRigid_body*;
    [[nodiscard]] auto get_drag_point_body () const -> erhe::physics::IRigid_body*;
    [[nodiscard]] auto get_projection_description() const -> const std::string&;
    [[nodiscard]] auto get_requested_drag_point  () const -> glm::vec3;
    [[nodiscard]] auto get_projected_drag_point  () const -> glm::vec3;

private:
    Scene_root*                                 m_scene_root{nullptr};
    erhe::physics::IWorld*                      m_world{nullptr};
    erhe::physics::IRigid_body*                 m_body {nullptr};
    std::unique_ptr<erhe::physics::IConstraint> m_constraint;
    std::shared_ptr<erhe::physics::IRigid_body> m_drag_point_body;
    Physics_drag_constraint_settings            m_settings{};
    erhe::physics::Joint_reach                  m_reach;
    std::string                                 m_projection_description;
    glm::vec3                                   m_requested_drag_point{0.0f};
    glm::vec3                                   m_projected_drag_point{0.0f};
    glm::vec3                                   m_drag_point          {0.0f};
    float                                       m_drag_point_speed_limit{std::numeric_limits<float>::infinity()}; // m/s; infinity for an immediate drag

    void configure_projection(std::span<const std::shared_ptr<Node_joint>> node_joints, glm::vec3 pivot_in_world);
    void place_drag_point    (glm::vec3 position_in_world, Drag_point_motion motion);
};

}
