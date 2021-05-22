#include "erhe/physics/shape.hpp"

namespace erhe::physics
{

#if 0
#include "erhe/geometry/geometry.hpp"

#include "BulletCollision/CollisionShapes/btConvexHullShape.h"

namespace erhe::physics
{

btConvexHullShape convex_shape(erhe::geometry::Geometry& geometry)
{
    btConvexHullShape shape(
        reinterpret_cast<const btScalar*>(geometry.point_attributes().find<glm::vec3>(erhe::geometry::c_point_locations)->values.data()),
        geometry.point_count(),
        sizeof(glm::vec3)
    );
    return shape;
}
#endif

} // namespace erhe::physics
