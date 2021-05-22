#include "erhe/physics/rigid_body.hpp"
#include "erhe/scene/node.hpp"

#include <glm/glm.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace erhe::physics
{


Rigid_body::Rigid_body(Create_info& create_info, btMotionState* motion_state)
    : collision_shape  {create_info.collision_shape}
    , bullet_rigid_body{
        btRigidBody::btRigidBodyConstructionInfo{
            create_info.mass,
            motion_state,
            collision_shape.get(),
            create_info.local_inertia
        }
    }
    , m_collision_mode{create_info.mass > 0.0f ? Collision_mode::e_dynamic
                                               : Collision_mode::e_static}
{
}

void Rigid_body::set_static()
{
    int flags = bullet_rigid_body.getCollisionFlags();
    flags |=  btCollisionObject::CF_STATIC_OBJECT;
    flags &= ~btCollisionObject::CF_KINEMATIC_OBJECT;
    bullet_rigid_body.setCollisionFlags(flags);
}

void Rigid_body::set_kinematic()
{
    int flags = bullet_rigid_body.getCollisionFlags();
    flags &= ~btCollisionObject::CF_STATIC_OBJECT;
    flags |=  btCollisionObject::CF_KINEMATIC_OBJECT;
    bullet_rigid_body.setCollisionFlags(flags);
}

void Rigid_body::set_dynamic()
{
    int flags = bullet_rigid_body.getCollisionFlags();
    flags &= ~btCollisionObject::CF_STATIC_OBJECT;
    flags &= ~btCollisionObject::CF_KINEMATIC_OBJECT;
    bullet_rigid_body.setCollisionFlags(flags);
    bullet_rigid_body.activate(true);
}


void Rigid_body::set_collision_mode(Collision_mode collision_mode)
{
    if (m_collision_mode == collision_mode)
    {
        return;
    }

    m_collision_mode = collision_mode;
    switch (m_collision_mode)
    {
        case Collision_mode::e_static:    set_static();    break;
        case Collision_mode::e_kinematic: set_kinematic(); break;
        case Collision_mode::e_dynamic:   set_dynamic();   break;
        default: break;
    }
}

} // namespace erhe::physics

