#pragma once

#include "erhe/geometry/operation/geometry_operation.hpp"

#include <glm/glm.hpp>

namespace erhe::geometry::operation
{

class Weld
    : public Geometry_operation
{
public:
    explicit Weld(Geometry& source, Geometry& destination);
};

[[nodiscard]] auto weld(
    erhe::geometry::Geometry& source
) -> erhe::geometry::Geometry;

} // namespace erhe::geometry::operation
