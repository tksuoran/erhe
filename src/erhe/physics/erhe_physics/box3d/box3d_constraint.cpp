#include "erhe_physics/box3d/box3d_constraint.hpp"
#include "erhe_physics/box3d/box3d_rigid_body.hpp"
#include "erhe_physics/box3d/box3d_six_dof_classifier.hpp"
#include "erhe_physics/box3d/box3d_world.hpp"
#include "erhe_physics/box3d/glm_conversions.hpp"
#include "erhe_physics/physics_log.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace erhe::physics {

Box3d_constraint::~Box3d_constraint() noexcept
{
    if (m_is_valid) {
        b3DestroyJoint(m_joint, true);
        m_is_valid = false;
    }
}

auto Box3d_constraint::get_diagnostics() const -> Constraint_diagnostics
{
    Constraint_diagnostics result{};
    if (!m_is_valid || !b3Joint_IsValid(m_joint)) {
        return result;
    }
    switch (b3Joint_GetType(m_joint)) {
        case b3_parallelJoint:  result.kind = "box3d parallel";  break;
        case b3_distanceJoint:  result.kind = "box3d distance";  break;
        case b3_filterJoint:    result.kind = "box3d filter";    break;
        case b3_motorJoint:     result.kind = "box3d motor";     break;
        case b3_prismaticJoint: result.kind = "box3d prismatic"; break;
        case b3_revoluteJoint:  result.kind = "box3d revolute";  break;
        case b3_sphericalJoint: result.kind = "box3d spherical"; break;
        case b3_weldJoint:      result.kind = "box3d weld";      break;
        case b3_wheelJoint:     result.kind = "box3d wheel";     break;
        default:                result.kind = "box3d ?";         break;
    }
    const b3Vec3 force  = b3Joint_GetConstraintForce (m_joint);
    const b3Vec3 torque = b3Joint_GetConstraintTorque(m_joint);
    result.has_load     = true;
    result.load_unit    = "force N / torque N m";
    result.linear_load  = glm::vec3{force.x,  force.y,  force.z};
    result.angular_load = glm::vec3{torque.x, torque.y, torque.z};
    return result;
}

namespace {

// Box3D rejects a non-finite drive force (box3d/types.h), while erhe uses
// infinity to mean "unbounded".
constexpr float unbounded_drive_force = 1.0e6f;

// Joint constraint softness. Box3D's soft constraints are stiff relative to the
// effective mass at the joint, which is tiny for a body hanging far from a
// joint compared to its own size (a pendulum bob: 2 kg, 7.5 cm radius, 0.64 m
// arm has ~0.01 kg effective mass across the swing plane). At Box3D's default
// 60 Hz such a joint gave way ~2 mm per newton pushing out of its plane.
// Box3D clamps constraintHertz to a quarter of the substep rate (joint.c
// b3PrepareJoint), so asking for more than any step allows gets the stiffest
// joint the step supports - 16x stiffer than the default at erhe's 240 Hz x 4
// substeps.
constexpr float stiffest_joint_hertz = 1.0e6f;

// Box3D clamps revolute and twist ranges just inside +/- pi.
constexpr float max_joint_angle = 0.99f * glm::pi<float>();

[[nodiscard]] auto finite_force(const float max_force) -> float
{
    return std::isfinite(max_force) ? max_force : unbounded_drive_force;
}

[[nodiscard]] auto clamp_angle(const float radians) -> float
{
    return (radians < -max_joint_angle) ? -max_joint_angle : ((radians > max_joint_angle) ? max_joint_angle : radians);
}

// The common b3JointDef fields, applied ONTO an already default-initialized
// def rather than replacing it. b3JointDef carries a B3_SECRET_COOKIE in
// internalValue that b3Default*JointDef() sets and Box3D validates at joint
// creation, so assigning a locally constructed b3JointDef over joint_def.base
// wipes the cookie and aborts the process.
class Joint_base_values
{
public:
    b3BodyId    body_a{};
    b3BodyId    body_b{};
    b3Transform local_frame_a{};
    b3Transform local_frame_b{};

    void apply_to(b3JointDef& base) const
    {
        base.bodyIdA          = body_a;
        base.bodyIdB          = body_b;
        base.localFrameA      = local_frame_a;
        base.localFrameB      = local_frame_b;
        base.collideConnected = false;
        base.constraintHertz  = stiffest_joint_hertz;
    }
};

class Joint_bodies
{
public:
    Box3d_world* world {nullptr};
    b3BodyId     body_a{};
    b3BodyId     body_b{};
    float        mass_a{0.0f};
    float        mass_b{0.0f};
    bool         valid {false};
};

// Box3D joints need two valid bodies, so rigid_body_b == nullptr ("constrain to
// world") resolves to the world's anchor body.
[[nodiscard]] auto resolve_bodies(IRigid_body* rigid_body_a, IRigid_body* rigid_body_b, const char* what) -> Joint_bodies
{
    Joint_bodies result{};
    Box3d_rigid_body* body_a = static_cast<Box3d_rigid_body*>(rigid_body_a);
    if ((body_a == nullptr) || !body_a->is_valid()) {
        log_physics->error("box3d {}: body A is missing or invalid", what);
        return result;
    }
    result.world  = &body_a->get_world();
    result.body_a = body_a->get_box3d_body();
    result.mass_a = b3Body_GetMass(result.body_a);

    Box3d_rigid_body* body_b = static_cast<Box3d_rigid_body*>(rigid_body_b);
    if (body_b == nullptr) {
        result.body_b = result.world->get_or_create_world_anchor_body();
        result.mass_b = 0.0f;
    } else {
        if (!body_b->is_valid()) {
            log_physics->error("box3d {}: body B is invalid", what);
            return result;
        }
        result.body_b = body_b->get_box3d_body();
        result.mass_b = b3Body_GetMass(result.body_b);
    }
    result.valid = true;
    return result;
}

} // anonymous namespace

// -----------------------------------------------------------------------------
// Point to point (the interactive grab / drag tool)
// -----------------------------------------------------------------------------

class Box3d_point_to_point_constraint : public Box3d_constraint
{
public:
    explicit Box3d_point_to_point_constraint(const Point_to_point_constraint_settings& settings)
    {
        const Joint_bodies bodies = resolve_bodies(settings.rigid_body_a, settings.rigid_body_b, "point-to-point constraint");
        if (!bodies.valid) {
            return;
        }

        // b3JointDef frames are relative to the body origin, not the center of
        // mass, which is the same convention as erhe's pivots.
        const b3Transform frame_a{to_box3d(settings.pivot_in_a), b3Quat_identity};
        const b3Transform frame_b{to_box3d(settings.pivot_in_b), b3Quat_identity};

        if (settings.frequency > 0.0f) {
            // A spring: the motor joint's linear spring pulls frame B onto
            // frame A with a bounded force; no velocity motor, no angular
            // spring, so rotation stays free.
            b3MotorJointDef joint_def = b3DefaultMotorJointDef();
            joint_def.base.bodyIdA          = bodies.body_a;
            joint_def.base.bodyIdB          = bodies.body_b;
            joint_def.base.localFrameA      = frame_a;
            joint_def.base.localFrameB      = frame_b;
            joint_def.base.collideConnected = true;
            joint_def.linearHertz           = settings.frequency;
            joint_def.linearDampingRatio    = settings.damping;
            joint_def.maxSpringForce        = finite_force(settings.max_force);

            m_joint    = b3CreateMotorJoint(bodies.world->get_box3d_world(), &joint_def);
            m_is_valid = true;
            return;
        }

        // A spherical joint with no limits, no motor and no spring IS a
        // point-to-point constraint. The softness comes from the joint's
        // positional constraint tuning, NOT from b3SphericalJointDef's spring,
        // which aligns the two frames' rotations instead.
        b3SphericalJointDef joint_def = b3DefaultSphericalJointDef();
        joint_def.base.bodyIdA = bodies.body_a;
        joint_def.base.bodyIdB = bodies.body_b;
        joint_def.base.localFrameA = frame_a;
        joint_def.base.localFrameB = frame_b;
        joint_def.base.collideConnected       = true;
        joint_def.base.constraintHertz        = settings.frequency;
        joint_def.base.constraintDampingRatio = settings.damping;
        joint_def.enableSpring     = false;
        joint_def.enableConeLimit  = false;
        joint_def.enableTwistLimit = false;
        joint_def.enableMotor      = false;

        m_joint    = b3CreateSphericalJoint(bodies.world->get_box3d_world(), &joint_def);
        m_is_valid = true;
    }
};

// -----------------------------------------------------------------------------
// Six degrees of freedom
// -----------------------------------------------------------------------------

class Box3d_six_dof_constraint : public Box3d_constraint
{
public:
    explicit Box3d_six_dof_constraint(const Six_dof_constraint_settings& settings)
    {
        const Joint_bodies bodies = resolve_bodies(settings.rigid_body_a, settings.rigid_body_b, "six-dof constraint");
        if (!bodies.valid) {
            return;
        }

        const Six_dof_classification classification = classify_six_dof(settings.limits);
        if (!classification.is_exact) {
            log_physics->warn(
                "box3d six-dof constraint: axis pattern '{}' is not exactly representable by any Box3D joint; "
                "approximating with a {} joint",
                describe_axis_states(classification.axis_states),
                c_str(classification.kind)
            );
        }

        Joint_base_values base{};
        base.body_a        = bodies.body_a;
        base.body_b        = bodies.body_b;
        base.local_frame_a = to_box3d(settings.frame_in_a);
        base.local_frame_b = to_box3d(settings.frame_in_b);

        const float joint_mass = reduced_mass(bodies.mass_a, bodies.mass_b);
        const b3WorldId world  = bodies.world->get_box3d_world();

        switch (classification.kind) {
            case Six_dof_joint_kind::weld:      create_weld     (world, base, settings, joint_mass); break;
            case Six_dof_joint_kind::revolute:  create_revolute (world, base, settings, classification, joint_mass); break;
            case Six_dof_joint_kind::prismatic: create_prismatic(world, base, settings, classification, joint_mass); break;
            case Six_dof_joint_kind::spherical: create_spherical(world, base, settings, joint_mass); break;
            case Six_dof_joint_kind::filter:    create_filter   (world, base); break;
            default: break;
        }
    }

private:
    // Rotates both joint frames so the erhe axis lands on the axis Box3D's
    // joint type actually uses.
    static void remap_frames(b3JointDef& joint_base, const glm::quat& rotation)
    {
        const b3Quat q = to_box3d(rotation);
        joint_base.localFrameA.q = b3MulQuat(joint_base.localFrameA.q, q);
        joint_base.localFrameB.q = b3MulQuat(joint_base.localFrameB.q, q);
    }

    static void warn_dropped_drives(const Six_dof_constraint_settings& settings, const int kept_axis, const char* kind)
    {
        for (int axis = 0; axis < 6; ++axis) {
            if (settings.drives[static_cast<std::size_t>(axis)].enabled && (axis != kept_axis)) {
                log_physics->warn(
                    "box3d six-dof constraint: drive on axis {} dropped; a {} joint carries only axis {}",
                    axis, kind, kept_axis
                );
            }
        }
    }

    void create_weld(
        const b3WorldId                    world,
        const Joint_base_values&           base,
        const Six_dof_constraint_settings& settings,
        const float                        joint_mass
    )
    {
        b3WeldJointDef joint_def = b3DefaultWeldJointDef();
        base.apply_to(joint_def.base);
        // A weld's only tuning is spring stiffness; 0 hertz means rigid, which
        // is what a hard-limited axis wants.
        float linear_stiffness  = 0.0f;
        float angular_stiffness = 0.0f;
        float linear_damping    = 0.0f;
        float angular_damping   = 0.0f;
        for (std::size_t axis = 0; axis < 6; ++axis) {
            const Constraint_axis_limit& limit = settings.limits[axis];
            if (!limit.stiffness.has_value()) {
                continue;
            }
            if (axis < 3) {
                linear_stiffness = limit.stiffness.value();
                linear_damping   = limit.damping;
            } else {
                angular_stiffness = limit.stiffness.value();
                angular_damping   = limit.damping;
            }
        }
        joint_def.linearHertz         = stiffness_to_hertz(linear_stiffness,  joint_mass);
        joint_def.angularHertz        = stiffness_to_hertz(angular_stiffness, joint_mass);
        joint_def.linearDampingRatio  = linear_damping;
        joint_def.angularDampingRatio = angular_damping;

        m_joint    = b3CreateWeldJoint(world, &joint_def);
        m_is_valid = true;
    }

    void create_revolute(
        const b3WorldId                    world,
        const Joint_base_values&           base,
        const Six_dof_constraint_settings& settings,
        const Six_dof_classification&      classification,
        const float                        joint_mass
    )
    {
        b3RevoluteJointDef joint_def = b3DefaultRevoluteJointDef();
        base.apply_to(joint_def.base);
        // Box3D's revolute joint rotates about its local frame Z.
        remap_frames(joint_def.base, revolute_frame_rotation(classification.axis));

        const std::size_t              axis_index = static_cast<std::size_t>(3 + classification.axis);
        const Constraint_axis_limit&   limit      = settings.limits[axis_index];
        const Constraint_axis_drive&   drive      = settings.drives[axis_index];

        if (classify_axis(limit) == Axis_state::limited) {
            joint_def.enableLimit = true;
            joint_def.lowerAngle  = clamp_angle(limit.min);
            joint_def.upperAngle  = clamp_angle(limit.max);
        }
        if (drive.enabled) {
            if (drive.use_position_target) {
                joint_def.enableSpring = true;
                joint_def.targetAngle  = drive.position_target;
                joint_def.hertz        = stiffness_to_hertz(drive.stiffness, joint_mass);
                joint_def.dampingRatio = drive.damping;
            } else {
                joint_def.enableMotor    = true;
                joint_def.motorSpeed     = drive.velocity_target;
                joint_def.maxMotorTorque = finite_force(drive.max_force);
            }
        }
        warn_dropped_drives(settings, static_cast<int>(axis_index), "revolute");

        m_joint    = b3CreateRevoluteJoint(world, &joint_def);
        m_is_valid = true;
    }

    void create_prismatic(
        const b3WorldId                    world,
        const Joint_base_values&           base,
        const Six_dof_constraint_settings& settings,
        const Six_dof_classification&      classification,
        const float                        joint_mass
    )
    {
        b3PrismaticJointDef joint_def = b3DefaultPrismaticJointDef();
        base.apply_to(joint_def.base);
        // Box3D's prismatic joint slides along its local frame X.
        remap_frames(joint_def.base, prismatic_frame_rotation(classification.axis));

        const std::size_t            axis_index = static_cast<std::size_t>(classification.axis);
        const Constraint_axis_limit& limit      = settings.limits[axis_index];
        const Constraint_axis_drive& drive      = settings.drives[axis_index];

        if (classify_axis(limit) == Axis_state::limited) {
            joint_def.enableLimit      = true;
            joint_def.lowerTranslation = limit.min;
            joint_def.upperTranslation = limit.max;
        }
        if (drive.enabled) {
            if (drive.use_position_target) {
                joint_def.enableSpring     = true;
                joint_def.targetTranslation = drive.position_target;
                joint_def.hertz             = stiffness_to_hertz(drive.stiffness, joint_mass);
                joint_def.dampingRatio      = drive.damping;
            } else {
                joint_def.enableMotor   = true;
                joint_def.motorSpeed    = drive.velocity_target;
                joint_def.maxMotorForce = finite_force(drive.max_force);
            }
        }
        warn_dropped_drives(settings, static_cast<int>(axis_index), "prismatic");

        m_joint    = b3CreatePrismaticJoint(world, &joint_def);
        m_is_valid = true;
    }

    // A spherical joint always exposes all three rotational axes, so it needs
    // the raw limits rather than the classification's single chosen axis.
    void create_spherical(
        const b3WorldId                    world,
        const Joint_base_values&           base,
        const Six_dof_constraint_settings& settings,
        const float                        joint_mass
    )
    {
        b3SphericalJointDef joint_def = b3DefaultSphericalJointDef();
        base.apply_to(joint_def.base);

        // A cone limit constrains two rotational axes together, so the widest
        // limited swing axis sets the cone half-angle; the twist limit takes
        // the frame Z axis.
        float cone_angle    = 0.0f;
        bool  has_cone      = false;
        for (int axis = 0; axis < 2; ++axis) {
            const Constraint_axis_limit& limit = settings.limits[static_cast<std::size_t>(3 + axis)];
            if (classify_axis(limit) != Axis_state::limited) {
                continue;
            }
            const float half_range = 0.5f * (limit.max - limit.min);
            cone_angle = (half_range > cone_angle) ? half_range : cone_angle;
            has_cone   = true;
        }
        if (has_cone) {
            joint_def.enableConeLimit = true;
            joint_def.coneAngle       = (cone_angle > max_joint_angle) ? max_joint_angle : cone_angle;
        }

        const Constraint_axis_limit& twist_limit = settings.limits[5];
        if (classify_axis(twist_limit) == Axis_state::limited) {
            joint_def.enableTwistLimit = true;
            joint_def.lowerTwistAngle  = clamp_angle(twist_limit.min);
            joint_def.upperTwistAngle  = clamp_angle(twist_limit.max);
        }

        // A spherical joint's motor takes an angular velocity vector, so all
        // three rotational drives can be carried at once.
        glm::vec3 motor_velocity{0.0f};
        bool      has_velocity_drive = false;
        float     max_torque         = 0.0f;
        float     spring_stiffness   = 0.0f;
        float     spring_damping     = 0.0f;
        bool      has_position_drive = false;
        for (int axis = 0; axis < 3; ++axis) {
            const Constraint_axis_drive& drive = settings.drives[static_cast<std::size_t>(3 + axis)];
            if (!drive.enabled) {
                continue;
            }
            if (drive.use_position_target) {
                has_position_drive = true;
                spring_stiffness   = drive.stiffness;
                spring_damping     = drive.damping;
            } else {
                has_velocity_drive          = true;
                motor_velocity[axis]        = drive.velocity_target;
                const float drive_max_force = finite_force(drive.max_force);
                max_torque                  = (drive_max_force > max_torque) ? drive_max_force : max_torque;
            }
        }
        if (has_velocity_drive) {
            joint_def.enableMotor    = true;
            joint_def.motorVelocity  = to_box3d(motor_velocity);
            joint_def.maxMotorTorque = max_torque;
        }
        if (has_position_drive) {
            joint_def.enableSpring = true;
            joint_def.hertz        = stiffness_to_hertz(spring_stiffness, joint_mass);
            joint_def.dampingRatio = spring_damping;
        }
        for (int axis = 0; axis < 3; ++axis) {
            if (settings.drives[static_cast<std::size_t>(axis)].enabled) {
                log_physics->warn(
                    "box3d six-dof constraint: translation drive on axis {} dropped; "
                    "a spherical joint has no translational degree of freedom",
                    axis
                );
            }
        }

        m_joint    = b3CreateSphericalJoint(world, &joint_def);
        m_is_valid = true;
    }

    void create_filter(const b3WorldId world, const Joint_base_values& base)
    {
        // Six free axes: nothing to constrain. A filter joint keeps the two
        // bodies associated (and excludes their collision), which is the only
        // remaining effect the joint can have.
        log_physics->info("box3d six-dof constraint: all axes free; created as a collision-exclusion joint only");
        b3FilterJointDef joint_def = b3DefaultFilterJointDef();
        base.apply_to(joint_def.base);
        m_joint    = b3CreateFilterJoint(world, &joint_def);
        m_is_valid = true;
    }
};

// -----------------------------------------------------------------------------
// IConstraint factories
// -----------------------------------------------------------------------------

auto IConstraint::create_point_to_point_constraint(const Point_to_point_constraint_settings& settings) -> IConstraint*
{
    return new Box3d_point_to_point_constraint(settings);
}

auto IConstraint::create_point_to_point_constraint_shared(
    const Point_to_point_constraint_settings& settings
) -> std::shared_ptr<IConstraint>
{
    return std::make_shared<Box3d_point_to_point_constraint>(settings);
}

auto IConstraint::create_point_to_point_constraint_unique(
    const Point_to_point_constraint_settings& settings
) -> std::unique_ptr<IConstraint>
{
    return std::make_unique<Box3d_point_to_point_constraint>(settings);
}

auto IConstraint::create_six_dof_constraint(const Six_dof_constraint_settings& settings) -> IConstraint*
{
    return new Box3d_six_dof_constraint(settings);
}

auto IConstraint::create_six_dof_constraint_shared(
    const Six_dof_constraint_settings& settings
) -> std::shared_ptr<IConstraint>
{
    return std::make_shared<Box3d_six_dof_constraint>(settings);
}

auto IConstraint::create_six_dof_constraint_unique(
    const Six_dof_constraint_settings& settings
) -> std::unique_ptr<IConstraint>
{
    return std::make_unique<Box3d_six_dof_constraint>(settings);
}

} // namespace erhe::physics
