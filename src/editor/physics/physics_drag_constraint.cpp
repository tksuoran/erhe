#include "physics/physics_drag_constraint.hpp"

#include "scene/joint.hpp"
#include "scene/joint_system.hpp"
#include "scene/node_physics_system.hpp"
#include "scene/scene_root.hpp"

#include "erhe_log/log_glm.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_physics/iconstraint.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_physics/iworld.hpp"
#include "erhe_scene/node.hpp"

#include <fmt/format.h>

#include <glm/gtc/constants.hpp>

#include <cmath>

namespace editor {

namespace {

// Rotation + translation of the node world transform; scale ignored. The
// convention Joint_system builds the joint frames with.
[[nodiscard]] auto world_transform_of(const erhe::scene::Node& node) -> erhe::physics::Transform
{
    const erhe::scene::Trs_transform& world_from_node = node.world_from_node_transform();
    return erhe::physics::Transform{
        glm::mat3_cast(world_from_node.get_rotation()),
        world_from_node.get_translation()
    };
}

[[nodiscard]] auto name_of(const Node_physics_entry* const entry) -> std::string
{
    if (entry == nullptr) {
        return "world";
    }
    return (entry->node != nullptr) ? entry->node->get_name() : std::string{"(detached)"};
}

[[nodiscard]] auto joint_name_of(const Joint& joint) -> std::string
{
    return joint.get_name();
}

constexpr const char* c_axis_names[3] = { "X", "Y", "Z" };

} // anonymous namespace

auto make_jointed_body_drag_settings(const float body_mass) -> Physics_drag_constraint_settings
{
    return Physics_drag_constraint_settings{
        .frequency                  = c_jointed_drag_frequency,
        .damping                    = c_jointed_drag_damping,
        .max_force                  = c_jointed_drag_max_force_in_body_weights * body_mass * c_standard_gravity,
        .solver_velocity_iterations = c_jointed_drag_solver_velocity_iterations,
        .solver_position_iterations = c_jointed_drag_solver_position_iterations,
        .drag_point_speed           = Drag_point_speed::braking,
        .limit_margin               = c_jointed_drag_limit_margin
    };
}

Physics_drag_constraint::Physics_drag_constraint() = default;

Physics_drag_constraint::~Physics_drag_constraint() noexcept
{
    detach();
}

auto Physics_drag_constraint::attach(
    Scene_root&                             scene_root,
    erhe::physics::IRigid_body&             body,
    const glm::vec3                         pivot_in_body,
    const glm::vec3                         drag_point_in_world,
    const Physics_drag_constraint_settings& settings
) -> bool
{
    detach();

    erhe::physics::IWorld& world = scene_root.get_physics_world();

    m_world    = &world;
    m_body     = &body;
    m_settings = settings;

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
    configure_projection(scene_root.get_joint_system(), drag_point_in_world);
    m_drag_point_speed_limit = std::numeric_limits<float>::infinity();
    if (settings.drag_point_speed == Drag_point_speed::braking) {
        const float brake_acceleration = (settings.max_force / body.get_mass()) - c_standard_gravity;
        const float brake_distance     = m_reach.is_angle_limited()
            ? (m_reach.get_radius() * settings.limit_margin)
            : c_jointed_drag_brake_distance;
        if (std::isfinite(brake_acceleration) && (brake_acceleration > 0.0f)) {
            m_drag_point_speed_limit = std::sqrt(2.0f * brake_acceleration * brake_distance);
        }
    }
    m_drag_point = m_reach.project(drag_point_in_world);
    move_drag_point(drag_point_in_world, Drag_point_motion::teleport);
    place_drag_point(m_drag_point, Drag_point_motion::teleport);
    m_scene_root = &scene_root;
    scene_root.register_physics_drag(this);

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
    return true;
}

void Physics_drag_constraint::configure_projection(
    const Joint_system& joint_system,
    const glm::vec3     pivot_in_world
)
{
    m_reach = erhe::physics::Joint_reach{};

    const Joint_entry* joint      = nullptr;
    std::size_t        live_count = 0;
    for (const std::unique_ptr<Joint_entry>& entry : joint_system.get_entries()) {
        if (!entry->constraint) {
            continue;
        }
        if ((entry->rigid_body_a != m_body) && (entry->rigid_body_b != m_body)) {
            continue;
        }
        joint = entry.get();
        ++live_count;
    }
    if (live_count == 0) {
        m_projection_description = "unprojected: no live joint holds the body";
        return;
    }
    if (live_count > 1) {
        m_projection_description = fmt::format("unprojected: {} live joints hold the body (several anchors are not handled)", live_count);
        return;
    }

    const Joint_constraint_state& state = joint->state;
    const bool moving_a =
        (state.node_physics_a != nullptr) &&
        (state.node_physics_a->rigid_body.get() == m_body);
    const erhe::physics::Joint_side  side   = moving_a ? erhe::physics::Joint_side::a : erhe::physics::Joint_side::b;
    const Node_physics_entry* const  moving = moving_a ? state.node_physics_a : state.node_physics_b;
    const Node_physics_entry* const  fixed  = moving_a ? state.node_physics_b : state.node_physics_a;
    const std::string joint_name = joint_name_of(*joint->joint);
    if ((moving == nullptr) || (moving->node == nullptr)) {
        m_projection_description = fmt::format("unprojected: joint '{}' body node not found", joint_name);
        return;
    }
    if (fixed != nullptr) {
        const erhe::physics::IRigid_body* const fixed_body = fixed->rigid_body.get();
        if ((fixed_body == nullptr) || (fixed->node == nullptr)) {
            m_projection_description = fmt::format("unprojected: joint '{}' anchor body not found", joint_name);
            return;
        }
        if (fixed_body->get_motion_mode() == erhe::physics::Motion_mode::e_dynamic) {
            m_projection_description = fmt::format(
                "unprojected: joint '{}' connects to dynamic body '{}' (not a fixed anchor)",
                joint_name, name_of(fixed)
            );
            return;
        }
    }

    const erhe::physics::Transform& frame_in_moving = moving_a ? state.frame_in_a : state.frame_in_b;
    const erhe::physics::Transform& frame_in_fixed  = moving_a ? state.frame_in_b : state.frame_in_a;
    const erhe::physics::Transform  world_from_moving_anchor = world_transform_of(*moving->node) * frame_in_moving;
    const erhe::physics::Transform  world_from_fixed_anchor  = (fixed == nullptr)
        ? frame_in_fixed // world-anchored side: the frame is in world space
        : (world_transform_of(*fixed->node) * frame_in_fixed);
    const erhe::physics::Transform moving_anchor_from_world = inverse(world_from_moving_anchor);
    const glm::vec3 pivot_in_moving_anchor = (moving_anchor_from_world.basis * pivot_in_world) + moving_anchor_from_world.origin;

    m_reach.configure(world_from_fixed_anchor, side, state.limits, pivot_in_moving_anchor, pivot_in_world, m_settings.limit_margin);

    const std::string anchor = fmt::format(
        "joint '{}', dragged body is side {}, anchor '{}' at {}",
        joint_name, moving_a ? "A" : "B", name_of(fixed), world_from_fixed_anchor.origin
    );
    switch (m_reach.get_shape()) {
        case erhe::physics::Joint_reach_shape::circle: {
            const int axis = m_reach.get_axis();
            const erhe::physics::Constraint_axis_limit& limit = state.limits[3 + static_cast<std::size_t>(axis)];
            const float to_degrees = 180.0f / glm::pi<float>();
            m_projection_description = fmt::format(
                "projected: circle ({}; rotation axis {} = world {}, center {}, radius {:.4f} m, joint angle {}, margin {:.3f} rad)",
                anchor,
                c_axis_names[axis],
                world_from_fixed_anchor.basis[axis],
                m_reach.get_center(), m_reach.get_radius(),
                limit.limited ? fmt::format("{:.2f} .. {:.2f} deg", limit.min * to_degrees, limit.max * to_degrees) : std::string{"free"},
                limit.limited ? m_settings.limit_margin : 0.0f
            );
            break;
        }
        case erhe::physics::Joint_reach_shape::sphere: {
            m_projection_description = fmt::format(
                "projected: sphere ({}; center {}, radius {:.4f} m, angular limits not applied)",
                anchor, m_reach.get_center(), m_reach.get_radius()
            );
            break;
        }
        case erhe::physics::Joint_reach_shape::box: {
            m_projection_description = fmt::format("projected: box ({}; per-axis translation ranges)", anchor);
            break;
        }
        case erhe::physics::Joint_reach_shape::point: {
            m_projection_description = fmt::format(
                "projected: point ({}; no free axis moves the pivot, it stays at {})",
                anchor, m_reach.get_last_projected()
            );
            break;
        }
        case erhe::physics::Joint_reach_shape::unprojected:
        default: {
            m_projection_description = fmt::format(
                "unprojected: {}; limit combination not handled (translation and rotation both free, or a rotation fixed at a non-zero angle)",
                anchor
            );
            break;
        }
    }
}

void Physics_drag_constraint::move_drag_point(const glm::vec3 requested_position_in_world, const Drag_point_motion motion)
{
    if (!m_drag_point_body) {
        return;
    }
    m_requested_drag_point = requested_position_in_world;
    m_projected_drag_point = m_reach.project(requested_position_in_world);
    if (m_settings.drag_point_speed == Drag_point_speed::immediate) {
        m_drag_point = m_projected_drag_point;
        place_drag_point(m_drag_point, motion);
    }
}

void Physics_drag_constraint::on_fixed_step(const float dt)
{
    if (!m_drag_point_body || (m_settings.drag_point_speed != Drag_point_speed::braking)) {
        return;
    }
    const glm::vec3 next = m_reach.step_toward(m_drag_point, m_projected_drag_point, m_drag_point_speed_limit * dt);
    if (next == m_drag_point) {
        return;
    }
    m_drag_point = next;
    place_drag_point(m_drag_point, Drag_point_motion::teleport);
}

void Physics_drag_constraint::place_drag_point(const glm::vec3 position_in_world, const Drag_point_motion motion)
{
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
    if (m_scene_root != nullptr) {
        m_scene_root->unregister_physics_drag(this);
        m_scene_root = nullptr;
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

auto Physics_drag_constraint::get_projection_description() const -> const std::string&
{
    return m_projection_description;
}

auto Physics_drag_constraint::get_requested_drag_point() const -> glm::vec3
{
    return m_requested_drag_point;
}

auto Physics_drag_constraint::get_projected_drag_point() const -> glm::vec3
{
    return m_projected_drag_point;
}

}
