// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe/physics/jolt/jolt_rigid_body.hpp"
#include "erhe/log/log_glm.hpp"
#include "erhe/physics/jolt/jolt_collision_shape.hpp"
#include "erhe/physics/jolt/jolt_world.hpp"
#include "erhe/physics/imotion_state.hpp"
#include "erhe/physics/physics_log.hpp"
#include "erhe/physics/transform.hpp"
#include "erhe/scene/node.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/ConvexShape.h>

#include <glm/glm.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace erhe::physics
{

[[nodiscard]] auto to_jolt(Motion_mode motion_mode) -> JPH::EMotionType
{
    switch (motion_mode)
    {
        case Motion_mode::e_static:                 return JPH::EMotionType::Static;
        case Motion_mode::e_kinematic_non_physical: return JPH::EMotionType::Kinematic;
        case Motion_mode::e_kinematic_physical:     return JPH::EMotionType::Kinematic;
        case Motion_mode::e_dynamic:                return JPH::EMotionType::Dynamic;
        default:
        {
            abort();
        }
    }
}

auto IRigid_body::create_rigid_body(
    const IRigid_body_create_info& create_info,
    IMotion_state*                 motion_state
) -> IRigid_body*
{
    // auto& body_interface = m_physics_system.GetBodyInterface();

    return new Jolt_rigid_body(create_info, motion_state);
}

auto IRigid_body::create_rigid_body_shared(
    const IRigid_body_create_info& create_info,
    IMotion_state*                 motion_state
) -> std::shared_ptr<IRigid_body>
{
    return std::make_shared<Jolt_rigid_body>(create_info, motion_state);
}

IRigid_body::~IRigid_body() noexcept
{
}

Jolt_rigid_body::Jolt_rigid_body(
    const IRigid_body_create_info& create_info,
    IMotion_state*                 motion_state
)
    : m_motion_state   {motion_state}
    , m_body_interface {
        reinterpret_cast<Jolt_world&>(create_info.world)
            .get_physics_system()
            .GetBodyInterface()
    }
    , m_collision_shape{std::dynamic_pointer_cast<Jolt_collision_shape>(create_info.collision_shape)}
    , m_mass           {create_info.mass > 0.0f ? create_info.mass : 1.0f}
    , m_local_inertia  {create_info.local_inertia}
    , m_motion_mode    {motion_state->get_motion_mode()}
    , m_friction       {create_info.friction}
    , m_restitution    {create_info.restitution}
    , m_linear_damping {create_info.linear_damping}
    , m_angular_damping{create_info.angular_damping}
    , m_debug_label    {create_info.debug_label}
{
    if (!m_collision_shape)
    {
        return;
    }

    const JPH::Shape* shape     = m_collision_shape->get_jolt_shape().GetPtr();
    const auto        transform = motion_state->get_world_from_rigidbody();
    const JPH::Vec3   position  = to_jolt(transform.origin);

    SPDLOG_LOGGER_TRACE(
        log_physics,
        "rigid body create {} with position {}",
        m_debug_label,
        transform.origin
    );

    const JPH::Quat rotation    = to_jolt(glm::quat(transform.basis));
    const auto      motion_mode = motion_state->get_motion_mode();
    JPH::BodyCreationSettings creation_settings
    {
        shape,
        position,
        rotation,
        to_jolt(motion_mode),
        create_info.enable_collisions
            ? Layers::get_layer(motion_mode)
            : Layers::NON_COLLIDING
    };
    creation_settings.mAllowDynamicOrKinematic         = true;
    creation_settings.mFriction                        = create_info.friction;
    creation_settings.mRestitution                     = create_info.restitution;
    creation_settings.mOverrideMassProperties          = JPH::EOverrideMassProperties::CalculateMassAndInertia; // JPH::EOverrideMassProperties::MassAndInertiaProvided;
    creation_settings.mMassPropertiesOverride.mMass    = create_info.mass > 0.0f ? create_info.mass : 1.0f;
    creation_settings.mMassPropertiesOverride.mInertia = to_jolt(create_info.local_inertia);
    creation_settings.mLinearDamping                   = create_info.linear_damping;
    creation_settings.mAngularDamping                  = create_info.angular_damping;
    creation_settings.mLinearDamping                   = 0.001f;

    static_assert(sizeof(uintptr_t) <= sizeof(JPH::uint64));
    creation_settings.mUserData = static_cast<JPH::uint64>(reinterpret_cast<uintptr_t>(this));
    m_body = m_body_interface.CreateBody(creation_settings);
}

Jolt_rigid_body::~Jolt_rigid_body() noexcept = default;

auto Jolt_rigid_body::get_motion_mode() const -> Motion_mode
{
    return m_motion_mode;
}

void Jolt_rigid_body::set_collision_shape(const std::shared_ptr<ICollision_shape>& collision_shape)
{
    m_collision_shape = std::dynamic_pointer_cast<Jolt_collision_shape>(collision_shape);
}

auto Jolt_rigid_body::get_collision_shape() const -> std::shared_ptr<ICollision_shape>
{
    return m_collision_shape;
}

auto Jolt_rigid_body::get_friction() const -> float
{
    return m_friction;
}

void Jolt_rigid_body::set_friction(const float friction)
{
    if (m_body == nullptr)
    {
        log_physics->error("Fixed world body cannot be modified");
        return;
    }
    if (m_friction == friction)
    {
        return;
    }
    SPDLOG_LOGGER_TRACE(
        log_physics,
        "{} set friction {}",
        m_debug_label,
        friction
    );
    m_friction = friction;
    m_body->SetFriction(friction);
}

auto Jolt_rigid_body::get_gravity_factor() const -> float
{
    if (m_body == nullptr) {
        return 0.0f;
    }
    return m_body_interface.GetGravityFactor(m_body->GetID());
}

void Jolt_rigid_body::set_gravity_factor(const float gravity_factor)
{
    if (m_body == nullptr)
    {
        log_physics->error("Fixed world body cannot be modified");
        return;
    }
    SPDLOG_LOGGER_TRACE(
        log_physics,
        "{} set gravity factor {}",
        m_debug_label,
        gravity_factor
    );
    m_body_interface.SetGravityFactor(m_body->GetID(), gravity_factor);
}

auto Jolt_rigid_body::get_restitution() const -> float
{
    return m_restitution;
}

void Jolt_rigid_body::set_restitution(float restitution)
{
    if (m_body == nullptr)
    {
        log_physics->error("Fixed world body cannot be modified");
        return;
    }
    if (m_restitution == restitution)
    {
        return;
    }
    m_restitution = restitution;
    m_body->SetRestitution(restitution);
}

void Jolt_rigid_body::begin_move()
{
    if (m_body == nullptr)
    {
        log_physics->error("Fixed world body cannot be modified");
        return;
    }
    SPDLOG_LOGGER_TRACE(log_physics, "{} begin move", m_debug_label);
    m_body->SetAllowSleeping(false);
    m_body_interface.ActivateBody(m_body->GetID());
}

void Jolt_rigid_body::end_move()
{
    if (m_body == nullptr)
    {
        log_physics->error("Fixed world body cannot be modified");
        return;
    }
    SPDLOG_LOGGER_TRACE(log_physics, "{} end move", m_debug_label);
    m_body->SetAllowSleeping(true);
    log_physics->trace("End velocity = {}", get_linear_velocity());
    //m_body_interface.ActivateBody(m_body->GetID());
}

namespace {

auto c_str(const Motion_mode motion_mode) -> const char*
{
    switch (motion_mode)
    {
        case Motion_mode::e_static: return "Static";
        case Motion_mode::e_kinematic_non_physical: return "Kinematic Non-Physical";
        case Motion_mode::e_kinematic_physical: return "Kinematic Physical";
        case Motion_mode::e_dynamic: return "Dynamic";
        default: return "?";
    }
}

}

void Jolt_rigid_body::set_motion_mode(const Motion_mode motion_mode)
{
    log_physics->info("{} set_motion_mode({})", get_debug_label(), c_str(motion_mode));

    if (m_body == nullptr)
    {
        log_physics->error("Fixed world body cannot be modified");
        return;
    }
    if (m_motion_mode == motion_mode)
    {
        return;
    }
    SPDLOG_LOGGER_TRACE(log_physics, "{} set motion mode = {}", m_debug_label, c_str(motion_mode));
    if (m_body->IsActive() && (motion_mode == Motion_mode::e_static))
    {
        m_body_interface.DeactivateBody(m_body->GetID());
    }

    m_motion_mode = motion_mode;
    m_body->SetMotionType(to_jolt(motion_mode));

    if (motion_mode == Motion_mode::e_dynamic)
    {
        m_body_interface.ActivateBody(m_body->GetID());
    }
}

auto Jolt_rigid_body::get_center_of_mass_transform() const -> Transform
{
    const auto rotation = from_jolt(m_body->GetRotation());
    const auto position = from_jolt(m_body->GetCenterOfMassPosition());
    return Transform{
        glm::mat3{rotation},
        position
    };
}

void Jolt_rigid_body::set_center_of_mass_transform(const Transform transform)
{
    if (m_body == nullptr)
    {
        log_physics->error("Fixed world body cannot be modified");
        return;
    }

    static_cast<void>(transform);
    // TODO
    //const auto rotation = m_body->GetRotation();
    //const auto position = m_body->GetCenterOfMassPosition();
}

void Jolt_rigid_body::move_world_transform(const Transform transform, float delta_time)
{
    if (m_body == nullptr)
    {
        log_physics->error("Fixed world body cannot be modified");
        return;
    }

    SPDLOG_LOGGER_TRACE(
        log_physics,
        "{} move to position {} time {}",
        m_debug_label,
        transform.origin,
        delta_time
    );

    m_body_interface.MoveKinematic(
        m_body->GetID(),
        to_jolt(transform.origin),
        to_jolt(glm::quat{transform.basis}),
        delta_time
        //(m_motion_mode != Motion_mode::e_static)
        //    ? JPH::EActivation::Activate
        //    : JPH::EActivation::DontActivate
    );
}

auto Jolt_rigid_body::get_world_transform() const -> Transform
{
    if (m_body == nullptr)
    {
        return erhe::physics::Transform{};
    }

    JPH::Quat basis;
    JPH::Vec3 origin;
    m_body_interface.GetPositionAndRotation(
        m_body->GetID(),
        origin,
        basis
    );
    return erhe::physics::Transform{
        glm::mat3{from_jolt(basis)},
        from_jolt(origin)
    };
}

void Jolt_rigid_body::set_world_transform(const Transform transform)
{
    if (m_body == nullptr)
    {
        log_physics->error("Fixed world body cannot be modified");
        return;
    }

    SPDLOG_LOGGER_TRACE(log_physics, "{} set transform position {}", m_debug_label, transform.origin);

    m_body_interface.SetPositionAndRotation(
        m_body->GetID(),
        to_jolt(transform.origin),
        to_jolt(glm::quat{transform.basis}),
        JPH::EActivation::DontActivate
        //(m_motion_mode != Motion_mode::e_static)
        //    ? JPH::EActivation::Activate
        //    : JPH::EActivation::DontActivate
    );
}

auto Jolt_rigid_body::get_linear_velocity() const -> glm::vec3
{
    if (m_body == nullptr)
    {
        return {};
    }

    return from_jolt(m_body->GetLinearVelocity());
}

void Jolt_rigid_body::set_linear_velocity(const glm::vec3 velocity)
{
    if (m_body == nullptr)
    {
        log_physics->error("Fixed world body cannot be modified");
        return;
    }

    SPDLOG_LOGGER_TRACE(log_physics, "{} set linear velocity {}", m_debug_label, velocity);

    m_body_interface.SetLinearVelocity(m_body->GetID(), to_jolt(velocity));
}

auto Jolt_rigid_body::get_angular_velocity() const -> glm::vec3
{
    if (m_body == nullptr)
    {
        return {};
    }

    return from_jolt(m_body->GetAngularVelocity());
}

void Jolt_rigid_body::set_angular_velocity(const glm::vec3 velocity)
{
    if (m_body == nullptr)
    {
        log_physics->error("Fixed world body cannot be modified");
        return;
    }

    SPDLOG_LOGGER_TRACE(log_physics, "{} set angular velocity {}", m_debug_label, velocity);

    m_body_interface.SetAngularVelocity(m_body->GetID(), to_jolt(velocity));
}

auto Jolt_rigid_body::get_linear_damping() const -> float
{
    if (m_body == nullptr)
    {
        return 0.0f;
    }
    auto* motion_properties = m_body->GetMotionProperties();
    if (motion_properties == nullptr)
    {
        return 0.0f;
    }
    return motion_properties->GetLinearDamping();
}

void Jolt_rigid_body::set_damping(const float linear_damping, const float angular_damping)
{
    if (m_body == nullptr)
    {
        log_physics->error("Fixed world body cannot be modified");
        return;
    }

    SPDLOG_LOGGER_TRACE(log_physics, "{} set damping linear = {}, angular = {}", m_debug_label, linear_damping, angular_damping);

    auto* motion_properties = m_body->GetMotionProperties();
    if (motion_properties == nullptr)
    {
        log_physics->warn("{} no motion properties");
        return;
    }
    motion_properties->SetLinearDamping(linear_damping);
    motion_properties->SetAngularDamping(angular_damping);
}

auto Jolt_rigid_body::get_angular_damping() const -> float
{
    if (m_body == nullptr)
    {
        return 0.0f;
    }

    auto* motion_properties = m_body->GetMotionProperties();
    if (motion_properties == nullptr)
    {
        return 0.0f;
    }
    return motion_properties->GetAngularDamping();
}

auto Jolt_rigid_body::get_local_inertia() const -> glm::mat4
{
    return m_local_inertia;
}

auto Jolt_rigid_body::get_mass() const -> float
{
    return m_mass;
}

void Jolt_rigid_body::set_mass_properties(const float mass, const glm::mat4 local_inertia)
{
    if (m_body == nullptr)
    {
        log_physics->error("Fixed world body cannot be modified");
        return;
    }

    SPDLOG_LOGGER_TRACE(log_physics, "{} set mass = {}", m_debug_label, mass);

    auto* motion_properties = m_body->GetMotionProperties();
    if (motion_properties == nullptr)
    {
        return;
    }
    JPH::MassProperties mass_properties
    {
        .mMass    = mass,
        .mInertia = to_jolt(local_inertia)
    };
    motion_properties->SetMassProperties(mass_properties);
    m_mass          = mass;
    m_local_inertia = local_inertia;
}

auto Jolt_rigid_body::get_debug_label() const -> const char*
{
    return m_debug_label.c_str();
}

auto Jolt_rigid_body::get_jolt_body() const -> JPH::Body*
{
    return (m_body != nullptr)
        ? m_body
        : &JPH::Body::sFixedToWorld;
}

void Jolt_rigid_body::pre_update_motion_state() const
{
    if (m_body == nullptr)
    {
        return;
    }

    // Only for kinematic moves
    if ((m_motion_mode == Motion_mode::e_static) || (m_motion_mode == Motion_mode::e_dynamic))
    {
        return;
    }

    const auto transform = m_motion_state->get_world_from_rigidbody();

    log_physics_frame->trace(
        "{} pre pos = {}",
        get_debug_label(),
        transform.origin
    );

    m_body->MoveKinematic(
        to_jolt(transform.origin),
        to_jolt(glm::quat{transform.basis}),
        1.0f / 100.0f
    );
}

void Jolt_rigid_body::update_motion_state() const
{
    if (m_body == nullptr)
    {
        return;
    }

    if (m_motion_mode != Motion_mode::e_dynamic)
    {
        return;
    }

    if (!m_body->IsActive())
    {
        return;
    }

    const auto rotation = from_jolt(m_body->GetRotation());
    const auto position = from_jolt(m_body->GetCenterOfMassPosition());

    m_motion_state->set_world_from_rigidbody(
        erhe::physics::Transform{
            glm::mat3{rotation},
            position
        }
    );
}

} // namespace erhe::physics

