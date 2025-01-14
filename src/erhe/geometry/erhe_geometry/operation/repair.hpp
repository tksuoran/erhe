#pragma once

#include "erhe_geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation {

class Repair : public Geometry_operation
{
public:
    Repair(const Geometry& source, Geometry& destination);
};

[[nodiscard]] auto repair(const Geometry& source) -> Geometry;

} // namespace erhe::geometry::operation
