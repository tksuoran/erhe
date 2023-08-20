#pragma once

#include "erhe_geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation
{

class Kis
    : public Geometry_operation
{
public:
    Kis(Geometry& src, Geometry& destination);
};

[[nodiscard]] auto kis(
    erhe::geometry::Geometry& source
) -> erhe::geometry::Geometry;

} // namespace erhe::geometry::operation
