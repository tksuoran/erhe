#pragma once

#include "erhe/physics/transform.hpp"

namespace erhe::physics
{

enum class Motion_mode : unsigned int
{
    e_static,    // zero mass, immovable
    e_kinematic, // zero mass, can move non-physically
    e_dynamic    // non-zero mass, physical movement
};

static constexpr const char* c_motion_mode_strings[] =
{
    "Static",
    "Kinematic",
    "Dynamic"
};

class IMotion_state
{
public:
    [[nodiscard]] virtual auto get_world_from_rigidbody() const -> Transform   = 0;
    [[nodiscard]] virtual auto get_motion_mode         () const -> Motion_mode = 0;

    virtual void set_world_from_rigidbody(const Transform   transform  ) = 0;
    virtual void set_motion_mode         (const Motion_mode motion_mode) = 0;
};

} // namespace erhe::physics
