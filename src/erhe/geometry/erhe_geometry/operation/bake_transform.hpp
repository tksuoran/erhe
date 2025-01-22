#pragma once

#include "erhe_geometry/operation/geometry_operation.hpp"

#include <glm/glm.hpp>

namespace erhe::geometry::operation {

class Bake_transform : public Geometry_operation
{
public:
    Bake_transform(const Geometry& source, const glm::mat4& transform, Geometry& destination);
};

[[nodiscard]] auto bake_transform(const Geometry& source, const glm::mat4& transform) -> Geometry;

} // namespace erhe::geometry::operation
