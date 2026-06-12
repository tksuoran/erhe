#include "erhe_physics/null/null_offset_center_of_mass_shape.hpp"

namespace erhe::physics {

auto ICollision_shape::create_offset_center_of_mass_shape(const std::shared_ptr<ICollision_shape>& shape, const glm::vec3 offset) -> ICollision_shape*
{
    return new Null_offset_center_of_mass_shape(shape, offset);
}

auto ICollision_shape::create_offset_center_of_mass_shape_shared(const std::shared_ptr<ICollision_shape>& shape, const glm::vec3 offset) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Null_offset_center_of_mass_shape>(shape, offset);
}

} // namespace erhe::physics
