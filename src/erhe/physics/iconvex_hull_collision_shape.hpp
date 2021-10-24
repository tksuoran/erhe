#pragma once

#include <glm/glm.hpp>

#include <memory>

namespace erhe::physics
{

class ICollision_shape
{
public:

    static auto create() -> ICollision_shape*;
    static auto create_shared() -> std::shared_ptr<ICollision_shape>;

    virtual void calculate_local_inertia(float mass, glm::vec3& inertia) const = 0;
};

} // namespace erhe::physics
