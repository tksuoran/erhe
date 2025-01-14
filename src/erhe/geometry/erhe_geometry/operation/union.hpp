#pragma once

#include "erhe_geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation {

class Union : public Geometry_operation
{
public:
    Union(const Geometry& lhs, const Geometry& rhs, Geometry& destination);
};

[[nodiscard]] auto union_(const Geometry& lhs, const Geometry& rhs) -> Geometry;

} // namespace erhe::geometry::operation
