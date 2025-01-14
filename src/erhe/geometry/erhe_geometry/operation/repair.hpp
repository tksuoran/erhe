#pragma once

#include "erhe_geometry/operation/geometry_operation.hpp"

namespace erhe::geometry {
    class Polygon;
}

namespace erhe::geometry::operation {

class Repair : public Geometry_operation
{
public:
    Repair(Geometry& source, Geometry& destination);
};

[[nodiscard]] auto repair(erhe::geometry::Geometry& source) -> erhe::geometry::Geometry;

} // namespace erhe::geometry::operation
