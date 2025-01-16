#pragma once

#include "erhe_geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation {

class Reverse : public Geometry_operation
{
public:
    Reverse(const Geometry& source, Geometry& destination);
};

[[nodiscard]] auto reverse(const Geometry& source) -> Geometry;

} // namespace erhe::geometry::operation
