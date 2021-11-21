#pragma once

#include "erhe/geometry/operation/geometry_operation.hpp"
#include <vector>

namespace erhe::geometry::operation
{

class Gyro
    : public Geometry_operation
{
public:
    Gyro(Geometry& src, Geometry& destination);
};

auto gyro(erhe::geometry::Geometry& source) -> erhe::geometry::Geometry;

} // namespace erhe::geometry::operation
