# erhe_primitive

## Purpose
Converts geometric data (from `erhe::geometry` Geometry or Triangle_soup) into
GPU-ready vertex/index buffers. Handles vertex attribute layout, buffer allocation,
index generation for multiple primitive modes (fill, wireframe, points), bounding
volume computation, and PBR material definitions.

## Key Types
- `Primitive` -- top-level type: owns render shape and collision shape, provides bounding box
- `Primitive_render_shape` -- holds the renderable `Buffer_mesh` built from geometry
- `Primitive_shape` -- holds source geometry, raytrace data, and element mappings
- `Buffer_mesh` -- the built result: buffer ranges, index ranges, bounding box/sphere, plus `vertex_input_key`. Move-only; holds `Buffer_allocation` RAII handles that free GPU/CPU buffer space on destruction.
- `Vertex_buffer_sink` / `Index_buffer_sink` -- abstract interfaces for allocating buffer space (GPU or CPU). Each returns `Buffer_sink_allocation` containing both `Buffer_range` (plain data) and `Buffer_allocation` (RAII handle). Range-based API: `allocate_vertex_buffer_range(Vertex_stream, count)` / `allocate_index_buffer_range(Format, count)`.
- `Buffer_sink_allocation` -- pairs a `Buffer_range` with a `Buffer_allocation` for reclaimable allocation.
- `Cpu_vertex_buffer_sink` / `Cpu_index_buffer_sink` -- CPU-memory implementations of the sink interfaces; used by `Primitive_raytrace` and the glTF importer.
- `Build_info` / `Buffer_info` -- configuration for mesh building (primitive types, vertex format, index type). `Buffer_info` carries both `vertex_buffer_sink` and `index_buffer_sink` references plus a `vertex_input_key`.
- `Primitive_builder` / `Build_context` -- orchestrates the conversion from GEO::Mesh to Buffer_mesh
- `Material` -- PBR material (extends `erhe::Item`): base color, roughness, metallic, emissive, texture samplers
- `Triangle_soup` -- raw vertex/index data container (e.g., from glTF import)
- `Vertex_buffer_writer` / `Index_buffer_writer` -- write vertex attributes and indices to byte buffers; on destruction they call `vertex_writer_ready` / `index_writer_ready` on the owning sink.
- `Index_range` -- offset and count for a specific primitive type within the index buffer
- `Buffer_range` -- byte offset, element count, and element size within a buffer; identifies the owning pool via `{pool_id, buffer_id}`.
- Enums: `Normal_style`, `Primitive_mode`, `Primitive_type`

## Public API
- Create a `Primitive` from a `Geometry` + `Build_info` to get a renderable mesh.
- `build_buffer_mesh()` -- builds vertex/index data from a GEO::Mesh.
- `Vertex_buffer_sink` / `Index_buffer_sink` subclasses control where data goes. The GPU-backed implementation lives in `erhe::scene_renderer::Mesh_memory` (which implements both sink interfaces directly); CPU-backed allocators use `Cpu_vertex_buffer_sink` / `Cpu_index_buffer_sink`.
- `Material` is an `Item` with PBR properties and optional texture samplers.

## Dependencies
- `erhe::geometry` -- source `Geometry` type
- `erhe::dataformat` -- `Vertex_format`, `Format` enums
- `erhe::math` -- `Aabb`, `Sphere`
- `erhe::item` -- `Item` base for `Material`
- `erhe::graphics` -- `Texture`, `Sampler` (in Material)
- `erhe::raytrace` -- `IGeometry` (for raytrace mesh)
- `erhe::buffer` -- `Cpu_buffer`
- `erhe::profile` -- profiling mutex
- External: Geogram (GEO::Mesh), glm

## Notes
- The build pipeline supports multiple vertex buffer streams (multi-stream vertex layouts).
- Element mappings track the relationship between triangles and source mesh facets, enabling picking.
- Raytrace geometry is built separately from render geometry, using CPU buffers.
- The builder generates indices for four primitive modes: triangle fill, edge lines, corner points, and polygon centroids.
- `Buffer_mesh` is move-only (due to `Buffer_allocation`). `Primitive_render_shape`, `Primitive_shape`, and `Primitive_raytrace` are also move-only.
- **Member declaration order matters**: In `Primitive_raytrace`, `m_rt_mesh` must be declared after the `Cpu_buffer` shared_ptrs so it is destroyed first, freeing allocations while the allocator is still alive.
