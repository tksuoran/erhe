#pragma once

#include "erhe_geometry/operation/geometry_operation.hpp"

#include <glm/glm.hpp>

namespace erhe::geometry::operation {

class Clone : public Geometry_operation
{
public:
    Clone(Geometry& source, Geometry& destination, const glm::mat4& transform);
};

[[nodiscard]] auto clone(erhe::geometry::Geometry& source, const glm::mat4& transform) -> erhe::geometry::Geometry;

} // namespace erhe::geometry::operation
