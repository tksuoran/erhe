#pragma once

#include <glm/glm.hpp>

#include <array>
#include <vector>

namespace GEO { class Mesh; }

namespace erhe::geometry::shapes {

// Fills mesh with the convex hull of points. Returns false, leaving mesh
// empty, when the point set has no volume (see
// erhe::math::classify_affine_span()).
[[nodiscard]] auto make_convex_hull(GEO::Mesh& mesh, const std::vector<glm::vec3>& points) -> bool;

} // namespace erhe::geometry::shapes
