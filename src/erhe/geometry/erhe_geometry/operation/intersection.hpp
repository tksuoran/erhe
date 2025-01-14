#pragma once

#include "erhe_geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation {

class Intersection : public Geometry_operation
{
public:
    Intersection(const Geometry& lhs, const Geometry& rhs, Geometry& destination);
};

[[nodiscard]] auto intersection(const Geometry& lhs, const Geometry& rhs) -> Geometry;

} // namespace erhe::geometry::operation
