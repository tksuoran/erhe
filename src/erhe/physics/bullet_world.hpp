#pragma once

#include "erhe/physics/iworld.hpp"

#include "btBulletDynamicsCommon.h"
#include "BulletDynamics/ConstraintSolver/btSequentialImpulseConstraintSolver.h"
#include "BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h"

#include <memory>
#include <vector>

namespace erhe::physics
{

class ICollision_shape;

class Debug_draw_adapter
    : public btIDebugDraw
{
public:
    void set_debug_draw(IDebug_draw* debug_draw);

    // Implements btIDebugDraw
	auto getDefaultColors  () const -> DefaultColors                                            override;
	void setDefaultColors  (const DefaultColors& /*colors*/)                                    override;
    void drawLine          (const btVector3& from, const btVector3& to, const btVector3& color) override;
    void draw3dText        (const btVector3& location, const char* textString)                  override;
    void setDebugMode      (int debugMode)                                                      override;
    auto getDebugMode      () const -> int                                                      override;

    void drawContactPoint(
        const btVector3& PointOnB,
        const btVector3& normalOnB,
        btScalar         distance,
        int              lifeTime,
        const btVector3& color
    )                                                                                           override;

    void reportErrorWarning(const char* warningString)                                          override;

private:
    IDebug_draw* m_debug_draw;
};

class Bullet_world
    : public IWorld
{
public:
    Bullet_world  ();
    ~Bullet_world ();
    Bullet_world  (const Bullet_world&)                  = delete;
    auto operator=(const Bullet_world&) -> Bullet_world& = delete;
    Bullet_world  (Bullet_world&&)                       = delete;
    auto operator=(Bullet_world&&) -> Bullet_world&      = delete;

    // Implements IWorld
    void enable_physics_updates    ()                        override;
    void disable_physics_updates   ()                        override;
    auto is_physics_updates_enabled() const -> bool          override;
    void update_fixed_step         (const double dt)         override;
    void set_gravity               (glm::vec3 gravity)       override;
    auto get_gravity               () const -> glm::vec3     override;
    void add_rigid_body            (IRigid_body* rigid_body) override;
    void remove_rigid_body         (IRigid_body* rigid_body) override;
    void add_constraint            (IConstraint* constraint) override;
    void remove_constraint         (IConstraint* constraint) override;
    void set_debug_drawer          (IDebug_draw* debug_draw) override;
    void debug_draw                ()                        override;

private:
    bool                                m_physics_enabled{false};
    Debug_draw_adapter                  m_debug_draw_adapter;
    btDefaultCollisionConfiguration     m_bullet_collision_configuration;
    btCollisionDispatcher               m_bullet_collision_dispatcher;
    btDbvtBroadphase                    m_bullet_broadphase_interface;
    btSequentialImpulseConstraintSolver m_bullet_impulse_constraint_solver;
    btDiscreteDynamicsWorld             m_bullet_dynamics_world;

    std::vector<std::shared_ptr<ICollision_shape>> m_collision_shapes;
};

} // namespace erhe::physics
