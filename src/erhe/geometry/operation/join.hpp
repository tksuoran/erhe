#pragma once

#include "erhe/geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation
{

class Join
    : public Geometry_operation
{
public:
    Join(Geometry& src, Geometry& destination);
};

[[nodiscard]] auto join(
    erhe::geometry::Geometry& source
) -> erhe::geometry::Geometry;

} // namespace erhe::geometry::operation
