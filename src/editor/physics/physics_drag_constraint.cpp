#include "physics/physics_drag_constraint.hpp"

#include "erhe_physics/icollision_shape.hpp"
#include "erhe_physics/iconstraint.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_physics/iworld.hpp"

namespace editor {

Physics_drag_constraint::Physics_drag_constraint() = default;

Physics_drag_constraint::~Physics_drag_constraint() noexcept
{
    detach();
}

auto Physics_drag_constraint::attach(
    erhe::physics::IWorld&                  world,
    erhe::physics::IRigid_body&             body,
    const glm::vec3                         pivot_in_body,
    const glm::vec3                         drag_point_in_world,
    const Physics_drag_constraint_settings& settings,
    const Physics_drag_monitor_info&        monitor_info
) -> bool
{
    detach();

    m_world         = &world;
    m_body          = &body;
    m_pivot_in_body = pivot_in_body;
    m_settings      = settings;

    m_drag_point_body = world.create_rigid_body_shared(
        erhe::physics::IRigid_body_create_info{
            .collision_shape   = erhe::physics::ICollision_shape::create_empty_shape_shared(),
            .mass              = 10.0f,
            .debug_label       = "Drag point",
            .enable_collisions = false,
            .motion_mode       = erhe::physics::Motion_mode::e_kinematic_non_physical
        }
    );
    if (!m_drag_point_body) {
        m_world = nullptr;
        m_body  = nullptr;
        return false;
    }
    world.add_rigid_body(m_drag_point_body.get());
    move_drag_point(drag_point_in_world, Drag_point_motion::teleport);

    body.begin_move();

    m_constraint = erhe::physics::IConstraint::create_point_to_point_constraint_unique(
        erhe::physics::Point_to_point_constraint_settings{
            .rigid_body_a               = &body,
            .rigid_body_b               = m_drag_point_body.get(),
            .pivot_in_a                 = pivot_in_body,
            .pivot_in_b                 = glm::vec3{0.0f, 0.0f, 0.0f},
            .frequency                  = settings.frequency,
            .damping                    = settings.damping,
            .max_force                  = settings.max_force,
            .solver_velocity_iterations = settings.solver_velocity_iterations,
            .solver_position_iterations = settings.solver_position_iterations
        }
    );
    world.add_constraint(m_constraint.get());

    m_monitor = monitor_info.monitor;
    if (m_monitor != nullptr) {
        m_monitor->begin(*this, world, monitor_info);
    }
    return true;
}

void Physics_drag_constraint::move_drag_point(const glm::vec3 position_in_world, const Drag_point_motion motion)
{
    if (!m_drag_point_body) {
        return;
    }
    m_drag_point_body->set_motion_mode(
        (motion == Drag_point_motion::kinematic)
            ? erhe::physics::Motion_mode::e_kinematic_physical
            : erhe::physics::Motion_mode::e_kinematic_non_physical
    );
    m_drag_point_body->set_world_transform(
        erhe::physics::Transform{glm::mat3{1.0f}, position_in_world}
    );
}

void Physics_drag_constraint::detach()
{
    // The monitor reads the release velocity, so it hears first.
    if (m_monitor != nullptr) {
        m_monitor->end(*this);
        m_monitor = nullptr;
    }
    // The constraint goes first: Box3D destroys the joint with the constraint
    // object, and the joint must not outlive the drag point body.
    if (m_constraint) {
        if (m_world != nullptr) {
            m_world->remove_constraint(m_constraint.get());
        }
        m_constraint.reset();
    }
    if (m_body != nullptr) {
        m_body->end_move();
        m_body = nullptr;
    }
    if (m_drag_point_body) {
        if (m_world != nullptr) {
            m_world->remove_rigid_body(m_drag_point_body.get());
        }
        m_drag_point_body.reset();
    }
    m_world = nullptr;
}

auto Physics_drag_constraint::is_attached() const -> bool
{
    return static_cast<bool>(m_constraint);
}

auto Physics_drag_constraint::get_body() const -> erhe::physics::IRigid_body*
{
    return m_body;
}

auto Physics_drag_constraint::get_drag_point_body() const -> erhe::physics::IRigid_body*
{
    return m_drag_point_body.get();
}

auto Physics_drag_constraint::get_pivot_in_body() const -> glm::vec3
{
    return m_pivot_in_body;
}

auto Physics_drag_constraint::get_settings() const -> const Physics_drag_constraint_settings&
{
    return m_settings;
}

}
