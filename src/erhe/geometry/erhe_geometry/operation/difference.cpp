#include "erhe_geometry/operation/difference.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_profile/profile.hpp"

#include <geogram/basic/common.h>
#include <geogram/mesh/mesh.h>
#include <geogram/mesh/mesh_intersection.h>
//#include <geogram/mesh/mesh_CSG.h>

#include <fmt/format.h>

namespace erhe::geometry::operation {

Difference::Difference(const Geometry& lhs, const Geometry& rhs, Geometry& destination)
    : Geometry_operation{lhs, rhs, destination}
{
    ERHE_PROFILE_FUNCTION();

#if 1
    GEO::Mesh lhs_mesh{};
    GEO::Mesh rhs_mesh{};
    GEO::Mesh result{};
    lhs.extract_geogram_mesh(lhs_mesh);
    rhs.extract_geogram_mesh(rhs_mesh);
    GEO::mesh_boolean_operation(result, lhs_mesh, rhs_mesh, "A-B", true);
    geometry_from_geogram(destination, result);
#else // TODO does either path make more sense?
    GEO::CSGMesh_var lhs_mesh = new GEO::CSGMesh;
    lhs.extract_geogram_mesh(*lhs_mesh.get());
    lhs_mesh->facets.connect();
    lhs_mesh->facets.triangulate();
    lhs_mesh->facets.compute_borders();
    lhs_mesh->update_bbox();

    GEO::CSGMesh_var rhs_mesh = new GEO::CSGMesh;
    rhs.extract_geogram_mesh(*rhs_mesh.get());
    rhs_mesh->facets.connect();
    rhs_mesh->facets.triangulate();
    rhs_mesh->facets.compute_borders();
    rhs_mesh->update_bbox();

    GEO::CSGScope scope{};
    scope.push_back(std::move(lhs_mesh));
    scope.push_back(std::move(rhs_mesh));

    GEO::CSGBuilder builder{};
    GEO::CSGMesh_var out_mesh = builder.difference(scope);

    geometry_from_geogram(destination, *out_mesh.get());
#endif

    Property_map<Point_id, glm::vec3>* point_normals = destination.point_attributes().find<glm::vec3>(c_point_normals);
    Property_map<Point_id, glm::vec3>* point_normals_smooth = destination.point_attributes().find<glm::vec3>(c_point_normals_smooth);
    Property_map<Corner_id, glm::vec3>* corner_normals = destination.corner_attributes().find<glm::vec3>(c_corner_normals);
    if (point_normals != nullptr) {
        point_normals->clear();
    }
    if (point_normals_smooth != nullptr) {
        point_normals_smooth->clear();
    }
    if (corner_normals != nullptr) {
        corner_normals->clear();
    }
    destination.compute_polygon_normals();
}

auto difference(const Geometry& lhs, const Geometry& rhs) -> Geometry
{
    return Geometry(
        fmt::format("difference({}, {})", lhs.name, rhs.name),
        [&lhs, &rhs](auto& result) {
            Difference operation{lhs, rhs, result};
        }
    );
}

} // namespace erhe::geometry::operation
