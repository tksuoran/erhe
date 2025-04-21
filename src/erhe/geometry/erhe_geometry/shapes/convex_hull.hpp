#pragma once

#include <glm/glm.hpp>

#include <array>
#include <vector>

namespace GEO { class Mesh; }

namespace erhe::geometry::shapes {

void make_convex_hull(GEO::Mesh& mesh, const std::vector<glm::vec3>& points);

} // namespace erhe::geometry::shapes
