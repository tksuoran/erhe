#pragma once

#include "erhe/physics/transform.hpp"

namespace erhe::physics
{

enum class Motion_mode : unsigned int
{
    e_invalid                = 0,
    e_static                 = 1, // immovable
    e_kinematic_non_physical = 2, // movable from scene graph (instant, does not create kinetic energy), not movable from physics simulation
    e_kinematic_physical     = 3, // movable from scene graph (creates kinetic energy), not movable from physics simulation
    e_dynamic                = 4  // not movable from scene graph, movable from physics simulation
};

static constexpr const char* c_motion_mode_strings[] =
{
    "Static",
    "Kinematic Non-Physical",
    "Kinematic Physical",
    "Dynamic"
};

class IMotion_state
{
public:
    [[nodiscard]] virtual auto get_world_from_rigidbody() const -> Transform   = 0;
    [[nodiscard]] virtual auto get_motion_mode         () const -> Motion_mode = 0;

    virtual void set_world_from_rigidbody(const glm::mat4&  transform  ) = 0;
    virtual void set_world_from_rigidbody(const Transform   transform  ) = 0;
    virtual void set_motion_mode         (const Motion_mode motion_mode) = 0;
};

} // namespace erhe::physics
