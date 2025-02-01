#include "erhe_geometry/operation/intersection.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"

#include <geogram/mesh/mesh_intersection.h>

namespace erhe::geometry::operation {

class Intersection : public Geometry_operation
{
public:
    Intersection(const Geometry& lhs, const Geometry& rhs, Geometry& destination);

    void build();
};

Intersection::Intersection(const Geometry& lhs, const Geometry& rhs, Geometry& destination)
    : Geometry_operation{lhs, rhs, destination}
{
}

void Intersection::build()
{
    GEO::mesh_boolean_operation(destination_mesh, lhs_mesh, *rhs_mesh, "A*B", true);

    post_processing();
}

void intersection(const Geometry& lhs, const Geometry& rhs, Geometry& destination)
{
    Intersection operation{lhs, rhs, destination};
    operation.build();
}


} // namespace erhe::geometry::operation
