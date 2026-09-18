#include "erhe_geometry/shapes/convex_hull.hpp"
#include "erhe_geometry/geometry.hpp"

#include <geogram/mesh/mesh.h>

namespace erhe::geometry::shapes {

auto make_convex_hull(GEO::Mesh& mesh, const std::vector<glm::vec3>& in_points) -> bool
{
    // The hull itself is built by erhe::geometry::make_convex_hull(), which
    // owns the degenerate-input guard, the geogram lock and the sequential
    // Delaunay ("BDEL"); see doc/erhe/geogram.md.
    GEO::Mesh source_mesh{};
    source_mesh.vertices.set_dimension(3);
    source_mesh.vertices.set_single_precision();
    source_mesh.vertices.create_vertices(static_cast<GEO::index_t>(in_points.size()));
    for (GEO::index_t v = 0; v < source_mesh.vertices.nb(); ++v) {
        float* p = source_mesh.vertices.single_precision_point_ptr(v);
        p[0] = in_points[v].x;
        p[1] = in_points[v].y;
        p[2] = in_points[v].z;
    }

    if (!erhe::geometry::make_convex_hull(source_mesh, mesh)) {
        return false;
    }

    mesh.facets.connect();
    return true;
}

} // namespace erhe::geometry::shapes
