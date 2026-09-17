#include "physics/physics_drag_monitor.hpp"

#include "editor_log.hpp"
#include "physics/physics_drag_constraint.hpp"
#include "scene/node_joint.hpp"
#include "scene/node_physics.hpp"

#include "erhe_log/log_glm.hpp"
#include "erhe_physics/iconstraint.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_physics/iworld.hpp"
#include "erhe_physics/physics_joint_settings.hpp"
#include "erhe_scene/node.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace editor {

namespace {

#if defined(ERHE_PHYSICS_LIBRARY_BOX3D)
constexpr const char* c_backend_name = "Box3D";
#elif defined(ERHE_PHYSICS_LIBRARY_JOLT)
constexpr const char* c_backend_name = "Jolt";
#else
constexpr const char* c_backend_name = "none";
#endif

// Detail lines are logged for joints whose limit violation exceeds these.
constexpr float c_detail_violation_mm  = 1.0f;
constexpr float c_detail_violation_deg = 1.0f;

constexpr const char* c_axis_names[6] = { "TX", "TY", "TZ", "RX", "RY", "RZ" };

// Rotation + translation of the node world transform; scale ignored. The same
// convention Node_joint::try_create_constraint() builds the joint frames with.
[[nodiscard]] auto world_transform_of(const erhe::scene::Node& node) -> erhe::physics::Transform
{
    const erhe::scene::Trs_transform& world_from_node = node.world_from_node_transform();
    return erhe::physics::Transform{
        glm::mat3_cast(world_from_node.get_rotation()),
        world_from_node.get_translation()
    };
}

[[nodiscard]] auto to_quat(const glm::mat3& basis) -> glm::quat
{
    return glm::normalize(glm::quat_cast(basis));
}

// Rotation vector (axis * angle, radians) of the shortest rotation q.
[[nodiscard]] auto rotation_vector(glm::quat q) -> glm::vec3
{
    if (q.w < 0.0f) {
        q = -q;
    }
    const glm::vec3 xyz{q.x, q.y, q.z};
    const float     sin_half = glm::length(xyz);
    if (sin_half < 1.0e-7f) {
        return 2.0f * xyz;
    }
    const float angle = 2.0f * std::atan2(sin_half, q.w);
    return (xyz / sin_half) * angle;
}

[[nodiscard]] auto axis_violation(const erhe::physics::Constraint_axis_limit& limit, const float value) -> float
{
    if (!limit.limited) {
        return 0.0f;
    }
    if (value > limit.max) {
        return value - limit.max;
    }
    if (value < limit.min) {
        return limit.min - value;
    }
    return 0.0f;
}

[[nodiscard]] auto node_name_of(const Node_physics* node_physics) -> const char*
{
    if (node_physics == nullptr) {
        return "world";
    }
    const erhe::scene::Node* node = node_physics->get_node();
    return (node != nullptr) ? node->get_name().c_str() : "(detached)";
}

[[nodiscard]] auto joint_name_of(const Node_joint& joint) -> const std::string&
{
    const erhe::scene::Node* node = joint.get_node();
    return (node != nullptr) ? node->get_name() : joint.get_name();
}

[[nodiscard]] auto degrees(const float radians) -> float
{
    return radians * (180.0f / glm::pi<float>());
}

} // anonymous namespace

Physics_drag_monitor::Physics_drag_monitor(const std::vector<std::shared_ptr<Node_joint>>& node_joints)
    : m_node_joints{node_joints}
{
}

Physics_drag_monitor::~Physics_drag_monitor() noexcept = default;

void Physics_drag_monitor::on_fixed_step(const double dt)
{
    m_last_fixed_step_dt = dt;
    if (m_drags.empty()) {
        return;
    }
    ++m_frame_fixed_steps;
}

void Physics_drag_monitor::log_joint_dump(const Node_joint& joint) const
{
    const Node_joint_constraint_state* const state = joint.get_constraint_state();
    if (state == nullptr) {
        return;
    }
    const std::shared_ptr<erhe::scene::Node>&                     connected_node = joint.get_connected_node();
    const std::shared_ptr<erhe::physics::Physics_joint_settings>& settings       = joint.get_settings();
    const erhe::physics::IConstraint* const                       constraint     = joint.get_constraint();
    const erhe::physics::Constraint_diagnostics diagnostics = (constraint != nullptr) ? constraint->get_diagnostics() : erhe::physics::Constraint_diagnostics{};
    log_physics_drag->info(
        "  joint '{}': connected node '{}', body A '{}', body B '{}', enable_collision {}, settings '{}', backend joint '{}'",
        joint_name_of(joint),
        connected_node ? connected_node->get_name().c_str() : "(none)",
        node_name_of(state->node_physics_a),
        node_name_of(state->node_physics_b),
        joint.get_enable_collision(),
        settings ? settings->get_name().c_str() : "(none)",
        diagnostics.kind
    );
    const glm::quat q_a = to_quat(state->frame_in_a.basis);
    const glm::quat q_b = to_quat(state->frame_in_b.basis);
    log_physics_drag->info(
        "    frame_in_a t {} q (w {:.4f}, {:.4f}, {:.4f}, {:.4f}); frame_in_b ({}) t {} q (w {:.4f}, {:.4f}, {:.4f}, {:.4f})",
        state->frame_in_a.origin, q_a.w, q_a.x, q_a.y, q_a.z,
        (state->node_physics_b != nullptr) ? "body B node space" : "world space",
        state->frame_in_b.origin, q_b.w, q_b.x, q_b.y, q_b.z
    );
    for (std::size_t i = 0; i < 6; ++i) {
        const erhe::physics::Constraint_axis_limit& limit = state->limits[i];
        if (!limit.limited) {
            log_physics_drag->info("    limit {}: free", c_axis_names[i]);
            continue;
        }
        const bool is_angular = (i >= 3);
        log_physics_drag->info(
            "    limit {}: {:.4f} .. {:.4f} {}, stiffness {}, damping {:.3f}",
            c_axis_names[i],
            is_angular ? degrees(limit.min) : limit.min,
            is_angular ? degrees(limit.max) : limit.max,
            is_angular ? "deg" : "m",
            limit.stiffness.has_value() ? fmt::format("{:.3f}", limit.stiffness.value()) : std::string{"hard"},
            limit.damping
        );
    }
    for (std::size_t i = 0; i < 6; ++i) {
        const erhe::physics::Constraint_axis_drive& drive = state->drives[i];
        if (!drive.enabled) {
            continue;
        }
        log_physics_drag->info(
            "    drive {}: position target {} ({:.4f}), velocity target {:.4f}, stiffness {:.3f}, damping {:.3f}, max force {}",
            c_axis_names[i], drive.use_position_target, drive.position_target, drive.velocity_target,
            drive.stiffness, drive.damping, drive.max_force
        );
    }
}

void Physics_drag_monitor::begin(
    const Physics_drag_constraint&   drag,
    erhe::physics::IWorld&           world,
    const Physics_drag_monitor_info& info
)
{
    const erhe::physics::IRigid_body* const body = drag.get_body();
    if (body == nullptr) {
        return;
    }
    end(drag);

    const Physics_drag_constraint_settings& settings = drag.get_settings();
    const glm::mat4 body_transform = body->get_world_transform();

    Drag_record record{};
    record.drag      = &drag;
    record.tool_name = std::string{info.tool_name};
    record.node_name = (info.node != nullptr) ? info.node->get_name() : std::string{"(unknown)"};

    bool jointed = false;
    for (const std::shared_ptr<Node_joint>& joint : m_node_joints) {
        if (joint->constrains_rigid_body(body)) {
            jointed = true;
        }
    }

    log_physics_drag->info(
        "drag start [{}] backend {}: node '{}', body motion {}, mass {:.4f} kg, active {}, jointed {}",
        record.tool_name, c_backend_name, record.node_name,
        erhe::physics::c_motion_mode_strings[static_cast<int>(body->get_motion_mode())],
        body->get_mass(), body->is_active(), jointed
    );
    log_physics_drag->info(
        "  body world position {}, node world position {}, center of mass {}",
        glm::vec3{body_transform[3]},
        (info.node != nullptr) ? glm::vec3{info.node->position_in_world()} : glm::vec3{0.0f},
        body->get_center_of_mass()
    );
    log_physics_drag->info(
        "  body values before tool: linear damping {:.4f}, angular damping {:.4f}, friction {:.4f}, gravity factor {:.4f}",
        info.values_before_tool.linear_damping, info.values_before_tool.angular_damping,
        info.values_before_tool.friction,       info.values_before_tool.gravity_factor
    );
    log_physics_drag->info(
        "  body values during drag: linear damping {:.4f}, angular damping {:.4f}, friction {:.4f}, gravity factor {:.4f}",
        body->get_linear_damping(), body->get_angular_damping(), body->get_friction(), body->get_gravity_factor()
    );
    log_physics_drag->info(
        "  drag constraint: frequency {:.3f} Hz (0 = rigid), damping ratio {:.3f}, max force {} N, solver iterations velocity {} position {} (0 = world default), pivot in body {}, drag point {}",
        settings.frequency, settings.damping, settings.max_force,
        settings.solver_velocity_iterations, settings.solver_position_iterations,
        drag.get_pivot_in_body(),
        (drag.get_drag_point_body() != nullptr) ? glm::vec3{drag.get_drag_point_body()->get_world_transform()[3]} : glm::vec3{0.0f}
    );
    if (!info.tool_details.empty()) {
        log_physics_drag->info("  tool settings: {}", info.tool_details);
    }
    log_physics_drag->info(
        "  world: {}; gravity {}; last fixed step dt {:.6f} s",
        world.describe_stepping(), world.get_gravity(), m_last_fixed_step_dt
    );

    record.joint_max.reserve(m_node_joints.size());
    std::size_t live_joint_count = 0;
    for (const std::shared_ptr<Node_joint>& joint : m_node_joints) {
        if (joint->get_constraint_state() == nullptr) {
            continue;
        }
        ++live_joint_count;
        record.joint_max.push_back(Joint_max{.joint = joint.get(), .name = joint_name_of(*joint)});
        if (joint->constrains_rigid_body(body)) {
            log_joint_dump(*joint);
        }
    }
    log_physics_drag->info("  {} live joints in the scene are monitored", live_joint_count);

    if (m_measurements.capacity() < m_node_joints.size()) {
        m_measurements.reserve(m_node_joints.size());
    }
    if (m_drags.empty()) {
        m_frame_fixed_steps = 0;
    }
    m_drags.push_back(std::move(record));
}

void Physics_drag_monitor::end(const Physics_drag_constraint& drag)
{
    const auto i = std::find_if(
        m_drags.begin(), m_drags.end(),
        [&drag](const Drag_record& record) { return record.drag == &drag; }
    );
    if (i == m_drags.end()) {
        return;
    }
    const Drag_record& record = *i;
    const erhe::physics::IRigid_body* const body = drag.get_body();
    const glm::vec3 linear_velocity  = (body != nullptr) ? body->get_linear_velocity () : glm::vec3{0.0f};
    const glm::vec3 angular_velocity = (body != nullptr) ? body->get_angular_velocity() : glm::vec3{0.0f};
    log_physics_drag->info(
        "drag end [{}] '{}': {} frames, {} fixed steps; release velocity {} |v| {:.4f} m/s, angular velocity {} |w| {:.4f} rad/s; max spring separation {:.2f} mm",
        record.tool_name, record.node_name, record.frame_count, record.fixed_step_count,
        linear_velocity, glm::length(linear_velocity), angular_velocity, glm::length(angular_velocity),
        record.max_spring_mm
    );
    if (record.has_last_worst && (record.last_worst.joint != nullptr)) {
        const char* name = "(unknown)";
        for (const Joint_max& joint_max : record.joint_max) {
            if (joint_max.joint == record.last_worst.joint) {
                name = joint_max.name.c_str();
            }
        }
        log_physics_drag->info(
            "  final worst joint '{}': separation {:.3f} mm (violation {:.3f} mm), rotation {:.3f} deg (violation {:.3f} deg)",
            name, record.last_worst.separation_mm, record.last_worst.violation_mm,
            record.last_worst.angle_deg, record.last_worst.violation_deg
        );
    }
    for (const Joint_max& joint_max : record.joint_max) {
        log_physics_drag->info(
            "  max during drag, joint '{}': separation {:.3f} mm (violation {:.3f} mm), rotation {:.3f} deg (violation {:.3f} deg)",
            joint_max.name, joint_max.separation_mm, joint_max.violation_mm, joint_max.angle_deg, joint_max.violation_deg
        );
    }
    m_drags.erase(i);
}

void Physics_drag_monitor::measure_joints()
{
    m_measurements.clear();
    for (const std::shared_ptr<Node_joint>& joint : m_node_joints) {
        const Node_joint_constraint_state* const state = joint->get_constraint_state();
        if ((state == nullptr) || (state->node_physics_a == nullptr)) {
            continue;
        }
        const erhe::scene::Node* const node_a = state->node_physics_a->get_node();
        if (node_a == nullptr) {
            continue;
        }
        const erhe::physics::Transform world_from_anchor_a = world_transform_of(*node_a) * state->frame_in_a;
        erhe::physics::Transform       world_from_anchor_b = state->frame_in_b;
        if (state->node_physics_b != nullptr) {
            const erhe::scene::Node* const node_b = state->node_physics_b->get_node();
            if (node_b == nullptr) {
                continue;
            }
            world_from_anchor_b = world_transform_of(*node_b) * state->frame_in_b;
        }

        const glm::mat3 anchor_a_from_world = glm::transpose(world_from_anchor_a.basis);
        const glm::vec3 offset_in_a         = anchor_a_from_world * (world_from_anchor_b.origin - world_from_anchor_a.origin);
        const glm::vec3 rotation_in_a       = rotation_vector(to_quat(anchor_a_from_world * world_from_anchor_b.basis));

        glm::vec3 linear_violation {0.0f};
        glm::vec3 angular_violation{0.0f};
        for (int axis = 0; axis < 3; ++axis) {
            linear_violation [axis] = axis_violation(state->limits[static_cast<std::size_t>(axis)],       offset_in_a  [axis]);
            angular_violation[axis] = axis_violation(state->limits[static_cast<std::size_t>(axis) + 3u], rotation_in_a[axis]);
        }

        Joint_measurement measurement{};
        measurement.joint             = joint.get();
        measurement.separation_mm     = 1000.0f * glm::length(offset_in_a);
        measurement.angle_deg         = degrees(glm::length(rotation_in_a));
        measurement.violation_mm      = 1000.0f * glm::length(linear_violation);
        measurement.violation_deg     = degrees(glm::length(angular_violation));
        measurement.offset_in_a_mm    = 1000.0f * offset_in_a;
        measurement.rotation_in_a_deg = glm::vec3{degrees(rotation_in_a.x), degrees(rotation_in_a.y), degrees(rotation_in_a.z)};
        measurement.score             = std::max(measurement.violation_mm, measurement.violation_deg);
        m_measurements.push_back(measurement);
    }
}

void Physics_drag_monitor::after_physics_simulation_steps()
{
    if (m_drags.empty()) {
        return;
    }

    measure_joints();

    const Joint_measurement* worst = nullptr;
    std::size_t over_threshold_count = 0;
    for (const Joint_measurement& measurement : m_measurements) {
        if ((worst == nullptr) || (measurement.score > worst->score)) {
            worst = &measurement;
        }
        if ((measurement.violation_mm > c_detail_violation_mm) || (measurement.violation_deg > c_detail_violation_deg)) {
            ++over_threshold_count;
        }
    }

    for (Drag_record& record : m_drags) {
        const erhe::physics::IRigid_body* const body            = record.drag->get_body();
        const erhe::physics::IRigid_body* const drag_point_body = record.drag->get_drag_point_body();
        if ((body == nullptr) || (drag_point_body == nullptr)) {
            continue;
        }
        ++record.frame_count;
        record.fixed_step_count += m_frame_fixed_steps;

        const glm::mat4 body_transform   = body->get_world_transform();
        const glm::vec3 drag_point       = glm::vec3{drag_point_body->get_world_transform()[3]};
        const glm::vec3 pivot_in_world   = glm::vec3{body_transform * glm::vec4{record.drag->get_pivot_in_body(), 1.0f}};
        const float     spring_mm        = 1000.0f * glm::distance(drag_point, pivot_in_world);
        const glm::vec3 linear_velocity  = body->get_linear_velocity();
        const glm::vec3 angular_velocity = body->get_angular_velocity();
        record.max_spring_mm = std::max(record.max_spring_mm, spring_mm);

        for (const Joint_measurement& measurement : m_measurements) {
            for (Joint_max& joint_max : record.joint_max) {
                if (joint_max.joint != measurement.joint) {
                    continue;
                }
                joint_max.separation_mm = std::max(joint_max.separation_mm, measurement.separation_mm);
                joint_max.angle_deg     = std::max(joint_max.angle_deg,     measurement.angle_deg);
                joint_max.violation_mm  = std::max(joint_max.violation_mm,  measurement.violation_mm);
                joint_max.violation_deg = std::max(joint_max.violation_deg, measurement.violation_deg);
                break;
            }
        }

        if (worst != nullptr) {
            record.last_worst     = *worst;
            record.has_last_worst = true;
            log_physics_drag->info(
                "drag [{}] frame {} steps {} dt {:.5f}: '{}' drag point {} pivot {} spring {:.2f} mm body {} v {} |v| {:.3f} w {} |w| {:.3f} | worst joint '{}' sep {:.3f} mm (viol {:.3f}) rot {:.2f} deg (viol {:.3f}) | {} joints over 1 mm / 1 deg",
                record.tool_name, record.frame_count, m_frame_fixed_steps, m_last_fixed_step_dt,
                record.node_name, drag_point, pivot_in_world, spring_mm,
                glm::vec3{body_transform[3]}, linear_velocity, glm::length(linear_velocity),
                angular_velocity, glm::length(angular_velocity),
                joint_name_of(*worst->joint), worst->separation_mm, worst->violation_mm, worst->angle_deg, worst->violation_deg,
                over_threshold_count
            );
        } else {
            log_physics_drag->info(
                "drag [{}] frame {} steps {} dt {:.5f}: '{}' drag point {} pivot {} spring {:.2f} mm body {} v {} |v| {:.3f} w {} |w| {:.3f} | no live joints",
                record.tool_name, record.frame_count, m_frame_fixed_steps, m_last_fixed_step_dt,
                record.node_name, drag_point, pivot_in_world, spring_mm,
                glm::vec3{body_transform[3]}, linear_velocity, glm::length(linear_velocity),
                angular_velocity, glm::length(angular_velocity)
            );
        }
    }

    for (const Joint_measurement& measurement : m_measurements) {
        if ((measurement.violation_mm <= c_detail_violation_mm) && (measurement.violation_deg <= c_detail_violation_deg)) {
            continue;
        }
        const Node_joint_constraint_state* const state      = measurement.joint->get_constraint_state();
        const erhe::physics::IConstraint* const  constraint = measurement.joint->get_constraint();
        const erhe::physics::Constraint_diagnostics diagnostics = (constraint != nullptr) ? constraint->get_diagnostics() : erhe::physics::Constraint_diagnostics{};
        log_physics_drag->info(
            "  joint '{}' (A '{}', B '{}'): offset in A {} mm, rotation vector in A {} deg; sep {:.3f} mm (viol {:.3f}) rot {:.2f} deg (viol {:.3f}); {} load{} {} / {} [{}]",
            joint_name_of(*measurement.joint),
            node_name_of(state->node_physics_a), node_name_of(state->node_physics_b),
            measurement.offset_in_a_mm, measurement.rotation_in_a_deg,
            measurement.separation_mm, measurement.violation_mm, measurement.angle_deg, measurement.violation_deg,
            diagnostics.kind, diagnostics.has_load ? "" : " (n/a)",
            diagnostics.linear_load, diagnostics.angular_load, diagnostics.load_unit
        );
    }

    m_frame_fixed_steps = 0;
}

}
