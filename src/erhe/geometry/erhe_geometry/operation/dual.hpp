#pragma once

#include "erhe_geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation {

// Conway dual operator
class Dual : public Geometry_operation
{
public:
    Dual(const Geometry& source, Geometry& destination);
};

[[nodiscard]] auto dual(const Geometry& source) -> Geometry;

} // namespace erhe::geometry::operation
