// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe/physics/jolt/jolt_rigid_body.hpp"
#include "erhe/log/log_glm.hpp"
#include "erhe/physics/jolt/jolt_collision_shape.hpp"
#include "erhe/physics/jolt/jolt_world.hpp"
#include "erhe/physics/physics_log.hpp"
#include "erhe/physics/transform.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>

namespace erhe::physics
{

[[nodiscard]] auto to_jolt(const Motion_mode motion_mode) -> JPH::EMotionType
{
    switch (motion_mode) {
        case Motion_mode::e_static:                 return JPH::EMotionType::Static;
        case Motion_mode::e_kinematic_non_physical: return JPH::EMotionType::Kinematic;
        case Motion_mode::e_kinematic_physical:     return JPH::EMotionType::Kinematic;
        case Motion_mode::e_dynamic:                return JPH::EMotionType::Dynamic;
        default: {
            abort();
        }
    }
}

IRigid_body::~IRigid_body() noexcept
{
}

auto Jolt_rigid_body::get_body_interface() const -> JPH::BodyInterface&
{
    return reinterpret_cast<Jolt_world&>(m_world).get_physics_system().GetBodyInterface();
}

Jolt_rigid_body::Jolt_rigid_body(
    Jolt_world&                    world,
    const IRigid_body_create_info& create_info,
    glm::vec3                      position,
    glm::quat                      orientation
)
    : m_world          {world}
    , m_collision_shape{std::static_pointer_cast<Jolt_collision_shape>(create_info.collision_shape)}
    , m_motion_mode    {create_info.motion_mode}
{
    if (!m_collision_shape) {
        return;
    }

    const JPH::Shape* jolt_shape = m_collision_shape->get_jolt_shape().GetPtr();

    SPDLOG_LOGGER_TRACE(
        log_physics,
        "rigid body create {} collisions {}",
        m_debug_label,
        (create_info.enable_collisions ? "enabled" : "disabled")
    );

    JPH::BodyCreationSettings creation_settings{
        jolt_shape,
        to_jolt(position),
        to_jolt(orientation),
        to_jolt(m_motion_mode),
        create_info.enable_collisions
            ? Layers::get_layer(m_motion_mode)
            : Layers::NON_COLLIDING
    };

    m_mass_properties = jolt_shape->GetMassProperties();
    if (create_info.inertia_override.has_value()) {
        m_mass_properties.mInertia = to_jolt(create_info.inertia_override.value());
        if (create_info.mass.has_value()) {
            m_mass_properties.mMass = create_info.mass.value();
        } else if (create_info.density.has_value()) {
            m_mass_properties.ScaleToMass(
                m_mass_properties.mMass * create_info.density.value()
            );
        }
    } else {
        if (create_info.mass.has_value()) {
            m_mass_properties.ScaleToMass(
                create_info.mass.value()
            );
        } else if (create_info.density.has_value()) {
            m_mass_properties.ScaleToMass(
                m_mass_properties.mMass * create_info.density.value()
            );
        }
    }

    if (m_mass_properties.mMass == 0.0f) {
        m_mass_properties.mMass = 1.0f;
    }

    creation_settings.mAllowDynamicOrKinematic = true;
    creation_settings.mFriction                = create_info.friction;
    creation_settings.mRestitution             = create_info.restitution;
    creation_settings.mOverrideMassProperties  = JPH::EOverrideMassProperties::MassAndInertiaProvided; //EOverrideMassProperties::CalculateMassAndInertia; // JPH::EOverrideMassProperties::MassAndInertiaProvided;
    creation_settings.mMassPropertiesOverride  = m_mass_properties;
    creation_settings.mLinearDamping           = create_info.linear_damping;
    creation_settings.mAngularDamping          = create_info.angular_damping;

    static_assert(sizeof(uintptr_t) <= sizeof(JPH::uint64));
    creation_settings.mUserData = static_cast<JPH::uint64>(reinterpret_cast<uintptr_t>(this));

    m_body = get_body_interface().CreateBody(creation_settings);
    if (m_body == nullptr) {
        log_physics->warn("CreateBody() returned nullptr");
        m_debug_label = fmt::format("{} (CreateBody() returned nullptr)", create_info.debug_label);
    } else {
        m_debug_label = fmt::format("{} ({})", create_info.debug_label, m_body->GetID().GetIndex());
    }
}

Jolt_rigid_body::~Jolt_rigid_body() noexcept
{
    if (m_body != nullptr) {
        JPH::BodyInterface& body_interface = get_body_interface();
        body_interface.DestroyBody(m_body->GetID());
    }
}

auto Jolt_rigid_body::get_motion_mode() const -> Motion_mode
{
    return m_motion_mode;
}

auto Jolt_rigid_body::get_collision_shape() const -> std::shared_ptr<ICollision_shape>
{
    return m_collision_shape;
}

auto Jolt_rigid_body::get_friction() const -> float
{
    if (m_body == nullptr) {
        return 0.0f;
    }
    return m_body->GetFriction();
}

void Jolt_rigid_body::set_friction(const float friction)
{
    if (m_body == nullptr) {
        log_physics->error("Fixed world body cannot be modified");
        return;
    }
    SPDLOG_LOGGER_TRACE(
        log_physics,
        "{} set friction {}",
        m_debug_label,
        friction
    );
    m_body->SetFriction(friction);
}

auto Jolt_rigid_body::get_gravity_factor() const -> float
{
    if (m_body == nullptr) {
        return 0.0f;
    }
    return get_body_interface().GetGravityFactor(m_body->GetID());
}

void Jolt_rigid_body::set_gravity_factor(const float gravity_factor)
{
    if (m_body == nullptr) {
        log_physics->error("Fixed world body cannot be modified");
        return;
    }
    SPDLOG_LOGGER_TRACE(
        log_physics,
        "{} set gravity factor {}",
        m_debug_label,
        gravity_factor
    );
    get_body_interface().SetGravityFactor(m_body->GetID(), gravity_factor);
}

auto Jolt_rigid_body::get_restitution() const -> float
{
    if (m_body == nullptr) {
        return 0.0f;
    }
    return m_body->GetRestitution();
}

void Jolt_rigid_body::set_restitution(float restitution)
{
    if (m_body == nullptr) {
        log_physics->error("Fixed world body cannot be modified");
        return;
    }
    m_body->SetRestitution(restitution);
}

void Jolt_rigid_body::begin_move()
{
    if (m_body == nullptr) {
        log_physics->error("Fixed world body cannot be modified");
        return;
    }
    SPDLOG_LOGGER_TRACE(log_physics, "{} begin move", m_debug_label);
    set_allow_sleeping(false);
    get_body_interface().ActivateBody(m_body->GetID());
}

void Jolt_rigid_body::end_move()
{
    if (m_body == nullptr) {
        log_physics->error("Fixed world body cannot be modified");
        return;
    }
    SPDLOG_LOGGER_TRACE(log_physics, "{} end move", m_debug_label);
    set_allow_sleeping(true);
    //m_body_interface.ActivateBody(m_body->GetID());
}

void Jolt_rigid_body::set_motion_mode(const Motion_mode motion_mode)
{
    if (m_body == nullptr) {
        log_physics->error("Fixed world body cannot be modified");
        return;
    }
    if (m_motion_mode == motion_mode) {
        return;
    }
    SPDLOG_LOGGER_TRACE(log_physics, "{} set motion mode = {}", m_debug_label, c_str(motion_mode));
    auto& body_interface = get_body_interface();
    if (m_body->IsActive() && (motion_mode == Motion_mode::e_static)) {
        body_interface.DeactivateBody(m_body->GetID());
    }

    m_motion_mode = motion_mode;
    m_body->SetMotionType(to_jolt(motion_mode));

    if (motion_mode == Motion_mode::e_dynamic) {
        // If body does not in world, this check will return false
        if (m_body->IsInBroadPhase()) {
            // This would asset if body was not in physics world
            body_interface.ActivateBody(m_body->GetID());
        }
    }
}

auto Jolt_rigid_body::get_center_of_mass() const -> glm::vec3
{
    return from_jolt(m_body->GetCenterOfMassPosition());
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

auto Jolt_rigid_body::get_world_transform() const -> glm::mat4
{
    if (m_body == nullptr) {
        return glm::mat4{1.0f};
    }

    // TODO Figure out difference of m_body->GetWorldTransform()
    //      vs m_body_interface.GetPositionAndRotation()
    const JPH::Mat44 jolt_transform = m_body->GetWorldTransform();
    const glm::mat4  transform      = from_jolt(jolt_transform);
    return transform;
}

void Jolt_rigid_body::set_world_transform(const Transform& transform)
{
    if (m_body == nullptr) {
        log_physics->error("Fixed world body cannot be modified");
        return;
    }

    auto& body_interface = get_body_interface();
    switch (m_motion_mode) {
        case Motion_mode::e_kinematic_non_physical: {
            SPDLOG_LOGGER_TRACE(log_physics_frame, "{} KNP SetPositionAndRotation {}", m_debug_label, transform.origin);
            body_interface.SetPositionAndRotation(
                m_body->GetID(),
                to_jolt(transform.origin),
                to_jolt(glm::quat{transform.basis}),
                JPH::EActivation::DontActivate
                //(m_motion_mode != Motion_mode::e_static)
                //    ? JPH::EActivation::Activate
                //    : JPH::EActivation::DontActivate
            );
            break;
        }

        case Motion_mode::e_kinematic_physical: {
            SPDLOG_LOGGER_TRACE(log_physics_frame, "{} KP MoveKinematic {}", m_debug_label, transform.origin);
            body_interface.MoveKinematic(
                m_body->GetID(),
                to_jolt(transform.origin),
                to_jolt(glm::quat{transform.basis}),
                1.0f / 30.0f  // TODO
            );
            break;
        }

        case Motion_mode::e_static:
        case Motion_mode::e_dynamic: {
            SPDLOG_LOGGER_TRACE(log_physics_frame, "{} S/D SetPositionAndRotation {}", m_debug_label, transform.origin);
            body_interface.SetPositionAndRotation(
                m_body->GetID(),
                to_jolt(transform.origin),
                to_jolt(glm::quat{transform.basis}),
                JPH::EActivation::DontActivate
                //(m_motion_mode != Motion_mode::e_static)
                //    ? JPH::EActivation::Activate
                //    : JPH::EActivation::DontActivate
            );
            break;
        }

        default: {
            log_physics_frame->error("bad motion mode");
            break;
        }
    }
}

auto Jolt_rigid_body::get_linear_velocity() const -> glm::vec3
{
    if (m_body == nullptr) {
        return {};
    }

    return from_jolt(m_body->GetLinearVelocity());
}

void Jolt_rigid_body::set_linear_velocity(const glm::vec3& velocity)
{
    if (m_body == nullptr) {
        log_physics->error("Fixed world body cannot be modified");
        return;
    }

    SPDLOG_LOGGER_TRACE(log_physics, "{} set linear velocity {}", m_debug_label, velocity);

    auto& body_interface = get_body_interface();
    body_interface.SetLinearVelocity(m_body->GetID(), to_jolt(velocity));
}

auto Jolt_rigid_body::get_angular_velocity() const -> glm::vec3
{
    if (m_body == nullptr) {
        return {};
    }

    return from_jolt(m_body->GetAngularVelocity());
}

void Jolt_rigid_body::set_angular_velocity(const glm::vec3& velocity)
{
    if (m_body == nullptr) {
        log_physics->error("Fixed world body cannot be modified");
        return;
    }

    SPDLOG_LOGGER_TRACE(log_physics, "{} set angular velocity {}", m_debug_label, velocity);

    auto& body_interface = get_body_interface();
    body_interface.SetAngularVelocity(m_body->GetID(), to_jolt(velocity));
}

auto Jolt_rigid_body::get_linear_damping() const -> float
{
    if (m_body == nullptr) {
        return 0.0f;
    }
    auto* motion_properties = m_body->GetMotionProperties();
    if (motion_properties == nullptr) {
        return 0.0f;
    }
    return motion_properties->GetLinearDamping();
}

void Jolt_rigid_body::set_damping(const float linear_damping, const float angular_damping)
{
    if (m_body == nullptr) {
        log_physics->error("Fixed world body cannot be modified");
        return;
    }

    SPDLOG_LOGGER_INFO(log_physics, "{} set damping linear = {}, angular = {}", m_debug_label, linear_damping, angular_damping);

    auto* motion_properties = m_body->GetMotionProperties();
    if (motion_properties == nullptr) {
        log_physics->warn("{} no motion properties");
        return;
    }
    motion_properties->SetLinearDamping(linear_damping);
    motion_properties->SetAngularDamping(angular_damping);
}

auto Jolt_rigid_body::get_angular_damping() const -> float
{
    if (m_body == nullptr) {
        return 0.0f;
    }

    auto* motion_properties = m_body->GetMotionProperties();
    if (motion_properties == nullptr) {
        return 0.0f;
    }
    return motion_properties->GetAngularDamping();
}

auto Jolt_rigid_body::get_local_inertia() const -> glm::mat4
{
    return from_jolt(m_mass_properties.mInertia);
}

auto Jolt_rigid_body::get_mass() const -> float
{
    return m_mass_properties.mMass;
}

void Jolt_rigid_body::set_mass_properties(
    const float      mass,
    const glm::mat4& inertia_tensor
)
{
    m_mass_properties.mMass    = mass;
    m_mass_properties.mInertia = to_jolt(inertia_tensor);

    if (m_body == nullptr) {
        log_physics->error("Fixed world body cannot be modified");
        return;
    }

    SPDLOG_LOGGER_TRACE(log_physics, "{} set mass = {}", m_debug_label, mass);

    if (m_body->IsStatic()) {
        return;
    }

    auto* motion_properties = m_body->GetMotionProperties();
    if (motion_properties == nullptr) {
        return;
    }
    motion_properties->SetMassProperties(JPH::EAllowedDOFs::All, m_mass_properties);
}

auto Jolt_rigid_body::get_debug_label() const -> const char*
{
    return m_debug_label.c_str();
}

auto Jolt_rigid_body::is_active() const -> bool
{
    if (m_body == nullptr) {
        return false;
    }

    // TODO Check if this would be needed or useful.
    // if (m_motion_mode != Motion_mode::e_dynamic) {
    //     return false;
    // }

    return m_body->IsActive();
}

auto Jolt_rigid_body::get_allow_sleeping() const -> bool
{
    if (m_body == nullptr) {
        return false;
    }
    return m_body->GetAllowSleeping();
}

void Jolt_rigid_body::set_allow_sleeping(const bool value)
{
    if (m_body == nullptr) {
        log_physics->error("set_allow_sleeping called for fixed body");
        return;
    }
    log_physics->trace("{} set_allow_sleeping = {}", m_debug_label, value);
    m_body->SetAllowSleeping(value);
}

auto Jolt_rigid_body::get_jolt_body() const -> JPH::Body*
{
    return (m_body != nullptr)
        ? m_body
        : &JPH::Body::sFixedToWorld;
}

void Jolt_rigid_body::set_owner(void* owner)
{
    m_owner = owner;
}

auto Jolt_rigid_body::get_owner() const -> void*
{
    return m_owner;
}

} // namespace erhe::physics

