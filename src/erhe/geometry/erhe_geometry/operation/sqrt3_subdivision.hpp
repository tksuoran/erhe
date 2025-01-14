#pragma once

#include "erhe_geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation {

class Sqrt3_subdivision : public Geometry_operation
{
public:
    Sqrt3_subdivision(const Geometry& src, Geometry& destination);
};

[[nodiscard]] auto sqrt3_subdivision(const Geometry& source) -> Geometry;

} // namespace erhe::geometry::operation
