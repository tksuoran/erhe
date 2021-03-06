#pragma once

#include "erhe/physics/rigid_body.hpp"

#include "btBulletDynamicsCommon.h"
#include "BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolver.h"
#include "BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h"

#include <vector>

namespace erhe::physics
{

class Rigid_body;

class World final
{
public:
    World         ();
    ~World        ();
    World         (const World&)           = delete;
    auto operator=(const World&) -> World& = delete;
    World         (World&&)                = delete;
    auto operator=(World&&) -> World&      = delete;

    void update_fixed_step(double dt);
    void add_rigid_body   (Rigid_body* rigid_body);
    void remove_rigid_body(Rigid_body* rigid_body);

    bool                                physics_enabled{false};
    btDefaultCollisionConfiguration     bullet_collision_configuration;
    btCollisionDispatcher               bullet_collision_dispatcher;
    btDbvtBroadphase                    bullet_broadphase_interface;
    btSequentialImpulseConstraintSolver bullet_impulse_constraint_solver;
    btDiscreteDynamicsWorld             bullet_dynamics_world;

    std::vector<std::shared_ptr<btCollisionShape>> collision_shapes;
};

} // namespace erhe::physics
