#include "erhe/physics/null/null_compound_shape.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::physics
{

auto ICollision_shape::create_compound_shape(const Compound_shape_create_info& create_info) -> ICollision_shape*
{
    return new Null_compound_shape(create_info);
}

auto ICollision_shape::create_compound_shape_shared(const Compound_shape_create_info& create_info) -> std::shared_ptr<ICollision_shape>
{
    return std::make_shared<Null_compound_shape>(create_info);
}


} // namespace erhe::physics
