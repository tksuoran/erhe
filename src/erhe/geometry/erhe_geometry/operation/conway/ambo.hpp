#pragma once

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

// Conway ambo operator

void ambo(const Geometry& source, Geometry& destination);

} // namespace erhe::geometry::operation
