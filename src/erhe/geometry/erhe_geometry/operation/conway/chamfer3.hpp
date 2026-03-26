#pragma once

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

void chamfer3(const Geometry& source, Geometry& destination, float bevel_ratio = 0.25f);

} // namespace erhe::geometry::operation
