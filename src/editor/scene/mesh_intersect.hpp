#pragma once

#include <geogram/mesh/mesh.h>

#include <glm/glm.hpp>

#include <memory>

namespace erhe::scene    { class Mesh; }
namespace erhe::geometry { class Geometry; }

namespace editor {

#if 0
[[nodiscard]] auto intersect(
    const erhe::scene::Mesh&                  mesh,
    const glm::vec3                           origin,
    const glm::vec3                           direction,
    std::shared_ptr<erhe::geometry::Geometry> out_geometry,
    GEO::index_t&                             out_facet,
    float&                                    out_t,
    float&                                    out_u,
    float&                                    out_v
) -> bool;
#endif

}
