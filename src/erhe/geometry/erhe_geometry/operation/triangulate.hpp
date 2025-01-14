#pragma once

#include "erhe_geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation {

class Triangulate : public Geometry_operation
{
public:
    Triangulate(const Geometry& src, Geometry& destination);
};

[[nodiscard]] auto triangulate(const Geometry& source) -> Geometry;

} // namespace erhe::geometry::operation
