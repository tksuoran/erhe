#pragma once

#include "erhe_geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation
{

class Truncate
    : public Geometry_operation
{
public:
    Truncate(Geometry& source, Geometry& destination);
};

[[nodiscard]] auto truncate(
    erhe::geometry::Geometry& source
) -> erhe::geometry::Geometry;

} // namespace erhe::geometry::operation
