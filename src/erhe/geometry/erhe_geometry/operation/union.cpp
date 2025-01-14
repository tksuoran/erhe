#include "erhe_geometry/operation/union.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_profile/profile.hpp"

#include <geogram/basic/common.h>
#include <geogram/mesh/mesh.h>
#include <geogram/mesh/mesh_intersection.h>

#include <fmt/format.h>

namespace erhe::geometry::operation {

Union::Union(const Geometry& lhs, const Geometry& rhs, Geometry& destination)
    : Geometry_operation{lhs, rhs, destination}
{
    ERHE_PROFILE_FUNCTION();
    GEO::Mesh lhs_mesh{};
    GEO::Mesh rhs_mesh{};
    GEO::Mesh result{};
    lhs.extract_geogram_mesh(lhs_mesh);
    rhs.extract_geogram_mesh(rhs_mesh);
    GEO::mesh_boolean_operation(result, lhs_mesh, rhs_mesh, "A+B", true);
    geometry_from_geogram(destination, result);

    Property_map<Point_id, glm::vec3>* point_normals        = destination.point_attributes  ().find<glm::vec3>(c_point_normals);
    Property_map<Point_id, glm::vec3>* point_normals_smooth = destination.point_attributes  ().find<glm::vec3>(c_point_normals_smooth);
    Property_map<Corner_id, glm::vec3>* corner_normals       = destination.corner_attributes ().find<glm::vec3>(c_corner_normals);
    point_normals->clear();
    point_normals_smooth->clear();
    corner_normals->clear();
    destination.compute_polygon_normals();
}

auto union_(const Geometry& lhs, const Geometry& rhs) -> Geometry
{
    return Geometry(
        fmt::format("union({}, {})", lhs.name, rhs.name),
        [&lhs, &rhs](auto& result) {
            Union operation{lhs, rhs, result};
        }
    );
}


} // namespace erhe::geometry::operation
