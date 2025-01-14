#pragma once

#include "erhe_geometry/operation/geometry_operation.hpp"
#include <vector>

namespace erhe::geometry::operation {

class Subdivide : public Geometry_operation
{
public:
    Subdivide(const Geometry& src, Geometry& destination);
};

[[nodiscard]] auto subdivide(const Geometry& source) -> Geometry;

} // namespace erhe::geometry::operation
