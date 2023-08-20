#pragma once

#include "erhe_geometry/operation/geometry_operation.hpp"
#include <vector>

namespace erhe::geometry::operation
{

class Meta
    : public Geometry_operation
{
public:
    Meta(Geometry& src, Geometry& destination);
};

[[nodiscard]] auto meta(
    erhe::geometry::Geometry& source
) -> erhe::geometry::Geometry;

} // namespace erhe::geometry::operation
