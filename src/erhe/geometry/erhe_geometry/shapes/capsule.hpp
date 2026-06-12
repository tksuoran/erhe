#pragma once

namespace GEO { class Mesh; }

namespace erhe::geometry::shapes {

// Capsule on the Y axis, centered at origin.
//   radius                 = cap and body radius
//   length                 = length of the cylindrical mid-section only
//                            (same convention as erhe::physics::ICollision_shape::create_capsule_shape());
//                            total height = length + 2 * radius
//   slice_count            = subdivisions around the Y axis
//   hemisphere_stack_count = latitude rings per cap
void make_capsule(GEO::Mesh& mesh, float radius, float length, int slice_count, int hemisphere_stack_count);

// Tapered capsule on the Y axis, centered at origin: sphere caps of
// bottom_radius at y = -length / 2 and top_radius at y = +length / 2, joined
// by the cone tangent to both spheres (the convex hull of the two spheres).
// The caps are spherical caps cut at the cone tangency latitude rather than
// exact hemispheres, so the surface normal is continuous across the cap/body
// junctions. Total height = length + bottom_radius + top_radius.
//   bottom_radius          = cap sphere radius at the bottom end; must be > 0
//   top_radius             = cap sphere radius at the top end; must be > 0;
//                            unless the radii are equal,
//                            |bottom_radius - top_radius| must be < length
//                            (otherwise one cap sphere contains the other)
//   length                 = axial distance between the two cap sphere centers
//   slice_count            = subdivisions around the Y axis
//   hemisphere_stack_count = latitude rings per cap
void make_capsule(GEO::Mesh& mesh, float bottom_radius, float top_radius, float length, int slice_count, int hemisphere_stack_count);

} // namespace erhe::geometry::shapes
