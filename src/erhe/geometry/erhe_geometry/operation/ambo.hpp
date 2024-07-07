#pragma once

#include "erhe_geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation {

// Conway ambo operator
class Ambo : public Geometry_operation
{
public:
    Ambo(Geometry& source, Geometry& destination);
};

[[nodiscard]] auto ambo(erhe::geometry::Geometry& source) -> erhe::geometry::Geometry;

} // namespace erhe::geometry::operation
