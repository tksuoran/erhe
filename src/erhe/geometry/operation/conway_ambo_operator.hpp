#pragma once

#include "erhe/geometry/operation/geometry_operation.hpp"

namespace erhe::geometry::operation
{

class Conway_ambo_operator
    : public Geometry_operation
{
public:
    Conway_ambo_operator(Geometry& src, Geometry& destination);
};

auto ambo(erhe::geometry::Geometry& source) -> erhe::geometry::Geometry;

} // namespace erhe::geometry::operation
