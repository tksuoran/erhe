#pragma once

#include "erhe/geometry/operation/geometry_operation.hpp"
#include <vector>

namespace erhe::geometry::operation
{

class Subdivide
    : public Geometry_operation
{
public:
    Subdivide(Geometry& src, Geometry& destination);
};

[[nodiscard]] auto subdivide(
    erhe::geometry::Geometry& source
) -> erhe::geometry::Geometry;

} // namespace erhe::geometry::operation
