#pragma once

#include <geogram/mesh/mesh.h>

#include <glm/glm.hpp>

namespace erhe::scene    { class Mesh; }
namespace erhe::geometry { class Geometry; }

namespace editor {

[[nodiscard]] auto intersect(
    const erhe::scene::Mesh&                  mesh,
    const glm::vec3                           origin,
    const glm::vec3                           direction,
    std::shared_ptr<erhe::geometry::Geometry> out_geometry,
    GEO::index_t&                             out_facet,
    double&                                   out_t,
    double&                                   out_u,
    double&                                   out_v
) -> bool;

} // namespace editor
