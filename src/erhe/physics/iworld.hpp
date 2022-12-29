#pragma once

#include <glm/glm.hpp>

#include <memory>

namespace erhe::physics
{

class IConstraint;
class IDebug_draw;
class IMotion_state;
class IRigid_body;
class IRigid_body_create_info;

class IWorld
{
public:
    virtual ~IWorld() noexcept;

    [[nodiscard]] static auto create       () -> IWorld*;
    [[nodiscard]] static auto create_shared() -> std::shared_ptr<IWorld>;
    [[nodiscard]] static auto create_unique() -> std::unique_ptr<IWorld>;

    [[nodiscard]] virtual auto is_physics_updates_enabled() const -> bool      = 0;
    [[nodiscard]] virtual auto get_gravity               () const -> glm::vec3 = 0;
    virtual void enable_physics_updates ()                         = 0;
    virtual void disable_physics_updates()                         = 0;
    virtual void update_fixed_step      (double dt)                = 0;
    virtual void add_rigid_body         (IRigid_body* rigid_body)  = 0;
    virtual void remove_rigid_body      (IRigid_body* rigid_body)  = 0;
    virtual void add_constraint         (IConstraint* constraint)  = 0;
    virtual void remove_constraint      (IConstraint* constraint)  = 0;
    virtual void set_gravity            (const glm::vec3& gravity) = 0;
    virtual void set_debug_drawer       (IDebug_draw* debug_draw)  = 0;
    virtual void debug_draw             ()                         = 0;
    virtual void sanity_check           ()                         = 0;
};

} // namespace erhe::physics
