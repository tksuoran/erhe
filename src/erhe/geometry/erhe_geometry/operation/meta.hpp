#pragma once

#include "erhe_geometry/operation/geometry_operation.hpp"
#include <vector>

namespace erhe::geometry::operation {

class Meta : public Geometry_operation
{
public:
    Meta(const Geometry& src, Geometry& destination);
};

[[nodiscard]] auto meta(const Geometry& source) -> Geometry;

} // namespace erhe::geometry::operation
