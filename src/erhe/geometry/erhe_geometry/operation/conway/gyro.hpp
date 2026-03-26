#pragma once

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

void gyro(const Geometry& source, Geometry& destination, float ratio = 1.0f / 3.0f);

} // namespace erhe::geometry::operation
