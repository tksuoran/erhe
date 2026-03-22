# erhe_math

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
- `Sphere`: `enclose(point)`, `enclose(sphere)`, `contains(point)`, `transformed_by(mat4)`, `optimal_enclosing_sphere(points)`
- `Viewport`: `project_to_screen_space()`, `unproject()`, `aspect_ratio()`, `hit_test()`
- `math_util.hpp`: `remap()`, `unproject<T>()`, `project_to_screen_space<T>()`, color conversion (`vec3_from_uint`, `uint_from_vector3`), axis helpers (`min_axis`, `max_axis`), predefined rotation/swap matrices

## Dependencies
- `erhe::profile` -- profiling macros (in math_util.hpp)
- External: glm

## Notes
- Most functions in `math_util.hpp` are templated on `float`/`double` via a `vector_types<T>` trait.
- `Input_axis` supports both linear and multiplicative damping modes.
- The unproject/project functions follow OpenGL conventions (configurable depth range).
