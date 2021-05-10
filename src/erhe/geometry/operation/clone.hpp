#pragma once

#include "erhe/geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation
{

class Clone
    : public Geometry_operation
{
public:
    explicit Clone(Geometry& src, Geometry& destination);
};

auto clone(erhe::geometry::Geometry& source) -> erhe::geometry::Geometry;

} // namespace erhe::geometry::operation
