#pragma once

#include "erhe_geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation {

// Conway ambo operator
class Ambo : public Geometry_operation
{
public:
    Ambo(const Geometry& source, Geometry& destination);
};

[[nodiscard]] auto ambo(const Geometry& source) -> Geometry;

} // namespace erhe::geometry::operation
