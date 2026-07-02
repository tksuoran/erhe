#include "erhe_geometry/operation/csg/intersection.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"

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
    run_mesh_boolean_operation("A*B");

    //post_processing();
    interpolate_mesh_attributes();
}

void intersection(const Geometry& lhs, const Geometry& rhs, Geometry& destination)
{
    Intersection operation{lhs, rhs, destination};
    operation.build();
}


} // namespace erhe::geometry::operation
