#pragma once

#include "erhe/geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation
{

// Conway dual operator
class Dual
    : public Geometry_operation
{
public:
    Dual(Geometry& source, Geometry& destination, bool post_process = true);
};

[[nodiscard]] auto dual(
    erhe::geometry::Geometry& source
) -> erhe::geometry::Geometry;

} // namespace erhe::geometry::operation
