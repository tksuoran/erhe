#pragma once

#include <glm/glm.hpp>

#include <memory>

namespace erhe::physics
{

class IConstraint;
class IDebug_draw;
class IRigid_body;

class IWorld
{
public:
    virtual ~IWorld(){}

    static [[nodiscard]] auto create() -> IWorld*;
    static [[nodiscard]] auto create_shared() -> std::shared_ptr<IWorld>;
    static [[nodiscard]] auto create_unique() -> std::unique_ptr<IWorld>;

    virtual [[nodiscard]] auto is_physics_updates_enabled() const -> bool      = 0;
    virtual [[nodiscard]] auto get_gravity               () const -> glm::vec3 = 0;
    virtual void enable_physics_updates ()                        = 0;
    virtual void disable_physics_updates()                        = 0;
    virtual void update_fixed_step      (const double dt)         = 0;
    virtual void add_rigid_body         (IRigid_body* rigid_body) = 0;
    virtual void remove_rigid_body      (IRigid_body* rigid_body) = 0;
    virtual void add_constraint         (IConstraint* constraint) = 0;
    virtual void remove_constraint      (IConstraint* constraint) = 0;
    virtual void set_gravity            (const glm::vec3 gravity) = 0;
    virtual void set_debug_drawer       (IDebug_draw* debug_draw) = 0;
    virtual void debug_draw             ()                        = 0;
};

} // namespace erhe::physics
