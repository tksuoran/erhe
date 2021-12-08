#pragma once

#include "erhe/physics/transform.hpp"

namespace erhe::physics
{

class IMotion_state
{
public:
    virtual [[nodiscard]] auto get_world_from_rigidbody() const -> Transform = 0;

    virtual void set_world_from_rigidbody(const Transform transform) = 0;
};

} // namespace erhe::physics
