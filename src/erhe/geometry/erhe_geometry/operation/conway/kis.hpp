#pragma once

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

void kis(const Geometry& source, Geometry& destination, float height = 0.0f);

} // namespace erhe::geometry::operation
