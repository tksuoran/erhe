#include "erhe_geometry/operation/union.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"

#include <geogram/mesh/mesh_intersection.h>

namespace erhe::geometry::operation {

class Union : public Geometry_operation
{
public:
    Union(const Geometry& lhs, const Geometry& rhs, Geometry& destination);

    void build();
};

Union::Union(const Geometry& lhs, const Geometry& rhs, Geometry& destination)
    : Geometry_operation{lhs, rhs, destination}
{
};

void Union::build()
{
    GEO::mesh_boolean_operation(destination_mesh, lhs_mesh, *rhs_mesh, "A+B", true);

    post_processing();
}

void union_(const Geometry& lhs, const Geometry& rhs, Geometry& destination)
{
    Union operation{lhs, rhs, destination};
    operation.build();
}


} // namespace erhe::geometry::operation
