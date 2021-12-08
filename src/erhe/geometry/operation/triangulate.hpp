#pragma once

#include "erhe/geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation
{

class Triangulate
    : public Geometry_operation
{
public:
    Triangulate(Geometry& src, Geometry& destination);
};

[[nodiscard]] auto triangulate(
    erhe::geometry::Geometry& source
) -> erhe::geometry::Geometry;

} // namespace erhe::geometry::operation
