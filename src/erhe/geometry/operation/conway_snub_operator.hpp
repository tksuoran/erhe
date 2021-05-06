#pragma once

#include "erhe/geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation
{

class Conway_snub_operator
    : public Geometry_operation
{
public:
    Conway_snub_operator(Geometry& src, Geometry& destination);
};

auto snub(erhe::geometry::Geometry& source) -> erhe::geometry::Geometry;

} // namespace erhe::geometry::operation
