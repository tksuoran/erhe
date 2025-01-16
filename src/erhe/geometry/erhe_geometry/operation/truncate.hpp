#pragma once

#include "erhe_geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation {

class Truncate : public Geometry_operation
{
public:
    Truncate(const Geometry& source, Geometry& destination);
};

[[nodiscard]] auto truncate(const Geometry& source) -> Geometry;

} // namespace erhe::geometry::operation
