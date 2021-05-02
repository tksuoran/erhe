#pragma once

#include "erhe/geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation
{

class Conway_truncate_operator
    : public Geometry_operation
{
public:
    Conway_truncate_operator(Geometry& src, Geometry& destination);
};

auto truncate(erhe::geometry::Geometry& source) -> erhe::geometry::Geometry;

} // namespace erhe::geometry::operation
