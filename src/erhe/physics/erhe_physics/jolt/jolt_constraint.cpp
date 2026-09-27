#include "erhe_physics/jolt/jolt_constraint.hpp"
#include "erhe_physics/joint_limits.hpp"
#include "erhe_physics/physics_log.hpp"

#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Constraints/MotorSettings.h>
#include <Jolt/Physics/Constraints/SpringSettings.h>

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace erhe::physics {

namespace {

[[nodiscard]] auto same_limit(const Constraint_axis_limit& lhs, const Constraint_axis_limit& rhs) -> bool
{
    if (lhs.limited != rhs.limited) {
        return false;
    }
    return !lhs.limited || ((lhs.min == rhs.min) && (lhs.max == rhs.max));
}

// A translation axis as JPH::SixDOFConstraint enforces it, after the fold of
// fixed values into frame A: an inverted range (min > max) is a fixed axis,
// fixed at zero.
[[nodiscard]] auto enforced_translation(const Constraint_axis_limit& limit) -> Constraint_axis_limit
{
    if (!limit.limited) {
        return Constraint_axis_limit{};
    }
    if (limit.min > limit.max) {
        return Constraint_axis_limit{.limited = true, .min = 0.0f, .max = 0.0f};
    }
    return Constraint_axis_limit{.limited = true, .min = limit.min, .max = limit.max};
}

// A drive spring as Jolt takes it. Jolt's mass normalized mode is the
// KHR_physics_rigid_bodies acceleration mode.
[[nodiscard]] auto to_jolt_spring(const Constraint_axis_drive& drive) -> JPH::SpringSettings
{
    return JPH::SpringSettings{
        (drive.mode == Drive_force_mode::acceleration)
            ? JPH::ESpringMode::MassNormalizedStiffnessAndDamping
            : JPH::ESpringMode::StiffnessAndDamping,
        drive.stiffness,
        drive.damping
    };
}

// A rotation axis as JPH::SixDOFConstraint and SwingTwistConstraintPart
// enforce it: the range is clamped to [-pi, pi], an inverted or empty range
// (a fixed axis, at zero: the fold moved any authored value into frame A)
// becomes [0, 0], a range inside +-0.5 degrees is locked at zero and a range
// wider than +-179.5 degrees is free.
[[nodiscard]] auto enforced_rotation(const Constraint_axis_limit& limit) -> Constraint_axis_limit
{
    if (!limit.limited) {
        return Constraint_axis_limit{};
    }
    constexpr float pi            = glm::pi<float>();
    constexpr float locked_angle  = 0.5f * pi / 180.0f;
    constexpr float free_angle    = 179.5f * pi / 180.0f;
    float min = std::clamp(limit.min, -pi, pi);
    float max = std::clamp(limit.max, -pi, pi);
    if (min >= max) {
        min = 0.0f;
        max = 0.0f;
    }
    if ((min > -locked_angle) && (max < locked_angle)) {
        return Constraint_axis_limit{.limited = true, .min = 0.0f, .max = 0.0f};
    }
    if ((min < -free_angle) && (max > free_angle)) {
        return Constraint_axis_limit{};
    }
    return Constraint_axis_limit{.limited = true, .min = min, .max = max};
}

} // anonymous namespace

auto get_enforced_joint_limits(const std::array<Constraint_axis_limit, 6>& authored_limits) -> Joint_limit_shape
{
    // What the constraint does: fold the fixed values into frame A, then
    // apply Jolt's own clamps to the folded limits.
    Transform                            folded_frame{};
    std::array<Constraint_axis_limit, 6> limits = authored_limits;
    const Fixed_axis_fold                fold   = fold_fixed_axis_values(folded_frame, limits);

    Joint_limit_shape shape{};
    shape.twist_axis  = 0;
    shape.swing_model = Swing_limit_model::pyramid;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        shape.translation[axis] = enforced_translation(limits[axis]);
    }
    shape.twist    = enforced_rotation(limits[3]);
    shape.swing[0] = enforced_rotation(limits[4]);
    shape.swing[1] = enforced_rotation(limits[5]);
    shape.is_exact =
        fold.exact &&
        same_limit(shape.translation[0], limits[0]) &&
        same_limit(shape.translation[1], limits[1]) &&
        same_limit(shape.translation[2], limits[2]) &&
        same_limit(shape.twist,          limits[3]) &&
        same_limit(shape.swing[0],       limits[4]) &&
        same_limit(shape.swing[1],       limits[5]);
    restore_folded_fixed_values(shape, authored_limits, fold);
    return shape;
}

void Jolt_constraint::prepare_step(const float)
{
}

auto IConstraint::create_point_to_point_constraint(const Point_to_point_constraint_settings& settings) -> IConstraint*
{
    return new Jolt_point_to_point_constraint(settings);
}

auto IConstraint::create_point_to_point_constraint_shared(
    const Point_to_point_constraint_settings& settings
) -> std::shared_ptr<IConstraint>
{
    return std::make_shared<Jolt_point_to_point_constraint>(settings);
}
auto IConstraint::create_point_to_point_constraint_unique(
    const Point_to_point_constraint_settings& settings
) -> std::unique_ptr<IConstraint>
{
    return std::make_unique<Jolt_point_to_point_constraint>(settings);
}

auto IConstraint::create_six_dof_constraint(const Six_dof_constraint_settings& settings) -> IConstraint*
{
    return new Jolt_six_dof_constraint(settings);
}

auto IConstraint::create_six_dof_constraint_shared(
    const Six_dof_constraint_settings& settings
) -> std::shared_ptr<IConstraint>
{
    return std::make_shared<Jolt_six_dof_constraint>(settings);
}

auto IConstraint::create_six_dof_constraint_unique(
    const Six_dof_constraint_settings& settings
) -> std::unique_ptr<IConstraint>
{
    return std::make_unique<Jolt_six_dof_constraint>(settings);
}

Jolt_point_to_point_constraint::Jolt_point_to_point_constraint(
    const Point_to_point_constraint_settings& settings
)
{
    auto* const body_a = reinterpret_cast<Jolt_rigid_body*>(settings.rigid_body_a)->get_jolt_body();
    auto* const body_b = reinterpret_cast<Jolt_rigid_body*>(settings.rigid_body_b)->get_jolt_body();

    // Jolt runs the largest override of any constraint in the island.
    const unsigned int velocity_steps = std::min(settings.solver_velocity_iterations, 255u);
    const unsigned int position_steps = std::min(settings.solver_position_iterations, 255u);

    if (settings.frequency <= 0.0f) {
        // Rigid: zero-length distance constraint.
        JPH::DistanceConstraintSettings jolt_settings{};
        jolt_settings.mSpace                    = JPH::EConstraintSpace::LocalToBodyCOM;
        jolt_settings.mPoint1                   = to_jolt(settings.pivot_in_a);
        jolt_settings.mPoint2                   = to_jolt(settings.pivot_in_b);
        jolt_settings.mMinDistance              = 0.0f;
        jolt_settings.mMaxDistance              = 0.0f;
        jolt_settings.mNumVelocityStepsOverride = velocity_steps;
        jolt_settings.mNumPositionStepsOverride = position_steps;
        m_constraint = jolt_settings.Create(*body_a, *body_b);
        return;
    }

    // Spring: every axis free, the three translation axes driven by position
    // motors toward coincident pivots. The motor spring is the pull, and its
    // force limits bound it (per axis).
    JPH::SixDOFConstraintSettings jolt_settings{};
    jolt_settings.mSpace                    = JPH::EConstraintSpace::LocalToBodyCOM;
    jolt_settings.mPosition1                = to_jolt(settings.pivot_in_a);
    jolt_settings.mPosition2                = to_jolt(settings.pivot_in_b);
    jolt_settings.mNumVelocityStepsOverride = velocity_steps;
    jolt_settings.mNumPositionStepsOverride = position_steps;
    for (std::size_t axis_index = 0; axis_index < 3; ++axis_index) {
        JPH::MotorSettings& motor_settings = jolt_settings.mMotorSettings[axis_index];
        motor_settings.mSpringSettings = JPH::SpringSettings{
            JPH::ESpringMode::FrequencyAndDamping,
            settings.frequency,
            settings.damping
        };
        if (std::isfinite(settings.max_force)) {
            motor_settings.SetForceLimits(-settings.max_force, settings.max_force);
        }
    }
    JPH::SixDOFConstraint* const constraint = static_cast<JPH::SixDOFConstraint*>(jolt_settings.Create(*body_a, *body_b));
    constraint->SetMotorState(JPH::SixDOFConstraintSettings::EAxis::TranslationX, JPH::EMotorState::Position);
    constraint->SetMotorState(JPH::SixDOFConstraintSettings::EAxis::TranslationY, JPH::EMotorState::Position);
    constraint->SetMotorState(JPH::SixDOFConstraintSettings::EAxis::TranslationZ, JPH::EMotorState::Position);
    m_constraint = constraint;
}

Jolt_point_to_point_constraint::~Jolt_point_to_point_constraint() noexcept = default;

auto Jolt_point_to_point_constraint::get_jolt_constraint() const -> JPH::Constraint*
{
    return m_constraint.GetPtr();
}

namespace {

[[nodiscard]] auto to_jolt_axis(const std::size_t axis_index) -> JPH::SixDOFConstraintSettings::EAxis
{
    // Six_dof_constraint_settings axis order (translation XYZ then rotation XYZ)
    // matches JPH::SixDOFConstraintSettings::EAxis order by design.
    static_assert(JPH::SixDOFConstraintSettings::EAxis::TranslationX == 0);
    static_assert(JPH::SixDOFConstraintSettings::EAxis::TranslationY == 1);
    static_assert(JPH::SixDOFConstraintSettings::EAxis::TranslationZ == 2);
    static_assert(JPH::SixDOFConstraintSettings::EAxis::RotationX    == 3);
    static_assert(JPH::SixDOFConstraintSettings::EAxis::RotationY    == 4);
    static_assert(JPH::SixDOFConstraintSettings::EAxis::RotationZ    == 5);
    return static_cast<JPH::SixDOFConstraintSettings::EAxis>(axis_index);
}

[[nodiscard]] auto inverse_mass(const JPH::Body* body) -> float
{
    return body->IsDynamic() ? body->GetMotionProperties()->GetInverseMass() : 0.0f;
}

// The effective mass of one degree of freedom of the joint, the mass a drive
// on that axis acts on: along a translation axis the reduced mass of the
// bodies, about a rotation axis 1 / (a . (I_A^-1 + I_B^-1) . a) with the axis
// in world space and the world inverse inertia tensors (zero for a body that
// is not dynamic). Zero when neither body can move.
[[nodiscard]] auto effective_mass(const JPH::Body* body_a, const JPH::Body* body_b, const JPH::Vec3 world_axis, const bool is_translation) -> float
{
    if (is_translation) {
        const float k = inverse_mass(body_a) + inverse_mass(body_b);
        return (k > 0.0f) ? (1.0f / k) : 0.0f;
    }
    JPH::Mat44 inverse_inertia = JPH::Mat44::sZero();
    if (body_a->IsDynamic()) {
        inverse_inertia = inverse_inertia + body_a->GetInverseInertia();
    }
    if (body_b->IsDynamic()) {
        inverse_inertia = inverse_inertia + body_b->GetInverseInertia();
    }
    const float k = world_axis.Dot(inverse_inertia.Multiply3x3(world_axis));
    return (k > 0.0f) ? (1.0f / k) : 0.0f;
}

} // anonymous namespace

void Jolt_six_dof_constraint::prepare_step(const float)
{
    bool any_active = false;
    for (const Velocity_drive& drive : m_velocity_drives) {
        any_active = any_active || drive.active;
    }
    if (!any_active || (m_constraint == nullptr)) {
        return;
    }

    // The linear motor targets are in body 1's constraint space and the
    // angular ones in body 2's (JPH::SixDOFConstraint::SetTargetVelocityCS /
    // SetTargetAngularVelocityCS), so the current relative velocities are
    // measured the same way: the relative velocity of the anchor point on
    // body 2, and the relative angular velocity.
    const JPH::RMat44 com_a          = m_body_a->GetCenterOfMassTransform();
    const JPH::RMat44 com_b          = m_body_b->GetCenterOfMassTransform();
    const JPH::Mat44  world_from_cs1 = JPH::Mat44::sRotation(m_body_a->GetRotation()) * m_constraint->GetConstraintToBody1Matrix();
    const JPH::Mat44  world_from_cs2 = JPH::Mat44::sRotation(m_body_b->GetRotation()) * m_constraint->GetConstraintToBody2Matrix();
    const JPH::RVec3  anchor         = com_b * m_constraint->GetConstraintToBody2Matrix().GetTranslation();
    const JPH::Vec3   r_a            = JPH::Vec3{anchor - com_a.GetTranslation()};
    const JPH::Vec3   r_b            = JPH::Vec3{anchor - com_b.GetTranslation()};
    const JPH::Vec3   v_a            = m_body_a->GetLinearVelocity() + m_body_a->GetAngularVelocity().Cross(r_a);
    const JPH::Vec3   v_b            = m_body_b->GetLinearVelocity() + m_body_b->GetAngularVelocity().Cross(r_b);
    const JPH::Vec3   v_cs           = world_from_cs1.Multiply3x3Transposed(v_b - v_a);
    const JPH::Vec3   w_cs           = world_from_cs2.Multiply3x3Transposed(m_body_b->GetAngularVelocity() - m_body_a->GetAngularVelocity());

    for (std::size_t axis_index = 0; axis_index < 6; ++axis_index) {
        const Velocity_drive& drive = m_velocity_drives[axis_index];
        if (!drive.active) {
            continue;
        }
        const bool  is_translation = axis_index < 3;
        const float velocity       = is_translation ? v_cs[static_cast<JPH::uint>(axis_index)] : w_cs[static_cast<JPH::uint>(axis_index - 3)];
        const float error          = std::abs(drive.target - velocity);
        const float cap            = std::isfinite(drive.gain) ? std::min(drive.max_force, drive.gain * error) : drive.max_force;
        if (!std::isfinite(cap)) {
            continue; // unbounded: the motor keeps its default limits
        }
        JPH::MotorSettings& motor_settings = m_constraint->GetMotorSettings(to_jolt_axis(axis_index));
        if (is_translation) {
            motor_settings.SetForceLimits(-cap, cap);
        } else {
            motor_settings.SetTorqueLimits(-cap, cap);
        }
    }
}

Jolt_six_dof_constraint::Jolt_six_dof_constraint(const Six_dof_constraint_settings& authored_settings)
{
    // Jolt fixes an axis at zero only: a fixed axis authored at another value
    // is folded into frame A first (joint_limits.hpp).
    Six_dof_constraint_settings settings = authored_settings;
    const Fixed_axis_fold fold = fold_fixed_axis_values(settings.frame_in_a, settings.limits);
    if (!fold.exact) {
        log_physics->warn(
            "Six-dof constraint: a fixed rotation at a non-zero angle cannot be folded into the joint frame "
            "while a translation axis is not fixed; it is fixed at 0"
        );
    }

    // Frame space convention:
    //
    // erhe::physics body transforms (IRigid_body get/set_world_transform) are node
    // space transforms: the Jolt body origin, not the center of mass.
    // Jolt_rigid_body::set_world_transform() passes the node space origin to
    // JPH::BodyInterface::SetPositionAndRotation(), which internally offsets it by
    // Shape::GetCenterOfMass() to obtain the COM position that JPH::Body stores.
    //
    // Six_dof_constraint_settings::frame_in_a / frame_in_b are given in the same
    // node space. JPH::EConstraintSpace::LocalToBodyCOM requires positions local to
    // each body's center of mass ("you need to subtract Shape::GetCenterOfMass()
    // from positions", see EConstraintSpace), so the shape COM offset is subtracted
    // here. The frame basis is unaffected: the COM transform rotation equals the
    // node transform rotation.
    //
    // Concrete example: a sphere shape wrapped in OffsetCenterOfMassShape with
    // offset c, body node transform (R, t). Shape::GetCenterOfMass() == c and the
    // body COM world position is t + R * c. A joint frame origin p in node space
    // sits at world position t + R * p; the solver computes the world anchor as
    // COM_transform * (p - c) = (t + R * c) + R * (p - c) = t + R * p. Consistent.
    //
    // A null rigid body constrains to the world via JPH::Body::sFixedToWorld,
    // which sits at the world origin with identity rotation and an EmptyShape
    // whose center of mass is zero - so for that body, COM local space IS world
    // space and the frame is interpreted as a world space frame unchanged.

    JPH::Body* const body_a = (settings.rigid_body_a != nullptr)
        ? reinterpret_cast<Jolt_rigid_body*>(settings.rigid_body_a)->get_jolt_body()
        : &JPH::Body::sFixedToWorld;
    JPH::Body* const body_b = (settings.rigid_body_b != nullptr)
        ? reinterpret_cast<Jolt_rigid_body*>(settings.rigid_body_b)->get_jolt_body()
        : &JPH::Body::sFixedToWorld;

    // Never null: bodies always have a shape, and sFixedToWorld has an EmptyShape
    // with zero center of mass.
    const JPH::Vec3 com_offset_a = body_a->GetShape()->GetCenterOfMass();
    const JPH::Vec3 com_offset_b = body_b->GetShape()->GetCenterOfMass();
    m_body_a = body_a;
    m_body_b = body_b;

    JPH::SixDOFConstraintSettings jolt_settings{};
    jolt_settings.mSpace     = JPH::EConstraintSpace::LocalToBodyCOM;
    // Pyramid supports independent, asymmetric rotation Y / Z limits; the default
    // Cone type would force the Y / Z limits to be symmetric around zero.
    jolt_settings.mSwingType = JPH::ESwingType::Pyramid;
    jolt_settings.mPosition1 = to_jolt(settings.frame_in_a.origin) - com_offset_a;
    jolt_settings.mAxisX1    = to_jolt(glm::normalize(settings.frame_in_a.basis[0]));
    jolt_settings.mAxisY1    = to_jolt(glm::normalize(settings.frame_in_a.basis[1]));
    jolt_settings.mPosition2 = to_jolt(settings.frame_in_b.origin) - com_offset_b;
    jolt_settings.mAxisX2    = to_jolt(glm::normalize(settings.frame_in_b.basis[0]));
    jolt_settings.mAxisY2    = to_jolt(glm::normalize(settings.frame_in_b.basis[1]));

    for (std::size_t axis_index = 0; axis_index < 6; ++axis_index) {
        const Constraint_axis_limit& limit          = settings.limits[axis_index];
        const auto                   jolt_axis      = to_jolt_axis(axis_index);
        const bool                   is_translation = axis_index < 3;

        if (!limit.limited) {
            jolt_settings.MakeFreeAxis(jolt_axis);
            continue;
        }
        if (limit.min > limit.max) {
            // Jolt sanitizes an inverted range to a fixed axis at value 0; make the
            // intent explicit and warn instead of passing the inverted range through.
            log_physics->warn(
                "Six-dof constraint axis {}: inverted limit range [{}, {}] treated as fixed",
                axis_index, limit.min, limit.max
            );
            jolt_settings.MakeFixedAxis(jolt_axis);
        } else {
            jolt_settings.SetLimitedAxis(jolt_axis, limit.min, limit.max); // min == max -> fixed axis
        }
        if (limit.stiffness.has_value()) {
            if (is_translation) {
                jolt_settings.mLimitsSpringSettings[axis_index] = JPH::SpringSettings{
                    JPH::ESpringMode::StiffnessAndDamping,
                    limit.stiffness.value(),
                    limit.damping
                };
            } else {
                // Jolt SixDOFConstraint supports soft limit springs on translation axes only.
                log_physics->warn(
                    "Six-dof constraint axis {}: angular soft limit (stiffness {}) is not supported by Jolt; using hard limit",
                    axis_index, limit.stiffness.value()
                );
            }
        }
    }

    for (std::size_t axis_index = 0; axis_index < 6; ++axis_index) {
        const Constraint_axis_drive& drive = settings.drives[axis_index];
        if (!drive.enabled) {
            continue;
        }
        const bool          is_translation = axis_index < 3;
        JPH::MotorSettings& motor_settings = jolt_settings.mMotorSettings[axis_index];
        // The spring is used only by position motors; setting it is harmless for
        // velocity motors.
        motor_settings.mSpringSettings = to_jolt_spring(drive);
        if (std::isfinite(drive.max_force)) {
            if (is_translation) {
                motor_settings.SetForceLimits(-drive.max_force, drive.max_force);
            } else {
                motor_settings.SetTorqueLimits(-drive.max_force, drive.max_force);
            }
        }
    }

    m_constraint = static_cast<JPH::SixDOFConstraint*>(jolt_settings.Create(*body_a, *body_b));

    // Motor states and targets are runtime properties of the created constraint.
    // Jolt keeps one shared target vector per kind; per-axis targets are composed
    // into these vectors and only the components of axes whose motor is in the
    // matching state are used by the solver.
    JPH::Vec3 target_velocity            = JPH::Vec3::sZero(); // body 1 constraint space
    JPH::Vec3 target_angular_velocity    = JPH::Vec3::sZero(); // body 2 constraint space
    JPH::Vec3 target_position            = JPH::Vec3::sZero(); // body 1 constraint space
    JPH::Vec3 target_angles              = JPH::Vec3::sZero();
    bool      has_angular_position_target = false;
    for (std::size_t axis_index = 0; axis_index < 6; ++axis_index) {
        const Constraint_axis_drive& drive = settings.drives[axis_index];
        if (!drive.enabled) {
            continue;
        }
        const auto jolt_axis      = to_jolt_axis(axis_index);
        const bool is_translation = axis_index < 3;
        const auto component      = static_cast<JPH::uint>(is_translation ? axis_index : (axis_index - 3));
        if (drive.use_position_target) {
            m_constraint->SetMotorState(jolt_axis, JPH::EMotorState::Position);
            if (is_translation) {
                target_position.SetComponent(component, drive.position_target);
            } else {
                target_angles.SetComponent(component, drive.position_target);
                has_angular_position_target = true;
            }
            if (drive.velocity_target != 0.0f) {
                log_physics->warn(
                    "Six-dof constraint axis {}: position drive velocity target {} dropped; a position motor damps toward rest",
                    axis_index, drive.velocity_target
                );
            }
        } else {
            m_constraint->SetMotorState(jolt_axis, JPH::EMotorState::Velocity);
            if (is_translation) {
                target_velocity.SetComponent(component, drive.velocity_target);
            } else {
                target_angular_velocity.SetComponent(component, drive.velocity_target);
            }
            // The motor's force bound follows the velocity error from now on
            // (prepare_step), so the drive's damping is its gain.
            const JPH::Vec3 world_axis = JPH::Mat44::sRotation(body_a->GetRotation()).Multiply3x3(
                to_jolt(glm::normalize(settings.frame_in_a.basis[static_cast<int>(component)]))
            );
            Velocity_drive& velocity_drive = m_velocity_drives[axis_index];
            velocity_drive.active    = true;
            velocity_drive.gain      = velocity_drive_gain(drive, effective_mass(body_a, body_b, world_axis, is_translation));
            velocity_drive.max_force = drive.max_force;
            velocity_drive.target    = drive.velocity_target;
        }
    }
    m_constraint->SetTargetVelocityCS(target_velocity);
    m_constraint->SetTargetAngularVelocityCS(target_angular_velocity);
    m_constraint->SetTargetPositionCS(target_position);
    if (has_angular_position_target) {
        // Jolt has a single quaternion target for angular position motors; compose
        // it from the per-axis target angles. JPH::Quat::sEulerAngles applies the
        // rotations in X, then Y, then Z order (RotZ * RotY * RotX). This is exact
        // when a single rotation axis is position-driven; with multiple driven
        // rotation axes the targets combine in that order.
        m_constraint->SetTargetOrientationCS(JPH::Quat::sEulerAngles(target_angles));
    }
}

Jolt_six_dof_constraint::~Jolt_six_dof_constraint() noexcept
{
    // m_constraint is a JPH::Ref; releasing it frees the constraint once the
    // physics system (IWorld::remove_constraint) has also released its reference.
}

auto Jolt_six_dof_constraint::get_jolt_constraint() const -> JPH::Constraint*
{
    return m_constraint.GetPtr();
}

} // namespace erhe::physics
