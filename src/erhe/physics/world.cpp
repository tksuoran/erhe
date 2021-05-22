#include "erhe/physics/world.hpp"
#include "erhe/physics/physics_system.hpp"
#include "erhe/physics/log.hpp"
#include "erhe/toolkit/verify.hpp"

#include "erhe/toolkit/tracy_client.hpp"

namespace erhe::physics
{

World::World()
    : bullet_collision_configuration{}
    , bullet_collision_dispatcher{&bullet_collision_configuration}
    , bullet_broadphase_interface{}
    , bullet_impulse_constraint_solver{}
    , bullet_dynamics_world{&bullet_collision_dispatcher,
                            &bullet_broadphase_interface,
                            &bullet_impulse_constraint_solver,
                            &bullet_collision_configuration}
{
    log_physics.trace("{}\n", __func__);
    bullet_dynamics_world.setGravity(btVector3(0, -10, 0));
}

World::~World()
{
    log_physics.trace("{}\n", __func__);
}

void World::update_fixed_step(double dt)
{
    ZoneScoped;

    if (!physics_enabled)
    {
        return;
    }

    const auto timeStep = static_cast<btScalar>(dt);
    int maxSubSteps{1};

    bullet_dynamics_world.stepSimulation(timeStep, maxSubSteps, timeStep);
}

void World::add_rigid_body(Rigid_body* rigid_body)
{
    VERIFY(rigid_body != nullptr);
    bullet_dynamics_world.addRigidBody(&rigid_body->bullet_rigid_body);
}

void World::remove_rigid_body(Rigid_body* rigid_body)
{
    VERIFY(rigid_body != nullptr);
    bullet_dynamics_world.removeRigidBody(&rigid_body->bullet_rigid_body);
}


} // namespace erhe::physics
