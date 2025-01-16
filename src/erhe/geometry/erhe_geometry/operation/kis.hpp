#pragma once

#include "erhe_geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation {

class Kis : public Geometry_operation
{
public:
    Kis(const Geometry& src, Geometry& destination);
};

[[nodiscard]] auto kis(const Geometry& source) -> Geometry;

} // namespace erhe::geometry::operation
