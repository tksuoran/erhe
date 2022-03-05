#pragma once

#include "erhe/physics/iworld.hpp"

#include "Jolt.h"
#include <Core/TempAllocator.h>
#include <Core/JobSystemThreadPool.h>
#include <Physics/PhysicsSettings.h>
#include "Physics/PhysicsSystem.h"
#include "Physics/Collision/BroadPhase/BroadPhaseLayer.h"

#include <memory>
#include <vector>

namespace erhe::physics
{

class ICollision_shape;

class Jolt_world
    : public IWorld
{
public:
    Jolt_world();
    virtual ~Jolt_world() override;

    // Implements IWorld
    void enable_physics_updates    ()                        override;
    void disable_physics_updates   ()                        override;
    auto is_physics_updates_enabled() const -> bool          override;
    void update_fixed_step         (const double dt)         override;
    void set_gravity               (const glm::vec3 gravity) override;
    auto get_gravity               () const -> glm::vec3     override;
    void add_rigid_body            (IRigid_body* rigid_body) override;
    void remove_rigid_body         (IRigid_body* rigid_body) override;
    void add_constraint            (IConstraint* constraint) override;
    void remove_constraint         (IConstraint* constraint) override;
    void set_debug_drawer          (IDebug_draw* debug_draw) override;
    void debug_draw                ()                        override;

private:
    bool                      m_physics_enabled{false};
    glm::vec3                 m_gravity        {0.0f};
    JPH::PhysicsSystem        m_physics_system;
    JPH::TempAllocatorImpl    m_temp_allocator{10 * 1024 * 1024};
    JPH::JobSystemThreadPool  m_job_system    {JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, 10};
    std::unique_ptr<JPH::BroadPhaseLayerInterface> m_broad_phase_layer_interface;

    std::vector<IRigid_body*> m_rigid_bodies;
    std::vector<IConstraint*> m_constraints;

    std::vector<std::shared_ptr<ICollision_shape>> m_collision_shapes;
};

} // namespace erhe::physics
