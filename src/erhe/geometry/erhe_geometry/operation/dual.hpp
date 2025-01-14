#pragma once

#include "erhe_geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation {

// Conway dual operator
class Dual : public Geometry_operation
{
public:
    Dual(Geometry& source, Geometry& destination);
};

[[nodiscard]] auto dual(erhe::geometry::Geometry& source) -> erhe::geometry::Geometry;

} // namespace erhe::geometry::operation
