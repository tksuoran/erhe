#pragma once

#include "erhe_geometry/operation/geometry_operation.hpp"
#include <vector>

namespace erhe::geometry::operation {

class Gyro : public Geometry_operation
{
public:
    Gyro(const Geometry& src, Geometry& destination);
};

[[nodiscard]] auto gyro(const Geometry& source) -> Geometry;

} // namespace erhe::geometry::operation
