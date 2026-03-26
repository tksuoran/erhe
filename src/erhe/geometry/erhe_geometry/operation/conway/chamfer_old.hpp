#pragma once

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

void chamfer_old(const Geometry& source, Geometry& destination);

} // namespace erhe::geometry::operation
