#pragma once

#include "erhe_geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation {

class Difference : public Geometry_operation
{
public:
    Difference(const Geometry& lhs, const Geometry& rhs, Geometry& destination);
};

[[nodiscard]] auto difference(const Geometry& lhs, const Geometry& rhs) -> Geometry;

} // namespace erhe::geometry::operation
