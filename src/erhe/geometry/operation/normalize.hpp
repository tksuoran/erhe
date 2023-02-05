#pragma once

#include "erhe/geometry/operation/geometry_operation.hpp"

#include <glm/glm.hpp>

namespace erhe::geometry::operation
{

class Normalize
    : public Geometry_operation
{
public:
    Normalize(Geometry& source, Geometry& destination);
};

[[nodiscard]] auto normalize(
    erhe::geometry::Geometry& source
) -> erhe::geometry::Geometry;

} // namespace erhe::geometry::operation
