#include "erhe_geometry/operation/csg/difference.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"

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
    run_mesh_boolean_operation("A-B");

    //post_processing();
    interpolate_mesh_attributes();
}

void difference(const Geometry& lhs, const Geometry& rhs, Geometry& destination)
{
    Difference operation{lhs, rhs, destination};
    operation.build();
}

} // namespace erhe::geometry::operation
