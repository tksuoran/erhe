#include "erhe/physics/bullet/bullet_rigid_body.hpp"
#include "erhe/physics/bullet/bullet_collision_shape.hpp"
#include "erhe/physics/bullet/glm_conversions.hpp"
#include "erhe/physics/imotion_state.hpp"
#include "erhe/scene/node.hpp"

#include <glm/glm.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace erhe::physics
{

Motion_state_adapter::Motion_state_adapter(IMotion_state* motion_state)
    : m_motion_state{motion_state}
{
}

void Motion_state_adapter::getWorldTransform(btTransform& worldTrans) const
{
    Transform world_transform = m_motion_state->get_world_from_rigidbody();
    worldTrans = to_bullet(world_transform);
}

void Motion_state_adapter::setWorldTransform(const btTransform& worldTrans)
{
    m_motion_state->set_world_from_rigidbody(from_bullet(worldTrans));
}

auto IRigid_body::create(
    IRigid_body_create_info& create_info,
    IMotion_state*           motion_state
) -> IRigid_body*
{
    return new Bullet_rigid_body(create_info, motion_state);
}

auto IRigid_body::create_shared(
    IRigid_body_create_info& create_info,
    IMotion_state*           motion_state
) -> std::shared_ptr<IRigid_body>
{
    return std::make_shared<Bullet_rigid_body>(create_info, motion_state);
}

Bullet_rigid_body::Bullet_rigid_body(
    IRigid_body_create_info& create_info,
    IMotion_state*           motion_state
)
    : m_motion_state_adapter{motion_state}
    , m_collision_shape{create_info.collision_shape}
    , m_bullet_rigid_body{
        btRigidBody::btRigidBodyConstructionInfo{
            static_cast<btScalar>(create_info.mass),
            &m_motion_state_adapter,
            dynamic_cast<Bullet_collision_shape*>(m_collision_shape.get())->get_bullet_collision_shape(),
            to_bullet(create_info.local_inertia)
        }
    }
    , m_motion_mode{
        (create_info.mass > 0.0f)
            ? Motion_mode::e_dynamic
            : Motion_mode::e_static
    }
{
    m_bullet_rigid_body.setDamping(0.02f, 0.02f);

    m_bullet_rigid_body.setFriction(0.5);
    m_bullet_rigid_body.setRollingFriction(0.1);
}

Bullet_rigid_body::~Bullet_rigid_body()
{
}

auto Bullet_rigid_body::get_motion_mode() const -> Motion_mode
{
    return m_motion_mode;
}

void Bullet_rigid_body::set_collision_shape(const std::shared_ptr<ICollision_shape>& collision_shape)
{
    m_bullet_rigid_body.setCollisionShape(
        dynamic_cast<Bullet_collision_shape*>(collision_shape.get())->get_bullet_collision_shape()
    );
    m_collision_shape = collision_shape;
}

auto Bullet_rigid_body::get_collision_shape() const -> std::shared_ptr<ICollision_shape>
{
    return m_collision_shape;
}

auto Bullet_rigid_body::get_friction() const -> float
{
    return static_cast<float>(m_bullet_rigid_body.getFriction());
}

void Bullet_rigid_body::set_friction(const float friction)
{
    m_bullet_rigid_body.setFriction(static_cast<btScalar>(friction));
}

auto Bullet_rigid_body::get_rolling_friction() const -> float
{
    return static_cast<float>(m_bullet_rigid_body.getRollingFriction());
}

void Bullet_rigid_body::set_rolling_friction(const float rolling_friction)
{
    m_bullet_rigid_body.setRollingFriction(static_cast<btScalar>(rolling_friction));
}

auto Bullet_rigid_body::get_restitution() const -> float
{
    return static_cast<float>(m_bullet_rigid_body.getRestitution());
}

void Bullet_rigid_body::set_restitution(float restitution)
{
    m_bullet_rigid_body.setRestitution(static_cast<btScalar>(restitution));
}

void Bullet_rigid_body::begin_move()
{
    m_bullet_rigid_body.setActivationState(DISABLE_DEACTIVATION);
}

void Bullet_rigid_body::end_move()
{
    m_bullet_rigid_body.setActivationState(ACTIVE_TAG);
}

void Bullet_rigid_body::set_motion_mode(const Motion_mode motion_mode)
{
    if (m_motion_mode == motion_mode)
    {
        return;
    }

    m_motion_mode = motion_mode;

    int flags = m_bullet_rigid_body.getCollisionFlags();

    switch (motion_mode)
    {
        case Motion_mode::e_static:
        {
            flags |=  btCollisionObject::CF_STATIC_OBJECT;
            flags &= ~btCollisionObject::CF_KINEMATIC_OBJECT;
            break;
        }
        case Motion_mode::e_kinematic:
        {
            flags &= ~btCollisionObject::CF_STATIC_OBJECT;
            flags |=  btCollisionObject::CF_KINEMATIC_OBJECT;
            break;
        }
        case Motion_mode::e_dynamic:
        {
            flags &= ~btCollisionObject::CF_STATIC_OBJECT;
            flags &= ~btCollisionObject::CF_KINEMATIC_OBJECT;
            break;
        }
    }
    m_bullet_rigid_body.setCollisionFlags(flags);
}

void Bullet_rigid_body::set_center_of_mass_transform(const Transform transform)
{
    m_bullet_rigid_body.setCenterOfMassTransform(to_bullet(transform));
}

void Bullet_rigid_body::set_world_transform(const Transform transform)
{
    m_bullet_rigid_body.setWorldTransform(to_bullet(transform));
}

void Bullet_rigid_body::set_linear_velocity(const glm::vec3 velocity)
{
    m_bullet_rigid_body.setLinearVelocity(to_bullet(velocity));
}

void Bullet_rigid_body::set_angular_velocity(const glm::vec3 velocity)
{
    m_bullet_rigid_body.setAngularVelocity(to_bullet(velocity));
}

auto Bullet_rigid_body::get_linear_damping() const -> float
{
    return static_cast<float>(m_bullet_rigid_body.getLinearDamping());
}

void Bullet_rigid_body::set_damping(const float linear_damping, const float angular_damping)
{
    m_bullet_rigid_body.setDamping(linear_damping, angular_damping);
}

auto Bullet_rigid_body::get_angular_damping () const -> float
{
    return static_cast<float>(m_bullet_rigid_body.getAngularDamping());
}

auto Bullet_rigid_body::get_local_inertia() const -> glm::vec3
{
    return from_bullet(m_bullet_rigid_body.getLocalInertia());
}

auto Bullet_rigid_body::get_mass() const -> float
{
    return static_cast<float>(m_bullet_rigid_body.getMass());
}

void Bullet_rigid_body::set_mass_properties(const float mass, const glm::vec3 local_inertia)
{
    m_bullet_rigid_body.setMassProps(static_cast<btScalar>(mass), to_bullet(local_inertia));
}

auto Bullet_rigid_body::get_bullet_rigid_body() -> btRigidBody*
{
    return &m_bullet_rigid_body;
}


} // namespace erhe::physics

