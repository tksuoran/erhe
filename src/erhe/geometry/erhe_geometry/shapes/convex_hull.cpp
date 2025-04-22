#include "erhe_geometry/shapes/convex_hull.hpp"
#include "erhe_geometry/geometry_log.hpp"

#include <geogram/mesh/mesh.h>
#include <geogram/mesh/mesh_repair.h>
#include <geogram/mesh/mesh_fill_holes.h>
#include <geogram/mesh/mesh_surface_intersection.h>

#include <geogram/basic/command_line.h>
#include <geogram/delaunay/delaunay.h>

namespace erhe::geometry::shapes {

void make_convex_hull(GEO::Mesh& mesh, const std::vector<glm::vec3>& in_points)
{
    GEO::vector<double> points;
    GEO::index_t point_count = 0;
    //log_geometry->info("Input points:");
    for (glm::vec3 p : in_points) {
        //log_geometry->info("  {}, {}, {}", p.x, p.y, p.z);
        points.push_back(static_cast<double>(p.x));
        points.push_back(static_cast<double>(p.y));
        points.push_back(static_cast<double>(p.z));
        ++point_count;
    }

    GEO::CmdLine::set_arg("algo:delaunay", "PDEL");
    GEO::Delaunay_var delaunay = GEO::Delaunay::create(3);
    delaunay->set_keeps_infinite(true); // keep "vertex at infinity" (makes it easier to find the convex hull
    delaunay->set_vertices(point_count, points.data());
    GEO::vector<GEO::index_t> triangles_indices;
    for (GEO::index_t t = delaunay->nb_finite_cells(); t < delaunay->nb_cells(); ++t) {
        GEO::index_t v0 = delaunay->cell_vertex(t, 0);
        GEO::index_t v1 = delaunay->cell_vertex(t, 1);
        GEO::index_t v2 = delaunay->cell_vertex(t, 2);
        GEO::index_t v3 = delaunay->cell_vertex(t, 3);
        if(v0 == GEO::NO_INDEX) {
            triangles_indices.push_back(v3);
            triangles_indices.push_back(v2);
            triangles_indices.push_back(v1);
        } else if (v1 == GEO::NO_INDEX) {
            triangles_indices.push_back(v0);
            triangles_indices.push_back(v2);
            triangles_indices.push_back(v3);
        } else if (v2 == GEO::NO_INDEX) {
            triangles_indices.push_back(v0);
            triangles_indices.push_back(v3);
            triangles_indices.push_back(v1);
        } else if (v3 == GEO::NO_INDEX) {
            triangles_indices.push_back(v0);
            triangles_indices.push_back(v1);
            triangles_indices.push_back(v2);
        }
    }

    mesh.vertices.set_dimension(3);
    mesh.vertices.set_double_precision();
    mesh.facets.assign_triangle_mesh(GEO::coord_index_t{3}, points, triangles_indices, true);
    mesh.vertices.remove_isolated();
    mesh.facets.connect();
    mesh.vertices.set_single_precision();
}

} // namespace erhe::geometry::shapes
