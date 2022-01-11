#pragma once

#include <glm/glm.hpp>

#include <memory>

namespace erhe::physics
{

class ICollision_shape
{
public:
    virtual void calculate_local_inertia(const float mass, glm::vec3& inertia) const = 0;
};

} // namespace erhe::physics
