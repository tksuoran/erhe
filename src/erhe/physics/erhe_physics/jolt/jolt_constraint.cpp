#include "erhe_physics/jolt/jolt_constraint.hpp"
#include "erhe_physics/physics_log.hpp"

#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Constraints/MotorSettings.h>
#include <Jolt/Physics/Constraints/SpringSettings.h>

#include <cmath>
#include <cstddef>

namespace erhe::physics {

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
    m_settings.mSpace       = JPH::EConstraintSpace::LocalToBodyCOM;
    m_settings.mPoint1      = to_jolt(settings.pivot_in_a);
    m_settings.mPoint2      = to_jolt(settings.pivot_in_b);
    m_settings.mMinDistance = 0.0f;
    m_settings.mMaxDistance = 0.0f;
    auto* const body_a = reinterpret_cast<Jolt_rigid_body*>(settings.rigid_body_a)->get_jolt_body();
    auto* const body_b = reinterpret_cast<Jolt_rigid_body*>(settings.rigid_body_b)->get_jolt_body();
    m_constraint = m_settings.Create(
        *body_a,
        *body_b
    );
}

Jolt_point_to_point_constraint::~Jolt_point_to_point_constraint() noexcept
{
    // TODO destroy
}

auto Jolt_point_to_point_constraint::get_jolt_constraint() const -> JPH::Constraint*
{
    return m_constraint;
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

} // anonymous namespace

Jolt_six_dof_constraint::Jolt_six_dof_constraint(const Six_dof_constraint_settings& settings)
{
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
        motor_settings.mSpringSettings = JPH::SpringSettings{
            JPH::ESpringMode::StiffnessAndDamping,
            drive.stiffness,
            drive.damping
        };
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
        } else {
            m_constraint->SetMotorState(jolt_axis, JPH::EMotorState::Velocity);
            if (is_translation) {
                target_velocity.SetComponent(component, drive.velocity_target);
            } else {
                target_angular_velocity.SetComponent(component, drive.velocity_target);
            }
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
