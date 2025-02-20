#include "erhe_geometry/operation/bake_transform.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/geometry.hpp"

namespace erhe::geometry::operation {

class Bake_transform : public Geometry_operation
{
public:
    Bake_transform(const Geometry& source, Geometry& destination);

    void build(const GEO::mat4f& transform);
};

Bake_transform::Bake_transform(const Geometry& source, Geometry& destination)
    : Geometry_operation{source, destination}
{
}

void Bake_transform::build(const GEO::mat4f& transform)
{
    destination.copy_with_transform(source, transform);
}

void bake_transform(const Geometry& source, Geometry& destination, const GEO::mat4f& transform)
{
    Bake_transform operation{source, destination};
    operation.build(transform);
}

} // namespace erhe::geometry::operation
