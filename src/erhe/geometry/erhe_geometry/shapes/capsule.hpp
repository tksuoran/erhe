#pragma once

namespace GEO { class Mesh; }

namespace erhe::geometry::shapes {

// Capsule on the Y axis, centered at origin.
//   radius                 = cap and body radius
//   length                 = length of the cylindrical mid-section only
//                            (same convention as erhe::physics::ICollision_shape::create_capsule_shape());
//                            total height = length + 2 * radius
//   slice_count            = subdivisions around the Y axis
//   hemisphere_stack_count = latitude rings per hemisphere cap
void make_capsule(GEO::Mesh& mesh, float radius, float length, int slice_count, int hemisphere_stack_count);

} // namespace erhe::geometry::shapes
