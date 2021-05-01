#pragma once

#include "erhe/geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation
{
class Sqrt3_subdivision
    : public Geometry_operation
{
public:
    Sqrt3_subdivision(Geometry& src, Geometry& destination);
};

auto sqrt3_subdivision(erhe::geometry::Geometry& source) -> erhe::geometry::Geometry;

} // namespace erhe::geometry::operation
