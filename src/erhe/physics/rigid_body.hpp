#pragma once

#include "BulletDynamics/Dynamics/btRigidBody.h"

#include <memory>

namespace erhe::scene
{
    class Node;
}

class Shape;

namespace erhe::physics
{

class Rigid_body
{
public:
    enum class Collision_mode : unsigned int
    {
        e_static,    // zero mass, immovable
        e_kinematic, // zero mass, can move non-physically
        e_dynamic    // non-zero mass, physical movement
    };

    static constexpr const char* c_collision_mode_strings[] =
    {
        "Static",
        "Kinematic",
        "Dynamic"
    };

    class Create_info
    {
    public:
        float                             mass{1.0f};
        std::shared_ptr<btCollisionShape> collision_shape;
        btVector3                         local_inertia{btScalar(0.0), btScalar(0.0), btScalar(0.0)};
    };

    Rigid_body(Create_info& create_info, btMotionState* motion_state);

    btTransform get_node_transform() const;

    void set_collision_mode(Collision_mode collision_mode);
    
    auto get_collision_mode() const -> Collision_mode
    {
        return m_collision_mode;
    }

    void set_static();

    void set_dynamic();

    void set_kinematic();

    auto get_friction() -> float
    {
        return m_friction;
        //return static_cast<float>(bullet_rigid_body.getFriction());
    }

    void set_friction(float friction)
    {
        if (m_friction == friction)
        {
            return;
        }
        m_friction = friction;
        bullet_rigid_body.setFriction(static_cast<btScalar>(m_friction));
    }

    auto get_restitution() -> float
    {
        return m_restitution;
        //return static_cast<float>(bullet_rigid_body.getRestitution());
    }

    void set_restitution(float restitution)
    {
        if (m_restitution == restitution)
        {
            return;
        }
        m_restitution = restitution;
        bullet_rigid_body.setRestitution(static_cast<btScalar>(m_restitution));
    }

    void begin_move()
    {
        bullet_rigid_body.setActivationState(DISABLE_DEACTIVATION);
    }

    void end_move()
    {
        bullet_rigid_body.setActivationState(ACTIVE_TAG);
    }

    std::shared_ptr<btCollisionShape> collision_shape;
    btRigidBody                       bullet_rigid_body;
    Collision_mode                    m_collision_mode{Collision_mode::e_kinematic};
    float                             m_friction{0.5f};
    float                             m_restitution{0.0f};
};

} // namespace erhe::physics
