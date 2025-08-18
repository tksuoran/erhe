#pragma once

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

void generate_tangents(const Geometry& source, Geometry& destination);

} // namespace erhe::geometry::operation
