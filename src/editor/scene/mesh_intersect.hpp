#pragma once

#include "erhe_geometry/geometry.hpp"

#include <glm/glm.hpp>

namespace erhe::scene {
    class Mesh;
}

namespace editor
{

auto intersect(
    const erhe::scene::Mesh&    mesh,
    const glm::vec3             origin,
    const glm::vec3             direction,
    erhe::geometry::Geometry*&  out_geometry,
    erhe::geometry::Polygon_id& out_polygon_id,
    float&                      out_t,
    float&                      out_u,
    float&                      out_v
) -> bool;

} // namespace editor
