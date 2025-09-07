#pragma once

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

void subdivide(const Geometry& source, Geometry& destination);

} // namespace erhe::geometry::operation
