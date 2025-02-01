#pragma once

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

void repair(const Geometry& source, Geometry& destination);

void weld(const Geometry& source, Geometry& destination);

} // namespace erhe::geometry::operation
