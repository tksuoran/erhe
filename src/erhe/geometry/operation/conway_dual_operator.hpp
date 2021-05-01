#pragma once

#include "erhe/geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation
{
class Conway_dual_operator
    : public Geometry_operation
{
public:
    Conway_dual_operator(Geometry& src, Geometry& destination);
};

auto dual(erhe::geometry::Geometry& source) -> erhe::geometry::Geometry;

} // namespace erhe::geometry::operation
