#include "erhe_geometry/operation/difference.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"

#include <geogram/mesh/mesh_intersection.h>
#include <geogram/mesh/mesh_CSG.h>


namespace erhe::geometry::operation {

class Difference : public Geometry_operation
{
public:
    Difference(const Geometry& lhs, const Geometry& rhs, Geometry& destination);

    void build();
};

Difference::Difference(const Geometry& lhs, const Geometry& rhs, Geometry& destination)
    : Geometry_operation{lhs, rhs, destination}
{
}

void Difference::build()
{
#if 1
    GEO::mesh_boolean_operation(destination_mesh, lhs_mesh, *rhs_mesh, "A-B", true);
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

    post_processing();
}

void difference(const Geometry& lhs, const Geometry& rhs, Geometry& destination)
{
    Difference operation{lhs, rhs, destination};
    operation.build();
}

} // namespace erhe::geometry::operation
