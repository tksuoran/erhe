#pragma once

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

void catmull_clark_subdivision(const Geometry& source, Geometry& destination);

} // namespace erhe::geometry::operation
