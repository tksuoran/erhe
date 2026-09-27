#include "erhe_physics/box3d/box3d_constraint.hpp"
#include "erhe_physics/box3d/box3d_rigid_body.hpp"
#include "erhe_physics/box3d_six_dof_classifier.hpp"
#include "erhe_physics/box3d/box3d_world.hpp"
#include "erhe_physics/box3d/glm_conversions.hpp"
#include "erhe_physics/joint_limits.hpp"
#include "erhe_physics/physics_log.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace erhe::physics {

Box3d_constraint::~Box3d_constraint() noexcept
{
    if (m_is_valid) {
        b3DestroyJoint(m_joint, true);
        m_is_valid = false;
    }
}

void Box3d_constraint::prepare_step(const float)
{
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

// The effective mass of one degree of freedom of the joint, the mass a spring
// or a drive on that axis acts on: along a translation axis the reduced mass
// of the bodies, about a rotation axis 1 / (a . (I_A^-1 + I_B^-1) . a) with
// the axis in world space and the world inverse inertia tensors (zero for a
// static body). Zero when neither body can move.
[[nodiscard]] auto effective_mass(const Joint_bodies& bodies, const Transform& frame_in_a, const std::size_t axis_index) -> float
{
    if (axis_index < 3) {
        return reduced_mass(bodies.mass_a, bodies.mass_b);
    }
    const glm::vec3 local_axis = glm::normalize(frame_in_a.basis[static_cast<int>(axis_index - 3)]);
    const glm::vec3 world_axis = from_box3d(b3Body_GetWorldVector(bodies.body_a, to_box3d(local_axis)));
    const glm::mat3 inverse_inertia =
        from_box3d(b3Body_GetWorldInverseRotationalInertia(bodies.body_a)) +
        from_box3d(b3Body_GetWorldInverseRotationalInertia(bodies.body_b));
    const float k = glm::dot(world_axis, inverse_inertia * world_axis);
    return (k > 0.0f) ? (1.0f / k) : 0.0f;
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
    explicit Box3d_six_dof_constraint(const Six_dof_constraint_settings& authored_settings)
    {
        const Joint_bodies bodies = resolve_bodies(authored_settings.rigid_body_a, authored_settings.rigid_body_b, "six-dof constraint");
        if (!bodies.valid) {
            return;
        }

        // Box3D fixes an axis at zero only: a fixed axis authored at another
        // value is folded into frame A first (joint_limits.hpp).
        Six_dof_constraint_settings settings = authored_settings;
        const Fixed_axis_fold fold = fold_fixed_axis_values(settings.frame_in_a, settings.limits);
        if (!fold.exact) {
            log_physics->warn(
                "box3d six-dof constraint: a fixed rotation at a non-zero angle cannot be folded into the joint frame "
                "while a translation axis is not fixed; it is fixed at 0"
            );
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
        if (classification.kind != Six_dof_joint_kind::weld) {
            warn_dropped_soft_limits(settings, c_str(classification.kind));
        }

        m_body_a = bodies.body_a;
        m_body_b = bodies.body_b;

        Joint_base_values base{};
        base.body_a        = bodies.body_a;
        base.body_b        = bodies.body_b;
        base.local_frame_a = to_box3d(settings.frame_in_a);
        base.local_frame_b = to_box3d(settings.frame_in_b);

        const b3WorldId world = bodies.world->get_box3d_world();

        switch (classification.kind) {
            case Six_dof_joint_kind::weld:      create_weld     (world, base, bodies, settings); break;
            case Six_dof_joint_kind::revolute:  create_revolute (world, base, bodies, settings, classification); break;
            case Six_dof_joint_kind::prismatic: create_prismatic(world, base, bodies, settings, classification); break;
            case Six_dof_joint_kind::distance:  create_distance (world, base, settings, classification); break;
            case Six_dof_joint_kind::spherical: create_spherical(world, base, bodies, settings, classification); break;
            case Six_dof_joint_kind::filter:    create_filter   (world, base); break;
            default: break;
        }
    }

    // The velocity drive of the joint's degree of freedom: the motor's force
    // limit follows the current velocity error, so the motor behaves as the
    // finite-gain viscous coupling the drive states.
    void prepare_step(const float) override
    {
        if (!m_is_valid || !m_velocity_drive.active) {
            return;
        }
        const Velocity_drive& drive = m_velocity_drive;
        switch (b3Joint_GetType(m_joint)) {
            case b3_revoluteJoint: {
                // The hinge axis is the Z axis of (the remapped) frame A.
                const b3Transform frame_a    = b3Joint_GetLocalFrameA(m_joint);
                const glm::vec3   hinge_axis = from_box3d(b3Body_GetWorldVector(m_body_a, b3RotateVector(frame_a.q, b3Vec3{0.0f, 0.0f, 1.0f})));
                const glm::vec3   relative   = from_box3d(b3Body_GetAngularVelocity(m_body_b)) - from_box3d(b3Body_GetAngularVelocity(m_body_a));
                const float       speed      = glm::dot(relative, hinge_axis);
                b3RevoluteJoint_SetMaxMotorTorque(m_joint, force_limit(drive, std::abs(drive.target - speed)));
                break;
            }
            case b3_prismaticJoint: {
                const float speed = b3PrismaticJoint_GetSpeed(m_joint);
                b3PrismaticJoint_SetMaxMotorForce(m_joint, force_limit(drive, std::abs(drive.target - speed)));
                break;
            }
            case b3_sphericalJoint: {
                // The motor velocity is a world space relative angular
                // velocity, so the target in body A space is re-expressed in
                // world space each step as body A turns.
                const glm::vec3 target_world = from_box3d(b3Body_GetWorldVector(m_body_a, to_box3d(drive.target_in_body_a)));
                const glm::vec3 relative     = from_box3d(b3Body_GetAngularVelocity(m_body_b)) - from_box3d(b3Body_GetAngularVelocity(m_body_a));
                b3SphericalJoint_SetMotorVelocity(m_joint, to_box3d(target_world));
                b3SphericalJoint_SetMaxMotorTorque(m_joint, force_limit(drive, glm::length(target_world - relative)));
                break;
            }
            default: {
                break;
            }
        }
    }

private:
    class Velocity_drive
    {
    public:
        bool      active          {false};
        float     gain            {0.0f};   // force per unit velocity error; infinity = hard motor
        float     max_force       {0.0f};   // the authored bound, made finite
        float     target          {0.0f};   // revolute / prismatic
        glm::vec3 target_in_body_a{0.0f};   // spherical: the angular velocity target in body A space
    };

    [[nodiscard]] static auto force_limit(const Velocity_drive& drive, const float velocity_error) -> float
    {
        if (!std::isfinite(drive.gain)) {
            return drive.max_force;
        }
        return std::min(drive.max_force, drive.gain * velocity_error);
    }

    // The joint's motor is bounded to its authored max force at creation; the
    // per-step clamp then takes over from prepare_step().
    void set_velocity_drive(const Constraint_axis_drive& drive, const float mass)
    {
        m_velocity_drive.active    = true;
        m_velocity_drive.gain      = velocity_drive_gain(drive, mass);
        m_velocity_drive.max_force = finite_force(drive.max_force);
        m_velocity_drive.target    = drive.velocity_target;
    }

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

    // Only the weld joint has springs on its (fixed) axes; the limits of the
    // other joints are hard.
    static void warn_dropped_soft_limits(const Six_dof_constraint_settings& settings, const char* kind)
    {
        for (std::size_t axis = 0; axis < 6; ++axis) {
            const Constraint_axis_limit& limit = settings.limits[axis];
            if (limit.limited && limit.stiffness.has_value()) {
                log_physics->warn(
                    "box3d six-dof constraint: soft limit on axis {} (stiffness {}) dropped; a {} joint's limits are hard",
                    axis, limit.stiffness.value(), kind
                );
            }
        }
    }

    // A position drive is a Box3D spring: stiffness and damping toward the
    // target. The spring has no force bound and no velocity target, so those
    // parts of the drive are reported.
    static void warn_partial_position_drive(const Constraint_axis_drive& drive, const std::size_t axis, const char* kind)
    {
        if (std::isfinite(drive.max_force)) {
            log_physics->warn(
                "box3d six-dof constraint: position drive on axis {} max force {} dropped; a {} joint spring is unbounded",
                axis, drive.max_force, kind
            );
        }
        if (drive.velocity_target != 0.0f) {
            log_physics->warn(
                "box3d six-dof constraint: position drive on axis {} velocity target {} dropped; a {} joint spring damps toward rest",
                axis, drive.velocity_target, kind
            );
        }
    }

    void create_weld(
        const b3WorldId                    world,
        const Joint_base_values&           base,
        const Joint_bodies&                bodies,
        const Six_dof_constraint_settings& settings
    )
    {
        b3WeldJointDef joint_def = b3DefaultWeldJointDef();
        base.apply_to(joint_def.base);
        // A weld's only tuning is spring stiffness; 0 hertz means rigid, which
        // is what a hard-limited axis wants. The stiffness is a force per
        // meter (or per radian): converted with the effective mass of the
        // softest axis that names it.
        float linear_hertz    = 0.0f;
        float angular_hertz   = 0.0f;
        float linear_damping  = 0.0f;
        float angular_damping = 0.0f;
        for (std::size_t axis = 0; axis < 6; ++axis) {
            const Constraint_axis_limit& limit = settings.limits[axis];
            if (!limit.stiffness.has_value()) {
                continue;
            }
            const float mass  = effective_mass(bodies, settings.frame_in_a, axis);
            const float hertz = stiffness_to_hertz(limit.stiffness.value(), mass);
            const float ratio = damping_to_ratio(limit.damping, limit.stiffness.value(), mass);
            if (axis < 3) {
                linear_hertz   = hertz;
                linear_damping = ratio;
            } else {
                angular_hertz   = hertz;
                angular_damping = ratio;
            }
        }
        joint_def.linearHertz         = linear_hertz;
        joint_def.angularHertz        = angular_hertz;
        joint_def.linearDampingRatio  = linear_damping;
        joint_def.angularDampingRatio = angular_damping;

        m_joint    = b3CreateWeldJoint(world, &joint_def);
        m_is_valid = true;
    }

    void create_revolute(
        const b3WorldId                    world,
        const Joint_base_values&           base,
        const Joint_bodies&                bodies,
        const Six_dof_constraint_settings& settings,
        const Six_dof_classification&      classification
    )
    {
        b3RevoluteJointDef joint_def = b3DefaultRevoluteJointDef();
        base.apply_to(joint_def.base);
        // Box3D's revolute joint rotates about its local frame Z.
        remap_frames(joint_def.base, revolute_frame_rotation(classification.axis));

        const std::size_t              axis_index = static_cast<std::size_t>(3 + classification.axis);
        const Constraint_axis_limit&   limit      = settings.limits[axis_index];
        const Constraint_axis_drive&   drive      = settings.drives[axis_index];
        const float                    mass       = effective_mass(bodies, settings.frame_in_a, axis_index);

        if (classify_axis(limit) == Axis_state::limited) {
            joint_def.enableLimit = true;
            joint_def.lowerAngle  = clamp_angle(limit.min);
            joint_def.upperAngle  = clamp_angle(limit.max);
        }
        if (drive.enabled) {
            if (drive.use_position_target) {
                const Box3d_spring spring = drive_to_box3d_spring(drive, mass);
                joint_def.enableSpring = true;
                joint_def.targetAngle  = drive.position_target;
                joint_def.hertz        = spring.hertz;
                joint_def.dampingRatio = spring.damping_ratio;
                warn_partial_position_drive(drive, axis_index, "revolute");
            } else {
                joint_def.enableMotor    = true;
                joint_def.motorSpeed     = drive.velocity_target;
                joint_def.maxMotorTorque = finite_force(drive.max_force);
                set_velocity_drive(drive, mass);
            }
        }
        warn_dropped_drives(settings, static_cast<int>(axis_index), "revolute");

        m_joint    = b3CreateRevoluteJoint(world, &joint_def);
        m_is_valid = true;
    }

    void create_prismatic(
        const b3WorldId                    world,
        const Joint_base_values&           base,
        const Joint_bodies&                bodies,
        const Six_dof_constraint_settings& settings,
        const Six_dof_classification&      classification
    )
    {
        b3PrismaticJointDef joint_def = b3DefaultPrismaticJointDef();
        base.apply_to(joint_def.base);
        // Box3D's prismatic joint slides along its local frame X.
        remap_frames(joint_def.base, prismatic_frame_rotation(classification.axis));

        const std::size_t            axis_index = static_cast<std::size_t>(classification.axis);
        const Constraint_axis_limit& limit      = settings.limits[axis_index];
        const Constraint_axis_drive& drive      = settings.drives[axis_index];
        const float                  mass       = effective_mass(bodies, settings.frame_in_a, axis_index);

        if (classify_axis(limit) == Axis_state::limited) {
            joint_def.enableLimit      = true;
            joint_def.lowerTranslation = limit.min;
            joint_def.upperTranslation = limit.max;
        }
        if (drive.enabled) {
            if (drive.use_position_target) {
                const Box3d_spring spring = drive_to_box3d_spring(drive, mass);
                joint_def.enableSpring      = true;
                joint_def.targetTranslation = drive.position_target;
                joint_def.hertz             = spring.hertz;
                joint_def.dampingRatio      = spring.damping_ratio;
                warn_partial_position_drive(drive, axis_index, "prismatic");
            } else {
                joint_def.enableMotor   = true;
                joint_def.motorSpeed    = drive.velocity_target;
                joint_def.maxMotorForce = finite_force(drive.max_force);
                set_velocity_drive(drive, mass);
            }
        }
        warn_dropped_drives(settings, static_cast<int>(axis_index), "prismatic");

        m_joint    = b3CreatePrismaticJoint(world, &joint_def);
        m_is_valid = true;
    }

    // Rotation free, the frame origins kept within a length range: a distance
    // joint with the spring enabled at zero hertz applies its length limits
    // only (distance_joint.c: with the spring disabled the joint is rigid at
    // its rest length and the limits are ignored). An equal minimum and
    // maximum is that rigid rod.
    void create_distance(
        const b3WorldId                    world,
        const Joint_base_values&           base,
        const Six_dof_constraint_settings& settings,
        const Six_dof_classification&      classification
    )
    {
        b3DistanceJointDef joint_def = b3DefaultDistanceJointDef();
        base.apply_to(joint_def.base);

        const float min_length = classification.min_distance;
        const float max_length = std::isfinite(classification.max_distance) ? classification.max_distance : B3_HUGE;
        const b3Pos anchor_a   = b3Body_GetWorldPoint(base.body_a, base.local_frame_a.p);
        const b3Pos anchor_b   = b3Body_GetWorldPoint(base.body_b, base.local_frame_b.p);
        const float length     = b3Length(b3SubPos(anchor_b, anchor_a));

        joint_def.length       = std::clamp(length, min_length, max_length);
        joint_def.enableSpring = min_length < max_length;
        joint_def.hertz        = 0.0f;
        joint_def.dampingRatio = 0.0f;
        joint_def.enableLimit  = true;
        joint_def.minLength    = min_length;
        joint_def.maxLength    = max_length;

        for (std::size_t axis = 0; axis < 6; ++axis) {
            if (settings.drives[axis].enabled) {
                log_physics->warn(
                    "box3d six-dof constraint: drive on axis {} dropped; a distance joint drives no axis",
                    axis
                );
            }
        }

        m_joint    = b3CreateDistanceJoint(world, &joint_def);
        m_is_valid = true;
    }

    // A spherical joint always exposes all three rotational axes, so it needs
    // the raw limits rather than a single chosen axis: the classified twist
    // axis takes the twist limit and the cone spans the other two.
    void create_spherical(
        const b3WorldId                    world,
        const Joint_base_values&           base,
        const Joint_bodies&                bodies,
        const Six_dof_constraint_settings& settings,
        const Six_dof_classification&      classification
    )
    {
        b3SphericalJointDef joint_def = b3DefaultSphericalJointDef();
        base.apply_to(joint_def.base);
        // Box3D's spherical joint twists about frame Z and its cone is about
        // frame A's Z.
        const int twist_axis = classification.axis;
        remap_frames(joint_def.base, revolute_frame_rotation(twist_axis));

        // A cone limit constrains the two swing axes together, so the widest
        // reach of the limited swing ranges sets the cone half-angle.
        const std::array<int, 2> swing_axes = get_swing_axes(twist_axis);
        float cone_angle = 0.0f;
        bool  has_cone   = false;
        for (const int swing_axis : swing_axes) {
            const Constraint_axis_limit& limit = settings.limits[static_cast<std::size_t>(3 + swing_axis)];
            if (classify_axis(limit) != Axis_state::limited) {
                continue;
            }
            cone_angle = std::max(cone_angle, std::max(std::abs(limit.min), std::abs(limit.max)));
            has_cone   = true;
        }
        if (has_cone) {
            joint_def.enableConeLimit = true;
            joint_def.coneAngle       = std::min(cone_angle, max_joint_angle);
        }

        const Constraint_axis_limit& twist_limit = settings.limits[static_cast<std::size_t>(3 + twist_axis)];
        switch (classify_axis(twist_limit)) {
            case Axis_state::limited: {
                joint_def.enableTwistLimit = true;
                joint_def.lowerTwistAngle  = clamp_angle(twist_limit.min);
                joint_def.upperTwistAngle  = clamp_angle(twist_limit.max);
                break;
            }
            case Axis_state::fixed: {
                // A universal joint: the twist about the fixed axis is locked.
                joint_def.enableTwistLimit = true;
                joint_def.lowerTwistAngle  = 0.0f;
                joint_def.upperTwistAngle  = 0.0f;
                break;
            }
            default: {
                break;
            }
        }

        // A spherical joint's motor takes an angular velocity vector, so all
        // three rotational velocity drives are carried at once, with the
        // largest gain and bound; its alignment spring carries one position
        // drive.
        glm::vec3 motor_velocity{0.0f};
        bool      has_velocity_drive = false;
        float     max_gain           = 0.0f;
        float     max_torque         = 0.0f;
        bool      has_position_drive = false;
        for (int axis = 0; axis < 3; ++axis) {
            const std::size_t            axis_index = static_cast<std::size_t>(3 + axis);
            const Constraint_axis_drive& drive      = settings.drives[axis_index];
            if (!drive.enabled) {
                continue;
            }
            const float mass = effective_mass(bodies, settings.frame_in_a, axis_index);
            if (drive.use_position_target) {
                if (has_position_drive) {
                    log_physics->warn("box3d six-dof constraint: position drive on axis {} dropped; a spherical joint has one alignment spring", axis_index);
                    continue;
                }
                const Box3d_spring spring = drive_to_box3d_spring(drive, mass);
                has_position_drive     = true;
                joint_def.enableSpring = true;
                joint_def.hertz        = spring.hertz;
                joint_def.dampingRatio = spring.damping_ratio;
                joint_def.targetRotation = to_box3d(
                    glm::inverse(revolute_frame_rotation(twist_axis)) *
                    glm::angleAxis(drive.position_target, glm::vec3{(axis == 0) ? 1.0f : 0.0f, (axis == 1) ? 1.0f : 0.0f, (axis == 2) ? 1.0f : 0.0f}) *
                    revolute_frame_rotation(twist_axis)
                );
                warn_partial_position_drive(drive, axis_index, "spherical");
            } else {
                has_velocity_drive   = true;
                motor_velocity[axis] = drive.velocity_target;
                max_gain             = std::max(max_gain, velocity_drive_gain(drive, mass));
                max_torque           = std::max(max_torque, finite_force(drive.max_force));
            }
        }
        if (has_velocity_drive) {
            // The motor velocity is a world space relative angular velocity;
            // prepare_step() keeps it current from the body A space target.
            const glm::vec3 target_in_body_a = settings.frame_in_a.basis * motor_velocity;
            joint_def.enableMotor    = true;
            joint_def.motorVelocity  = b3Body_GetWorldVector(bodies.body_a, to_box3d(target_in_body_a));
            joint_def.maxMotorTorque = max_torque;
            m_velocity_drive.active           = true;
            m_velocity_drive.gain             = max_gain;
            m_velocity_drive.max_force        = max_torque;
            m_velocity_drive.target_in_body_a = target_in_body_a;
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

    b3BodyId       m_body_a{};
    b3BodyId       m_body_b{};
    Velocity_drive m_velocity_drive{};
};

// -----------------------------------------------------------------------------
// IConstraint factories
// -----------------------------------------------------------------------------

namespace {

[[nodiscard]] auto fixed_at_zero() -> Constraint_axis_limit
{
    return Constraint_axis_limit{.limited = true, .min = 0.0f, .max = 0.0f};
}

// An axis the chosen Box3D joint carries: free, or limited with the joint's
// own clamp.
[[nodiscard]] auto carried_axis(const Constraint_axis_limit& limit, const bool is_rotation) -> Constraint_axis_limit
{
    switch (classify_axis(limit)) {
        case Axis_state::free:  return Constraint_axis_limit{};
        case Axis_state::fixed: return fixed_at_zero();
        default: break;
    }
    return Constraint_axis_limit{
        .limited = true,
        .min     = is_rotation ? clamp_angle(limit.min) : limit.min,
        .max     = is_rotation ? clamp_angle(limit.max) : limit.max
    };
}

[[nodiscard]] auto same_limit(const Constraint_axis_limit& lhs, const Constraint_axis_limit& rhs) -> bool
{
    if (lhs.limited != rhs.limited) {
        return false;
    }
    return !lhs.limited || ((lhs.min == rhs.min) && (lhs.max == rhs.max));
}

} // anonymous namespace

// The shapes follow Box3d_six_dof_constraint's joint creation: the fold of
// fixed values into frame A, then the weld, the revolute joint about the
// classified axis, the prismatic joint along it, the distance joint (a sphere
// of the translation ranges), the spherical joint (twist about the classified
// twist axis, one cone of the widest limited swing reach about it, a locked
// twist for a universal joint) and the filter joint (nothing constrained).
auto get_enforced_joint_limits(const std::array<Constraint_axis_limit, 6>& authored_limits) -> Joint_limit_shape
{
    Transform                            folded_frame{};
    std::array<Constraint_axis_limit, 6> limits = authored_limits;
    const Fixed_axis_fold                fold   = fold_fixed_axis_values(folded_frame, limits);
    const Six_dof_classification         classification = classify_six_dof(limits);

    Joint_limit_shape shape{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        shape.translation[axis] = fixed_at_zero();
    }
    shape.twist_axis  = 0;
    shape.swing_model = Swing_limit_model::pyramid;
    shape.twist       = fixed_at_zero();
    shape.swing       = {fixed_at_zero(), fixed_at_zero()};

    bool matches = true;
    switch (classification.kind) {
        case Six_dof_joint_kind::weld: {
            break;
        }
        case Six_dof_joint_kind::revolute: {
            shape.twist_axis = classification.axis;
            shape.twist      = carried_axis(limits[static_cast<std::size_t>(3 + classification.axis)], true);
            matches = same_limit(shape.twist, limits[static_cast<std::size_t>(3 + classification.axis)]);
            break;
        }
        case Six_dof_joint_kind::prismatic: {
            const std::size_t axis = static_cast<std::size_t>(classification.axis);
            shape.translation[axis] = carried_axis(limits[axis], false);
            break;
        }
        case Six_dof_joint_kind::distance: {
            const float radius = classification.max_distance;
            shape.translation_model = Translation_limit_model::sphere;
            shape.distance = std::isfinite(radius)
                ? Constraint_axis_limit{.limited = true, .min = classification.min_distance, .max = radius}
                : Constraint_axis_limit{};
            for (std::size_t axis = 0; axis < 3; ++axis) {
                shape.translation[axis] = std::isfinite(radius)
                    ? Constraint_axis_limit{.limited = true, .min = -radius, .max = radius}
                    : Constraint_axis_limit{};
            }
            shape.twist       = Constraint_axis_limit{};
            shape.swing_model = Swing_limit_model::cone;
            shape.swing       = {Constraint_axis_limit{}, Constraint_axis_limit{}};
            shape.cone        = Constraint_axis_limit{};
            break;
        }
        case Six_dof_joint_kind::spherical: {
            const int twist_axis = classification.axis;
            shape.twist_axis  = twist_axis;
            shape.swing_model = Swing_limit_model::cone;
            const Constraint_axis_limit& twist_limit = limits[static_cast<std::size_t>(3 + twist_axis)];
            shape.twist = carried_axis(twist_limit, true);
            const std::array<int, 2> swing_axes = get_swing_axes(twist_axis);
            float cone_angle = 0.0f;
            bool  has_cone   = false;
            for (const int swing_axis : swing_axes) {
                const Constraint_axis_limit& limit = limits[static_cast<std::size_t>(3 + swing_axis)];
                if (classify_axis(limit) != Axis_state::limited) {
                    continue;
                }
                cone_angle = std::max(cone_angle, std::max(std::abs(limit.min), std::abs(limit.max)));
                has_cone   = true;
            }
            shape.cone = has_cone
                ? Constraint_axis_limit{.limited = true, .min = 0.0f, .max = std::min(cone_angle, max_joint_angle)}
                : Constraint_axis_limit{};
            matches = same_limit(shape.twist, twist_limit);
            break;
        }
        case Six_dof_joint_kind::filter: {
            shape.translation = {Constraint_axis_limit{}, Constraint_axis_limit{}, Constraint_axis_limit{}};
            shape.twist       = Constraint_axis_limit{};
            shape.swing       = {Constraint_axis_limit{}, Constraint_axis_limit{}};
            break;
        }
        default: {
            break;
        }
    }
    restore_folded_fixed_values(shape, authored_limits, fold);
    shape.is_exact = classification.is_exact && matches && fold.exact;
    return shape;
}

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
