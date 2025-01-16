#pragma once

#include "erhe_geometry/operation/geometry_operation.hpp"

#include <glm/glm.hpp>

namespace erhe::geometry::operation {

class Normalize : public Geometry_operation
{
public:
    Normalize(const Geometry& source, Geometry& destination);
};

[[nodiscard]] auto normalize(const Geometry& source) -> Geometry;

} // namespace erhe::geometry::operation
