# erhe_primitive -- Code Review

## Summary
A large library responsible for converting geometry data (from `erhe::geometry`/Geogram meshes) into GPU-ready vertex/index buffers. It includes a primitive builder pipeline, buffer sink abstraction (GPU and CPU backends), material management integrated with the item system, and ray tracing geometry support. The architecture is well-structured with clear data flow, though the complexity of the builder pipeline makes it dense.

## Strengths
- Clean `Buffer_sink` abstraction with GPU (`Graphics_buffer_sink`) and CPU (`Cpu_buffer_sink`) implementations, enabling both rendering and raytrace/physics use cases
- `Build_context` systematically handles all vertex attributes (position, normal, tangent, texcoord, joint data, etc.) with proper fallbacks for missing data
- `Primitive` has a good separation between render shape and collision shape, allowing different geometry for each
- `Buffer_mesh` compactly stores all buffer ranges and bounding volumes
- `Material` integrates with the item system via CRTP, enabling selection, naming, and cloning
- PBR material model with full texture sampler support (base color, metallic-roughness, normal, occlusion, emissive)
- Proper use of move semantics in buffer data transfer

## Issues
- **[moderate]** `Build_context::Vertex_writers` (primitive_builder.hpp:137-153) stores raw `Vertex_buffer_writer*` pointers that point into the `vertex_writers` vector of `unique_ptr<Vertex_buffer_writer>`. If `vertex_writers` is resized after these pointers are set, they become dangling. The current code likely avoids this by completing setup before use, but it is fragile.
- **[moderate]** `Primitive` (primitive.hpp:106-141) has a large number of constructors (7 constructors plus copy/move). This indicates the class may be doing too much. Consider using a builder pattern or reducing constructor overloads.
- **[minor]** `Primitive_render_shape` publicly inherits from `Primitive_shape` but stores an additional `Buffer_mesh`. The naming is somewhat confusing -- "shape" implies geometry but this class also owns GPU buffer references.
- **[minor]** `Buffer_range` (referenced from buffer_mesh.hpp) uses default-initialized members. An invalid/empty range is represented by default values, but there is no explicit `is_valid()` method to check.
- **[minor]** `Material::material_buffer_index` and `preview_slot` are public mutable members (material.hpp:81-82) updated externally by `Material_buffer::update()`. This breaks encapsulation.
- **[minor]** `Element_mappings` is forward-declared and used as a pointer parameter in some places but as a reference in others. Consistency would improve readability.
- **[minor]** `Build_context_root::build_failed` is a public `bool` flag (primitive_builder.hpp:64). Consider returning error codes or using `std::expected`/`std::optional` from build functions instead.

## Suggestions
- Guard `Vertex_writers` raw pointers against invalidation by either reserving the `vertex_writers` vector upfront or by using indices instead of pointers.
- Consider a builder/factory pattern for `Primitive` construction to reduce the number of constructors.
- Add an `is_valid()` method to `Buffer_range`.
- Make `Material::material_buffer_index` and `preview_slot` private with accessor methods, or document why external mutation is required.
- Consider renaming `Primitive_render_shape` to something like `Renderable_primitive` to better convey that it owns both shape and buffer data.
