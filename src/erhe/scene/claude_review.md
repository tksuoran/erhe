# erhe_scene -- Code Review

## Summary
A comprehensive glTF-like scene graph with nodes, transforms, cameras, lights, meshes, skins, and animations. The architecture is well-structured with a clean `Node` / `Node_attachment` / `Scene` hierarchy. The transform propagation system with serial numbers for dirty tracking is effective. Main concerns are around thread safety of the global serial counter, `ERHE_FATAL` in copy constructors, and a few public data members that break encapsulation.

## Strengths
- Clean separation between `Node` (transform hierarchy) and `Node_attachment` (mesh, camera, light, etc.)
- Serial-number-based transform dirty tracking avoids redundant recalculations
- `Transform` class stores both forward and inverse matrices, avoiding runtime inversions
- Rich projection API covering orthographic, perspective, frustum, and XR fov types
- Proper clone support with `for_clone` tag dispatch
- `Mesh_layer` / `Light_layer` grouping for efficient rendering by category
- Template `get_attachment<T>()` helper for type-safe attachment retrieval
- `node_sanity_check()` for debugging structural invariants

## Issues
- **[notable]** `Node::Node(const Node&)` and `Node::operator=(const Node&)` both call `ERHE_FATAL("TODO")` (node.cpp:37-38). Any accidental copy will crash the application. These should either be `= delete` or properly implemented.
- **[moderate]** `Node_transforms::s_global_update_serial` is a non-atomic `uint64_t` static (node.cpp:15) with `get_next_serial()` doing `++s_global_update_serial`. This is a data race if nodes are updated from multiple threads.
- **[moderate]** `Light` has many public data members (`type`, `color`, `intensity`, `range`, `inner_spot_angle`, etc. at light.hpp:68-77). This breaks encapsulation -- any code can modify these without notification.
- **[moderate]** `Node_data` is a public member of `Node` (`node_data` at node.hpp:128), exposing internal transform and attachment state directly.
- **[minor]** `Mesh_layer::meshes` is a public `std::vector` (scene.hpp:37) despite having `add()`/`remove()` methods, creating two mutation paths.
- **[minor]** `Scene` copy constructor and assignment operator are declared (scene.hpp:64-65) -- it's unclear if they are properly implemented for deep copying all layers, nodes, cameras, etc.
- **[minor]** `Animation_sampler` has a default constructor (animation.hpp:39) that doesn't initialize `interpolation_mode` to `LINEAR` by default -- it's only initialized in the member declaration.

## Suggestions
- Mark `Node(const Node&)` and `Node::operator=(const Node&)` as `= delete` if they should not be used, rather than crashing at runtime
- Make `s_global_update_serial` a `std::atomic<uint64_t>` for thread safety
- Encapsulate `Light`'s public data members behind setters that can trigger notifications
- Make `Node_data` private or at least expose it through accessor methods
- Make `Mesh_layer::meshes` and `Light_layer::lights` private with only `add()`/`remove()` access
- Consider `= delete` for `Scene` copy constructor/assignment if deep copy is not supported
