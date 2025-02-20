#pragma once

#include "erhe_geometry/geometry.hpp"

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

void bake_transform(const Geometry& source, Geometry& destination, const GEO::mat4f& transform);

} // namespace erhe::geometry::operation
