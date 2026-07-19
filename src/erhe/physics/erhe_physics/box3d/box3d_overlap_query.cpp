#include "erhe_physics/box3d/box3d_overlap_query.hpp"
#include "erhe_physics/physics_log.hpp"

#include <box3d/collision.h>

#include <algorithm>
#include <cfloat>

namespace erhe::physics {

namespace {

// b3LocalManifold points into a caller owned buffer. Four points is the most a
// convex pair produces (B3_MAX_MANIFOLD_POINTS); Box3D's own contact solver
// passes 32 because it also feeds mesh shapes through here, so this keeps the
// same slack rather than assuming the tight bound.
constexpr int manifold_point_capacity = 32;

// Box3D fixes the argument order of its manifold functions by shape type: the
// sphere is always B, the hull is always A. Ranking the kinds reproduces that
// order, and the pair is swapped (with the transform inverted) when needed.
[[nodiscard]] auto get_collide_rank(const Body_shape_geometry::Kind kind) -> int
{
    switch (kind) {
        case Body_shape_geometry::Kind::sphere:  return 0;
        case Body_shape_geometry::Kind::capsule: return 1;
        case Body_shape_geometry::Kind::hull:    return 2;
        default:                                 return 3;
    }
}

// Minimum separation over the manifold, or FLT_MAX when the shapes produce no
// manifold at all (fully separated). Negative means penetrating.
[[nodiscard]] auto compute_min_separation(
    const Body_shape_geometry& geometry_a,
    const Body_shape_geometry& geometry_b,
    const b3Transform          b_to_a
) -> float
{
    b3LocalManifoldPoint points[manifold_point_capacity] = {};
    b3LocalManifold      manifold                        = {};
    manifold.points = points;

    // These caches are pure inputs to the incremental solver; a zeroed cache
    // means "no previous frame", which is what a one-shot query wants.
    b3SimplexCache simplex_cache = {};
    b3SATCache     sat_cache     = {};

    switch (geometry_a.kind) {
        case Body_shape_geometry::Kind::sphere: {
            b3CollideSpheres(&manifold, manifold_point_capacity, &geometry_a.sphere, &geometry_b.sphere, b_to_a);
            break;
        }
        case Body_shape_geometry::Kind::capsule: {
            if (geometry_b.kind == Body_shape_geometry::Kind::sphere) {
                b3CollideCapsuleAndSphere(&manifold, manifold_point_capacity, &geometry_a.capsule, &geometry_b.sphere, b_to_a);
            } else {
                b3CollideCapsules(&manifold, manifold_point_capacity, &geometry_a.capsule, &geometry_b.capsule, b_to_a);
            }
            break;
        }
        case Body_shape_geometry::Kind::hull: {
            switch (geometry_b.kind) {
                case Body_shape_geometry::Kind::sphere: {
                    b3CollideHullAndSphere(&manifold, manifold_point_capacity, geometry_a.hull, &geometry_b.sphere, b_to_a, &simplex_cache);
                    break;
                }
                case Body_shape_geometry::Kind::capsule: {
                    b3CollideHullAndCapsule(&manifold, manifold_point_capacity, geometry_a.hull, &geometry_b.capsule, b_to_a, &simplex_cache);
                    break;
                }
                default: {
                    b3CollideHulls(&manifold, manifold_point_capacity, geometry_a.hull, geometry_b.hull, b_to_a, &sat_cache);
                    break;
                }
            }
            break;
        }
        default: {
            return FLT_MAX;
        }
    }

    float min_separation = FLT_MAX;
    for (int i = 0; i < manifold.pointCount; ++i) {
        min_separation = std::min(min_separation, manifold.points[i].separation);
    }
    return min_separation;
}

// Point cloud approximating a convex shape, for the boolean mesh fallback.
class Convex_proxy_points
{
public:
    b3Vec3 points[B3_MAX_SHAPE_CAST_POINTS] = {};
    int    count                            {0};
    float  radius                           {0.0f};
    bool   is_exact                         {true};
};

// Builds the proxy in the frame given by to_mesh_frame, since b3OverlapMesh
// compares the mesh (at identity) against the proxy as handed in.
[[nodiscard]] auto make_convex_proxy_points(
    const Body_shape_geometry& geometry,
    const b3Transform          to_mesh_frame
) -> Convex_proxy_points
{
    Convex_proxy_points proxy;
    switch (geometry.kind) {
        case Body_shape_geometry::Kind::sphere: {
            proxy.points[0] = b3TransformPoint(to_mesh_frame, geometry.sphere.center);
            proxy.count     = 1;
            proxy.radius    = geometry.sphere.radius;
            break;
        }
        case Body_shape_geometry::Kind::capsule: {
            proxy.points[0] = b3TransformPoint(to_mesh_frame, geometry.capsule.center1);
            proxy.points[1] = b3TransformPoint(to_mesh_frame, geometry.capsule.center2);
            proxy.count     = 2;
            proxy.radius    = geometry.capsule.radius;
            break;
        }
        case Body_shape_geometry::Kind::hull: {
            const b3Vec3* hull_points = b3GetHullPoints(geometry.hull);
            if (hull_points == nullptr) {
                break;
            }
            if (geometry.hull->vertexCount <= B3_MAX_SHAPE_CAST_POINTS) {
                for (int i = 0; i < geometry.hull->vertexCount; ++i) {
                    proxy.points[i] = b3TransformPoint(to_mesh_frame, hull_points[i]);
                }
                proxy.count = geometry.hull->vertexCount;
                break;
            }
            // A proxy is capped at B3_MAX_SHAPE_CAST_POINTS points but a hull may
            // carry up to B3_MAX_HULL_VERTICES (128), so a dense hull falls back
            // to its bounding box: a superset of the hull, hence conservative in
            // the same direction the boolean mesh test already is.
            const b3AABB  aabb       = geometry.hull->aabb;
            const b3Vec3  bounds[2]  = {aabb.lowerBound, aabb.upperBound};
            for (int corner = 0; corner < 8; ++corner) {
                const b3Vec3 point{
                    bounds[(corner & 1) != 0 ? 1 : 0].x,
                    bounds[(corner & 2) != 0 ? 1 : 0].y,
                    bounds[(corner & 4) != 0 ? 1 : 0].z
                };
                proxy.points[corner] = b3TransformPoint(to_mesh_frame, point);
            }
            proxy.count    = 8;
            proxy.is_exact = false;
            break;
        }
        default: {
            break;
        }
    }
    return proxy;
}

bool g_mesh_tolerance_warning_logged = false;

// Box3D publishes no hull-versus-mesh manifold (only b3CollideHullAndTriangle,
// with no public way to iterate a mesh's triangles), so a mesh can only be
// tested with the boolean b3OverlapMesh. That ignores the penetration
// tolerance: a touching contact reads as an intersection. Rarely reached --
// meshes are static or kinematic here, and trial placement moves a dynamic
// body.
[[nodiscard]] auto mesh_overlaps_convex(
    const Body_shape_geometry& mesh_geometry,
    const Body_shape_geometry& convex_geometry,
    const b3Transform          convex_to_mesh
) -> bool
{
    const Convex_proxy_points proxy_points = make_convex_proxy_points(convex_geometry, convex_to_mesh);
    if (proxy_points.count == 0) {
        return false;
    }
    if (!g_mesh_tolerance_warning_logged) {
        log_physics->warn(
            "box3d: trial-placement overlap against a mesh shape is a boolean test and ignores the "
            "penetration tolerance; Box3D publishes no hull-versus-mesh manifold"
        );
        g_mesh_tolerance_warning_logged = true;
    }

    const b3ShapeProxy proxy{
        .points = proxy_points.points,
        .count  = proxy_points.count,
        .radius = proxy_points.radius
    };
    return b3OverlapMesh(&mesh_geometry.mesh, b3Transform_identity, &proxy);
}

} // anonymous namespace

auto get_body_shape_geometry(const b3ShapeId shape_id) -> Body_shape_geometry
{
    Body_shape_geometry geometry;
    switch (b3Shape_GetType(shape_id)) {
        case b3_sphereShape: {
            geometry.kind   = Body_shape_geometry::Kind::sphere;
            geometry.sphere = b3Shape_GetSphere(shape_id);
            break;
        }
        case b3_capsuleShape: {
            geometry.kind    = Body_shape_geometry::Kind::capsule;
            geometry.capsule = b3Shape_GetCapsule(shape_id);
            break;
        }
        case b3_hullShape: {
            geometry.kind = Body_shape_geometry::Kind::hull;
            geometry.hull = b3Shape_GetHull(shape_id);
            break;
        }
        case b3_meshShape: {
            geometry.kind = Body_shape_geometry::Kind::mesh;
            geometry.mesh = b3Shape_GetMesh(shape_id);
            break;
        }
        default: {
            break;
        }
    }
    return geometry;
}

auto get_local_aabb(const Body_shape_geometry& geometry) -> b3AABB
{
    switch (geometry.kind) {
        case Body_shape_geometry::Kind::sphere: {
            const b3Vec3 radius{geometry.sphere.radius, geometry.sphere.radius, geometry.sphere.radius};
            return b3AABB{b3Sub(geometry.sphere.center, radius), b3Add(geometry.sphere.center, radius)};
        }
        case Body_shape_geometry::Kind::capsule: {
            const b3Vec3 radius{geometry.capsule.radius, geometry.capsule.radius, geometry.capsule.radius};
            const b3Vec3 lower = b3Min(geometry.capsule.center1, geometry.capsule.center2);
            const b3Vec3 upper = b3Max(geometry.capsule.center1, geometry.capsule.center2);
            return b3AABB{b3Sub(lower, radius), b3Add(upper, radius)};
        }
        case Body_shape_geometry::Kind::hull: {
            return geometry.hull->aabb;
        }
        case Body_shape_geometry::Kind::mesh: {
            // The mesh AABB is stored unscaled; the shape's scale may mirror, so
            // take the componentwise min / max after scaling rather than
            // assuming the bounds stay ordered.
            const b3AABB aabb         = geometry.mesh.data->bounds;
            const b3Vec3 scaled_lower = b3Mul(geometry.mesh.scale, aabb.lowerBound);
            const b3Vec3 scaled_upper = b3Mul(geometry.mesh.scale, aabb.upperBound);
            return b3AABB{b3Min(scaled_lower, scaled_upper), b3Max(scaled_lower, scaled_upper)};
        }
        default: {
            return b3AABB{b3Vec3_zero, b3Vec3_zero};
        }
    }
}

auto shapes_intersect(
    const Body_shape_geometry& geometry_a,
    const Body_shape_geometry& geometry_b,
    const b3Transform          b_to_a,
    const float                penetration_tolerance
) -> bool
{
    const bool a_is_mesh = (geometry_a.kind == Body_shape_geometry::Kind::mesh);
    const bool b_is_mesh = (geometry_b.kind == Body_shape_geometry::Kind::mesh);
    if (a_is_mesh && b_is_mesh) {
        // Box3D never collides two meshes either (both are static geometry).
        return false;
    }
    if (a_is_mesh) {
        return mesh_overlaps_convex(geometry_a, geometry_b, b_to_a);
    }
    if (b_is_mesh) {
        return mesh_overlaps_convex(geometry_b, geometry_a, b3InvertTransform(b_to_a));
    }

    // Box3D fixes the type order, so swap the pair (and the transform with it)
    // when B outranks A.
    const bool  swap           = get_collide_rank(geometry_b.kind) > get_collide_rank(geometry_a.kind);
    const float min_separation = swap
        ? compute_min_separation(geometry_b, geometry_a, b3InvertTransform(b_to_a))
        : compute_min_separation(geometry_a, geometry_b, b_to_a);

    return min_separation < -penetration_tolerance;
}

} // namespace erhe::physics
