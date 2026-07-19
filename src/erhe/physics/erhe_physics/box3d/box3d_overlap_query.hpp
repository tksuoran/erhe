#pragma once

#include <box3d/box3d.h>

namespace erhe::physics {

// Narrow-phase overlap testing for the trial-placement queries
// (IWorld::would_bodies_intersect / would_body_intersect_world), which ask
// whether a body WOULD penetrate something at a hypothetical transform without
// moving anything.
//
// Box3D offers no query that answers this: b3Body_OverlapShape and the
// b3Overlap* family are boolean, and b3ShapeDistance reports distance 0 for any
// overlap however deep, so neither can express erhe's penetration tolerance.
// The pairwise manifold functions (b3CollideHulls and friends) do: their
// b3ManifoldPoint::separation is negative when penetrating, which is exactly
// what the tolerance compares against. So the test runs shape against shape and
// looks at the separations.
//
// Everything here is in BODY-ORIGIN LOCAL space. Box3D bakes a shape's creation
// transform and scale into the shape at create time, so what b3Shape_Get*()
// hands back is already placed in its body's frame -- the frame the b3Collide*()
// functions want -- and one transform between the two bodies covers every shape
// pair.

class Body_shape_geometry
{
public:
    enum class Kind : int {
        sphere,
        capsule,
        hull,
        mesh,
        unsupported // height fields and Box3D's baked compound shape; erhe creates neither
    };

    Kind              kind   {Kind::unsupported};
    b3Sphere          sphere {};
    b3Capsule         capsule{};
    const b3HullData* hull   {nullptr};
    b3Mesh            mesh   {};
};

[[nodiscard]] auto get_body_shape_geometry(b3ShapeId shape_id) -> Body_shape_geometry;

// Axis aligned bounds of the shape in its body's frame.
[[nodiscard]] auto get_local_aabb(const Body_shape_geometry& geometry) -> b3AABB;

// True when the two shapes penetrate by more than penetration_tolerance.
// b_to_a maps body B's frame into body A's frame.
//
// Touching contacts (separation >= -tolerance) are not reported, so bodies that
// merely rest against each other do not count as intersecting.
[[nodiscard]] auto shapes_intersect(
    const Body_shape_geometry& geometry_a,
    const Body_shape_geometry& geometry_b,
    b3Transform                b_to_a,
    float                      penetration_tolerance
) -> bool;

} // namespace erhe::physics
