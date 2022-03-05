#include "erhe/physics/jolt/jolt_uniform_scaling_shape.hpp"

namespace erhe::physics
{

auto ICollision_shape::create_uniform_scaling_shape(ICollision_shape* shape, const float scale) -> ICollision_shape*
{
    return new Jolt_uniform_scaling_shape(shape, scale);
}

auto ICollision_shape::create_uniform_scaling_shape_shared(ICollision_shape* shape, const float scale) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Jolt_uniform_scaling_shape>(shape, scale);
}

} // namespace erhe::physics
