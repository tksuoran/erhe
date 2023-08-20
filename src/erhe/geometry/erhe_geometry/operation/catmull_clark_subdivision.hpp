#pragma once

#include "erhe_geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation
{
class Catmull_clark_subdivision
    : public Geometry_operation
{
public:
    Catmull_clark_subdivision(Geometry& src, Geometry& destination);
};

[[nodiscard]] auto catmull_clark_subdivision(
    erhe::geometry::Geometry& source
) -> erhe::geometry::Geometry;

} // namespace erhe::geometry::operation
