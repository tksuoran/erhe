#include "erhe_geometry/operation/csg/union.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"

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
    run_mesh_boolean_operation("A+B");

    //post_processing();
    interpolate_mesh_attributes();
}

void union_(const Geometry& lhs, const Geometry& rhs, Geometry& destination)
{
    Union operation{lhs, rhs, destination};
    operation.build();
}


} // namespace erhe::geometry::operation
