#include "erhe_geometry/operation/repair.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_log/log_glm.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <geogram/basic/common.h>
#include <geogram/mesh/mesh.h>
#include <geogram/mesh/mesh_repair.h>
#include <geogram/mesh/mesh_fill_holes.h>
#include <geogram/mesh/mesh_surface_intersection.h>

#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>

#include <fmt/format.h>

namespace erhe::geometry::operation {

Repair::Repair(const Geometry& source, Geometry& destination)
    : Geometry_operation{source, destination}
{
    ERHE_PROFILE_FUNCTION();

    GEO::Mesh mesh{};
    source.extract_geogram_mesh(mesh);

    mesh.facets.connect();

    const double merge_vertices_epsilon = 0.001;
    const double fill_hole_max_area = 0.01;
    GEO::mesh_repair(mesh, GEO::MESH_REPAIR_DEFAULT, merge_vertices_epsilon);
    GEO::fill_holes(mesh, fill_hole_max_area);
    GEO::MeshSurfaceIntersection intersection(mesh);
    intersection.intersect();
    intersection.remove_internal_shells();
    intersection.simplify_coplanar_facets();
    GEO::mesh_repair(mesh, GEO::MESH_REPAIR_DEFAULT, merge_vertices_epsilon);

    geometry_from_geogram(destination, mesh);
}

auto repair(const Geometry& source) -> Geometry
{
    return Geometry(
        fmt::format("repair({})", source.name),
        [&source](auto& result) {
            Repair operation{source, result};
        }
    );
}


} // namespace erhe::geometry::operation

#if 0
    GEO::Mesh tet_mesh;
    GEO::MeshTetrahedralizeParameters params
    {
        .preprocess = true,
        .preprocess_merge_vertices_epsilon = 0.001,
        .preprocess_fill_hole_max_area = 0.01,
        .refine = true,
        .refine_quality = 2.0,
        .keep_regions = true,
        .verbose = true
    }; 
    bool tet_ok = GEO::mesh_tetrahedralize(mesh, params);
    ERHE_VERIFY(tet_ok); // TODO Handle not ok
#endif
