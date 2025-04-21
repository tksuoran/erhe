#include "erhe_geometry/shapes/convex_hull.hpp"

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
    for (glm::vec3 p : in_points) {
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
        for (GEO::index_t lv = 0; lv < 4; ++lv ) {
            GEO::signed_index_t v = delaunay->cell_vertex(t, lv);
            if (v != GEO::NO_INDEX) {
                triangles_indices.push_back(GEO::index_t(v));
            }
        }
    }
    mesh.vertices.set_dimension(3);
    mesh.vertices.set_double_precision();
    mesh.facets.assign_triangle_mesh(GEO::coord_index_t{3}, points, triangles_indices, true);
    mesh.vertices.remove_isolated();

    GEO::mesh_reorient(mesh);

    //const double merge_vertices_epsilon = 0.001;
    //const double fill_hole_max_area = 0.01;
    //GEO::mesh_repair(
    //    mesh,
    //    static_cast<GEO::MeshRepairMode>(GEO::MeshRepairMode::MESH_REPAIR_COLOCATE | GEO::MeshRepairMode::MESH_REPAIR_DUP_F),
    //    merge_vertices_epsilon
    //);
    //GEO::fill_holes(mesh, fill_hole_max_area);
    GEO::MeshSurfaceIntersection intersection(mesh);
    intersection.intersect();
    //intersection.remove_internal_shells();
    intersection.simplify_coplanar_facets();
    //GEO::mesh_repair(mesh, GEO::MESH_REPAIR_DEFAULT, merge_vertices_epsilon);
    mesh.vertices.set_single_precision();
}

} // namespace erhe::geometry::shapes
