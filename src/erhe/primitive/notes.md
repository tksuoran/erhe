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
- `Buffer_mesh` -- the built result: buffer ranges, index ranges, bounding box/sphere, plus `vertex_input_key`. Move-only; holds `Buffer_allocation` RAII handles that free GPU/CPU buffer space on destruction. Also carries `joint_bounding_boxes` -- per-joint rest-pose AABBs (indexed like the JOINTS_n attribute), built by `Build_context_root::calculate_joint_bounding_volumes()` from the geometry's joint attributes, empty for unskinned geometry. `erhe::scene::Mesh::get_aabb_world()` needs them: the whole-mesh `bounding_box` is the rest pose and glTF forbids applying the mesh node transform to a skinned mesh, so only the joints can bound it.
- `Vertex_buffer_sink` / `Index_buffer_sink` -- abstract interfaces for allocating buffer space (GPU or CPU). Each returns `Buffer_sink_allocation` containing both `Buffer_range` (plain data) and `Buffer_allocation` (RAII handle). Range-based API: `allocate_vertex_buffer_range(Vertex_stream, count)` / `allocate_index_buffer_range(Format, count)`.
- `Buffer_sink_allocation` -- pairs a `Buffer_range` with a `Buffer_allocation` for reclaimable allocation.
- `Cpu_vertex_buffer_sink` / `Cpu_index_buffer_sink` -- CPU-memory implementations of the sink interfaces; used by `Primitive_raytrace` and the glTF importer.
- `Build_info` / `Buffer_info` -- configuration for mesh building (primitive types, vertex format, index type). `Buffer_info` carries both `vertex_buffer_sink` and `index_buffer_sink` references plus a `vertex_input_key`.
- `Primitive_builder` / `Build_context` -- orchestrates the conversion from GEO::Mesh to Buffer_mesh
- `Material` -- PBR material: base color, roughness, metallic, emissive, texture samplers. It is a typed prim (`erhe::Typed`, `src/erhe/item/notes.md` "Prim classes") whose fixed `typeName` token is `Material`, USD's `UsdShadeMaterial`.
- `Triangle_soup` -- raw vertex/index data container (e.g., from glTF import)
- `Vertex_buffer_writer` / `Index_buffer_writer` -- write vertex attributes and indices to byte buffers; on destruction they call `vertex_writer_ready` / `index_writer_ready` on the owning sink.
- `mesh_optimizer.hpp` -- `optimize_triangle_soup()` / `optimize_triangle_soup_cached()`:
  meshoptimizer passes (vertex weld/remap, vertex-cache order, overdraw order,
  vertex-fetch order) over a copy of a `Triangle_soup`, plus the disk cache for
  import-time results. `meshoptimizer.h` stays private to this target.
- `Index_range` -- offset and count for a specific primitive type within the index buffer
- `Buffer_range` -- byte offset, element count, and element size within a buffer; identifies the owning pool via `{pool_id, buffer_id}`.
- Enums: `Normal_style`, `Primitive_mode`, `Primitive_type`

## Public API
- Create a `Primitive` from a `Geometry` + `Build_info` to get a renderable mesh.
- `build_buffer_mesh()` -- builds vertex/index data from a GEO::Mesh.
- `Vertex_buffer_sink` / `Index_buffer_sink` subclasses control where data goes. The GPU-backed implementation lives in `erhe::scene_renderer::Mesh_memory` (which implements both sink interfaces directly); CPU-backed allocators use `Cpu_vertex_buffer_sink` / `Cpu_index_buffer_sink`.
- `Material` is an `Item` with PBR properties and optional texture samplers. The five slot textures are object properties (`base_color_texture_property` ..., `register_member` over `data.texture_samplers.<slot>.texture_reference`, `doc/property-system.md` section 4.1): write a live material's slot through `set_base_color_texture()` and the other setters, `set_slot_texture()` for a slot held by pointer, `set_slot_sampler()` for a slot's `Material_sampler_state`, `set_slot_uv_transform()` for a slot's rotation / offset / scale, or `set_data()` for a whole `Material_data` snapshot; every slot field (texture, transform, sampler state) mirrors a property. `Material_sampler_state` is plain data (wrap, filters, mipmap mode, anisotropy, LOD bias); the renderer resolves the GPU `Sampler` from it. `erhe::primitive` links `erhe::graphics` publicly for the sampler enums.
- A bound normal texture is decoded as `texel * normal_texture_decode_scale + normal_texture_decode_bias`, two `vec4` properties whose defaults `(2, 2, 2, 2)` and `(-1, -1, -1, -1)` are the mapping glTF fixes and UsdPreviewSurface's fallbacks, so a material that authors neither decodes a 0..1 map into -1..1. USD carries them as the normal slot's `UsdUVTexture` `inputs:scale` and `inputs:bias`; glTF has no field for either, so a glTF import and export leave both at the defaults. The scalar `normal_texture_scale` is a different thing and stays what glTF's `normalTexture.scale` is: the bumpiness multiplier the shader applies to the decoded X and Y.

## Dependencies
- `erhe::geometry` -- source `Geometry` type
- `erhe::dataformat` -- `Vertex_format`, `Format` enums
- `erhe::math` -- `Aabb`, `Sphere`
- `erhe::item` -- `Item` base for `Material`
- `erhe::graphics` -- `Texture_reference`, the sampler enums (in Material)
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
- Mesh optimization (`optimize_meshes`, on by default in the editor config) builds an
  additional fill-only, welded `Primitive_render_shape` variant per unique `Primitive`
  (`Primitive::optimized_render_shape`), in a separate vertex format without the
  per-corner facet-id attribute and with the ONLY quantized position (the base variant
  always stores float3, so in-place GPU edits never clamp). The original variant is
  always built and remains the one used by ID rendering, ray tracing, export, and
  vertex editing. A live edit drops the optimized variant at edit start and takes an
  optimization hold on the `Primitive`; all attaches go through
  `Primitive::publish_optimized_render_shape()`, which refuses under a hold, and the
  edit's commit re-optimizes in the background. See `doc/meshoptimizer-integration.md`.
