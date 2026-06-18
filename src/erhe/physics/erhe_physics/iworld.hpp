#pragma once

#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace erhe::renderer {
    class Jolt_debug_renderer;
}

namespace erhe::physics {

void initialize_physics_system();

class IConstraint;
class IRigid_body;
class IRigid_body_create_info;
class Transform;

// Sensor (trigger volume) overlap event: another body started or stopped
// overlapping a body created with IRigid_body_create_info::is_sensor = true.
class Trigger_event
{
public:
    IRigid_body* sensor{nullptr};
    IRigid_body* other {nullptr};
};

class IWorld
{
public:
    virtual ~IWorld() noexcept;

    [[nodiscard]] static auto create       () -> IWorld*;
    [[nodiscard]] static auto create_shared() -> std::shared_ptr<IWorld>;
    [[nodiscard]] static auto create_unique() -> std::unique_ptr<IWorld>;

    [[nodiscard]] virtual auto create_rigid_body       (const IRigid_body_create_info& create_info) -> IRigid_body* = 0;
    [[nodiscard]] virtual auto create_rigid_body_shared(const IRigid_body_create_info& create_info) -> std::shared_ptr<IRigid_body> = 0;

    [[nodiscard]] virtual auto get_gravity         () const -> glm::vec3                 = 0;
    [[nodiscard]] virtual auto get_rigid_body_count() const -> std::size_t               = 0;
    [[nodiscard]] virtual auto get_constraint_count() const -> std::size_t               = 0;
    [[nodiscard]] virtual auto describe            () const -> std::vector<std::string>  = 0;
    virtual void update_fixed_step      (double dt)                                      = 0;
    virtual void add_rigid_body         (IRigid_body* rigid_body)                        = 0;
    virtual void remove_rigid_body      (IRigid_body* rigid_body)                        = 0;
    virtual void add_constraint         (IConstraint* constraint)                        = 0;
    virtual void remove_constraint      (IConstraint* constraint)                        = 0;
    virtual void set_gravity            (const glm::vec3& gravity)                       = 0;
    virtual void debug_draw             (erhe::renderer::Jolt_debug_renderer& debug_renderer) = 0;
    virtual void sanity_check           ()                                               = 0;
    virtual void set_on_body_activated  (std::function<void(IRigid_body*)> callback)     = 0;
    virtual void set_on_body_deactivated(std::function<void(IRigid_body*)> callback)     = 0;
    virtual void for_each_active_body   (std::function<void(IRigid_body*)> callback)     = 0;

    // Trigger (sensor) overlap callbacks. Events are collected during
    // update_fixed_step() and dispatched on the calling thread at the end of
    // that same update_fixed_step() call.
    virtual void set_on_trigger_enter   (std::function<void(const Trigger_event&)> callback) = 0;
    virtual void set_on_trigger_exit    (std::function<void(const Trigger_event&)> callback) = 0;

    // Enables / disables collision between one specific pair of bodies
    // (used for joint enableCollision = false pair exclusion). Must be
    // called outside update_fixed_step().
    virtual void set_collision_enabled  (IRigid_body* rigid_body_a, IRigid_body* rigid_body_b, bool enabled) = 0;

    // Opaque saved simulation state produced by save_state() and consumed by
    // restore_state() (a Jolt StateRecorder under the hood).
    class State
    {
    public:
        virtual ~State() noexcept;
    };

    // Save / restore the full simulation state (body transforms, velocities,
    // activation, contacts). Intended for trial placement: snapshot, move bodies
    // (IRigid_body::set_world_transform) and test them (bodies_intersect), then
    // restore to undo the trial moves. restore_state() requires the same set of
    // bodies that existed when the snapshot was taken. Must be called outside
    // update_fixed_step().
    [[nodiscard]] virtual auto save_state   () -> std::unique_ptr<State> = 0;
    virtual void               restore_state(State& state)               = 0;

    // Narrow-phase geometric overlap tests for trial placement. The tested body's
    // shape is placed at the given world (node-space rotation + translation)
    // transform without moving the body, so the world is not mutated and no
    // snapshot is needed. Collision filters are ignored (pure geometric query);
    // touching contacts (penetration <= tolerance) are not reported, so
    // feature-aligned bodies that merely touch do not count as intersecting.

    // True if body_a at transform_a interpenetrates body_b at transform_b.
    [[nodiscard]] virtual auto would_bodies_intersect(
        const IRigid_body& body_a, const Transform& transform_a,
        const IRigid_body& body_b, const Transform& transform_b,
        float penetration_tolerance
    ) const -> bool = 0;

    // True if body at transform interpenetrates any other body in the world
    // (body itself excluded), tested against those bodies at their current
    // world positions.
    [[nodiscard]] virtual auto would_body_intersect_world(
        const IRigid_body& body, const Transform& transform,
        float penetration_tolerance
    ) const -> bool = 0;
};

} // namespace erhe::physics
