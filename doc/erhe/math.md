# erhe_math

Stability: stable

## Purpose
Collection of math utilities for 3D graphics: AABB and bounding sphere
types, viewport projection/unprojection, input axis smoothing, and various
vector/matrix helper functions used throughout erhe.

## Key Types
- `Aabb` -- axis-aligned bounding box with include/transform/query operations
- `Sphere` -- bounding sphere with enclosure, containment, and transform; includes `optimal_enclosing_sphere()`
- `Viewport` -- integer viewport rectangle with project/unproject and hit-test
- `Input_axis` -- smoothed input axis with damping, used for camera movement controls

## Public API
- `Aabb`: `include(point)`, `include(aabb)`, `transformed_by(mat4)`, `center()`, `diagonal()`, `volume()`
- `Sphere`: `enclose(point)`, `enclose(sphere)`, `contains(point)`, `transformed_by(mat4)`, `optimal_enclosing_sphere(points)`. The enclosing sphere is minimal up to float rounding and always contains every input point under the exact `contains(point)` test, at any scale and at any distance from the origin. The support search (MathGeoLib's) keeps every tolerance relative to the coordinate magnitude and the size of the support simplex; the two, three, four and five point cases return the smallest of their grown candidates (circumscribed sphere when its center lies inside the simplex, else the minimal spheres of the sub-simplices grown to reach the left-out point), so a candidate that rounding leaves short costs at most the rounding error; a final pass encloses any point the search's cycle guard left outside. `test/test_sphere.cpp` covers trivial, degenerate and hard cases at several scales and offsets, a Bistro mesh 6 km from the origin, and a seeded pseudorandom smoke test (`ERHE_SPHERE_SMOKE_SECONDS`, `ERHE_SPHERE_SMOKE_SEED`) that checks exact enclosure and Jung's bound on the radius and prints failing inputs as hexadecimal floats.
- `Viewport`: `project_to_screen_space()`, `unproject()`, `aspect_ratio()`, `hit_test()`
- `euler_angles.hpp`: Euler angles to and from quaternions for all 12 axis orders, without a rotation matrix in between. `euler_angles_to_quaternion()` / `quaternion_to_euler_angles()` take the three axis indices; `quatEulerAngleABC()` / `extractEulerAngleABC()` are the per-order forms, named after the matrix functions `glm::eulerAngleABC()` / `glm::extractEulerAngleABC()` whose convention they share (`R_A(t1) * R_B(t2) * R_C(t3)`). Extraction keeps the quaternion's sign: composing the extracted angles gives q back, not -q. `quaternion_to_euler_angles_near()` takes reference angles and returns, of every angle triple that gives q, the one nearest to them, each angle in (-2 pi, 2 pi]; an editor passes the angles it shows, so they survive a round trip through the quaternion as edited. The header comment states the angle ranges and the gimbal-lock rule; `test/test_euler_angles.cpp` covers them.
- `coordinate_conventions.hpp`: `Coordinate_conventions` and its enums (`Depth_range`, `Framebuffer_origin`, `Texture_origin`, `Clip_space_y_flip`), the per-backend conventions a `Device_info` carries; a header with no glm or profile dependency, included by `math_util.hpp`.
- `box_hull.hpp`: `calculate_bounding_convex_hull_of_boxes()` returns the 2D convex hull of the projected corners of many `Aabb`s plus extra points, bit-identical to hulling every projected corner but without sorting the corners of boxes that lie inside the hull of the others (a polygon from the extreme corners in 16 directions, then a center and extents test per box). `Box_hull_scratch` holds the persistent buffers. The shadow fit's rotation stage uses it; `test/test_box_hull.cpp` checks exact equality with the plain hull.
- `math_util.hpp`: `remap()`, `unproject<T>()`, `project_to_screen_space<T>()`, color conversion (`vec3_from_uint`, `uint_from_vector3`), axis helpers (`min_axis`, `max_axis`), predefined rotation/swap matrices

## Dependencies
- `erhe::profile` -- profiling macros (in math_util.hpp)
- External: glm

## Notes
- Most functions in `math_util.hpp` are templated on `float`/`double` via a `vector_types<T>` trait.
- `Input_axis` supports both linear and multiplicative damping modes.
- The unproject/project functions follow OpenGL conventions (configurable depth range).
