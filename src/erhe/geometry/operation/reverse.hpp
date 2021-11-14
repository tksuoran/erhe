#pragma once

#include "erhe/geometry/operation/geometry_operation.hpp"

#include <glm/glm.hpp>

namespace erhe::geometry::operation
{

class Reverse
    : public Geometry_operation
{
public:
    explicit Reverse(
        Geometry& source,
        Geometry& destination
    );
};

auto reverse(erhe::geometry::Geometry& source)
-> erhe::geometry::Geometry;

} // namespace erhe::geometry::operation
