#pragma once

#include "erhe/physics/iworld.hpp"

#include <memory>
#include <vector>

namespace erhe::physics
{

class ICollision_shape;

class Null_world
    : public IWorld
{
public:
    virtual ~Null_world() override;

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
    glm::vec3                 m_gravity;

    std::vector<IRigid_body*> m_rigid_bodies;
    std::vector<IConstraint*> m_constraints;

    std::vector<std::shared_ptr<ICollision_shape>> m_collision_shapes;
};

} // namespace erhe::physics
