#include "erhe/physics/null/null_compound_shape.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::physics
{

auto ICollision_shape::create_compound_shape() -> ICollision_shape*
{
    return new Null_compound_shape();
}

auto ICollision_shape::create_compound_shape_shared() -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Null_compound_shape>();
}


} // namespace erhe::physics
