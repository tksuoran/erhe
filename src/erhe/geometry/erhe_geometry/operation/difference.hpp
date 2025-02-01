#pragma once

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

void difference(const Geometry& lhs, const Geometry& rhs, Geometry& destination);

} // namespace erhe::geometry::operation
