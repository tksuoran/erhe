# erhe_scene_renderer -- Code Review

## Summary
A high-quality rendering layer that bridges `erhe::scene` with `erhe::graphics`, providing GPU buffer management for cameras, lights, materials, primitives, joints, and shadow maps. The `Forward_renderer` and `Shadow_renderer` form a complete forward rendering pipeline. The architecture cleanly separates interface definitions (shader resource layouts) from buffer management (ring buffer updates) and rendering (draw command emission). Well-designed overall with good use of modern OpenGL abstractions.

## Strengths
- Clean separation between `*_interface` (shader resource layout) and `*_buffer` (data upload) classes for each concept (camera, light, material, primitive, joint)
- `Program_interface` centralizes shader resource definitions, making it easy to keep CPU and GPU layouts in sync
- `Forward_renderer::Render_parameters` aggregates all rendering state into a single parameter object, avoiding long parameter lists
- Ring buffer pattern for all GPU data uploads avoids synchronization stalls
- Shadow map rendering with per-light projection fitting
- `Light_projections` properly handles directional and spot light shadow mapping
- Texture heap management for bindless textures

## Issues
- **[moderate]** `Forward_renderer` owns many heavy objects (6 buffer classes, 3 samplers, a texture, a texture heap) all initialized in the constructor's member initializer list. The constructor in `forward_renderer.cpp` is extremely long (80+ lines of initializer list). Consider a builder or init method.
- **[moderate]** `Shadow_renderer` duplicates several buffer classes that `Forward_renderer` also owns (`Joint_buffer`, `Light_buffer`, `Material_buffer`, `Primitive_buffer`). This means shadow rendering and forward rendering use separate GPU ring buffers, doubling memory usage.
- **[minor]** `Primitive_interface_settings` is referenced in `forward_renderer.hpp:68` but not defined in any visible header here -- it's presumably in `primitive_buffer.hpp` but the coupling is not obvious.
- **[minor]** `buffer_binding_points.hpp` exists but was not read -- the binding point assignment strategy should be documented.
- **[minor]** `Shadow_renderer` has a `Pipeline_cache_entry` with a `serial` field for cache invalidation -- the cache invalidation logic could benefit from documentation.

## Suggestions
- Consider sharing buffer instances between `Forward_renderer` and `Shadow_renderer` to reduce GPU memory duplication
- Break the `Forward_renderer` constructor into a constructor and an `init()` method for readability
- Add a brief comment to `Program_interface_config` explaining the max counts and their GPU memory implications
- Document the pipeline cache invalidation strategy in `Shadow_renderer`
