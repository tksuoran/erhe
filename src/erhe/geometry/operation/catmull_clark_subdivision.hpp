#pragma once

#include "erhe/geometry/operation/geometry_operation.hpp"
#include <unordered_map>

namespace erhe::geometry::operation
{
class Catmull_clark_subdivision
    : public Geometry_operation
{
public:
    Catmull_clark_subdivision(Geometry& src, Geometry& destination);
};

[[nodiscard]] auto catmull_clark_subdivision(
    erhe::geometry::Geometry& source
) -> erhe::geometry::Geometry;

} // namespace erhe::geometry::operation
