#include "erhe_geometry/operation/intersection.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_profile/profile.hpp"

#include <geogram/basic/common.h>
#include <geogram/mesh/mesh.h>
#include <geogram/mesh/mesh_CSG.h>

#include <fmt/format.h>

namespace erhe::geometry::operation {

Intersection::Intersection(const Geometry& lhs, const Geometry& rhs, Geometry& destination)
    : Geometry_operation{lhs, rhs, destination}
{
    ERHE_PROFILE_FUNCTION();

    GEO::CSGMesh_var lhs_mesh = new GEO::CSGMesh; 
    GEO::CSGMesh_var rhs_mesh = new GEO::CSGMesh;
    lhs.extract_geogram_mesh(*lhs_mesh.get());
    rhs.extract_geogram_mesh(*rhs_mesh.get());
    lhs_mesh->facets.connect();
    rhs_mesh->facets.connect();

    GEO::CSGScope scope{};
    scope.push_back(std::move(lhs_mesh));
    scope.push_back(std::move(rhs_mesh));

    GEO::CSGBuilder builder{};
    GEO::CSGMesh_var out_mesh = builder.intersection(scope);

    geometry_from_geogram(destination, *out_mesh.get());
}

auto intersection(const Geometry& lhs, const Geometry& rhs) -> Geometry
{
    return Geometry(
        fmt::format("difference({}, {})", lhs.name, rhs.name),
        [&lhs, &rhs](auto& result) {
            Intersection operation{lhs, rhs, result};
        }
    );
}


} // namespace erhe::geometry::operation
