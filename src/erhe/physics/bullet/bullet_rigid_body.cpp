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

auto IRigid_body::create_rigid_body(
    const IRigid_body_create_info& create_info,
    IMotion_state*                 motion_state
) -> IRigid_body*
{
    return new Bullet_rigid_body(create_info, motion_state);
}

auto IRigid_body::create_rigid_body_shared(
    const IRigid_body_create_info& create_info,
    IMotion_state*                 motion_state
) -> std::shared_ptr<IRigid_body>
{
    return std::make_shared<Bullet_rigid_body>(create_info, motion_state);
}

namespace {

auto to_bullet(
    const IRigid_body_create_info& create_info,
    Motion_state_adapter&          motion_state_adapter
) -> btRigidBody::btRigidBodyConstructionInfo
{
    btRigidBody::btRigidBodyConstructionInfo bullet_create_info{
        static_cast<btScalar>(create_info.mass),
        &motion_state_adapter,
        reinterpret_cast<Bullet_collision_shape*>(
            create_info.collision_shape.get()
        )->get_bullet_collision_shape(),
        erhe::physics::to_bullet(
            glm::vec3{
                create_info.local_inertia[0][0],
                create_info.local_inertia[1][1],
                create_info.local_inertia[2][2]
            }
        )
    };

    bullet_create_info.m_friction       = create_info.friction;
    bullet_create_info.m_restitution    = create_info.restitution;
    bullet_create_info.m_linearDamping  = create_info.linear_damping;
    bullet_create_info.m_angularDamping = create_info.angular_damping;

    return bullet_create_info;
}

}

Bullet_rigid_body::Bullet_rigid_body(
    const IRigid_body_create_info& create_info,
    IMotion_state*                 motion_state
)
    : m_motion_state_adapter{motion_state}
    , m_collision_shape     {create_info.collision_shape}
    , m_bullet_rigid_body   {to_bullet(create_info, m_motion_state_adapter)}
    , m_motion_mode         {motion_state->get_motion_mode()}
    , m_debug_label         {create_info.debug_label}
{
    m_bullet_rigid_body.setDamping(0.02f, 0.02f);

    m_bullet_rigid_body.setFriction(0.5f);
    m_bullet_rigid_body.setRollingFriction(0.1f);
}

IRigid_body::~IRigid_body()
{
}

Bullet_rigid_body::~Bullet_rigid_body()
{
}

auto Bullet_rigid_body::get_motion_mode() const -> Motion_mode
{
    return m_motion_mode;
}

void Bullet_rigid_body::set_collision_shape(
    const std::shared_ptr<ICollision_shape>& collision_shape
)
{
    m_bullet_rigid_body.setCollisionShape(
        dynamic_cast<Bullet_collision_shape*>(
            collision_shape.get()
        )->get_bullet_collision_shape()
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
        //using enum Motion_mode;
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

void Bullet_rigid_body::set_damping(
    const float linear_damping,
    const float angular_damping
)
{
    m_bullet_rigid_body.setDamping(linear_damping, angular_damping);
}

auto Bullet_rigid_body::get_angular_damping () const -> float
{
    return static_cast<float>(m_bullet_rigid_body.getAngularDamping());
}

auto Bullet_rigid_body::get_local_inertia() const -> glm::mat4
{
    const auto local_inertia = from_bullet(m_bullet_rigid_body.getLocalInertia());
    return glm::mat4{
        glm::vec4{local_inertia.x, 0.0f, 0.0f, 0.0f},
        glm::vec4{0.0f, local_inertia.y, 0.0f, 0.0f},
        glm::vec4{0.0f, 0.0, local_inertia.z, 0.0f},
        glm::vec4{0.0f, 0.0, 0.0, 1.0f}
    };
}

auto Bullet_rigid_body::get_mass() const -> float
{
    return static_cast<float>(m_bullet_rigid_body.getMass());
}

auto Bullet_rigid_body::get_center_of_mass_transform() const -> Transform
{
    return from_bullet(m_bullet_rigid_body.getCenterOfMassTransform());
}

auto Bullet_rigid_body::get_debug_label() const -> const char*
{
    return m_debug_label.c_str();
}

void Bullet_rigid_body::set_mass_properties(
    const float     mass,
    const glm::mat4 local_inertia
)
{
    const glm::vec3 inertia{
        local_inertia[0][0],
        local_inertia[1][1],
        local_inertia[2][2]
    };
    m_bullet_rigid_body.setMassProps(
        static_cast<btScalar>(mass),
        to_bullet(inertia)
    );
}

auto Bullet_rigid_body::get_bullet_rigid_body() -> btRigidBody*
{
    return &m_bullet_rigid_body;
}


} // namespace erhe::physics

