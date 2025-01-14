#pragma once

#include "erhe_geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation {

class Join : public Geometry_operation
{
public:
    Join(const Geometry& src, Geometry& destination);
};

[[nodiscard]] auto join(const Geometry& source) -> Geometry;

} // namespace erhe::geometry::operation
