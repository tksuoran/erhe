#include "erhe_physics/null/null_scaled_shape.hpp"

namespace erhe::physics {

auto ICollision_shape::create_scaled_shape(const std::shared_ptr<ICollision_shape>& shape, const glm::vec3 scale) -> ICollision_shape*
{
    return new Null_scaled_shape(shape, scale);
}

auto ICollision_shape::create_scaled_shape_shared(const std::shared_ptr<ICollision_shape>& shape, const glm::vec3 scale) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Null_scaled_shape>(shape, scale);
}

} // namespace erhe::physics
