#pragma once

#include <geogram/mesh/mesh.h>

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

void bake_transform(const Geometry& source, Geometry& destination, const GEO::mat4& transform);

} // namespace erhe::geometry::operation
