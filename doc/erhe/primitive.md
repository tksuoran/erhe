# erhe_primitive

Stability: stable

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
- `Material` -- PBR material: base color, roughness, metallic, emissive, texture samplers. It is a typed prim (`erhe::Typed`, `doc/erhe/item.md` "Prim classes") whose fixed `typeName` token is `Material`, USD's `UsdShadeMaterial`.
- `Triangle_soup` -- raw vertex/index data container (e.g., from glTF import); `mesh_from_triangle_soup()` converts it to a `GEO::Mesh` ("Triangle soup to mesh" below)
- `Triangle_soup_source` -- where a released soup is read back from ("Releasing the triangle soup" below); `Triangle_soup_counts` -- the vertex and index count a shape records for its soup
- `Vertex_colocation` -- per-soup-vertex point ids plus point count: the colocation of a soup's vertices into mesh points
- `Vertex_buffer_writer` / `Index_buffer_writer` -- write vertex attributes and indices to byte buffers; on destruction they call `vertex_writer_ready` / `index_writer_ready` on the owning sink.
- `mesh_optimizer.hpp` -- `optimize_triangle_soup()` / `optimize_triangle_soup_cached()`:
  meshoptimizer passes (vertex weld/remap, vertex-cache order, overdraw order,
  vertex-fetch order) over a copy of a `Triangle_soup`, plus the disk cache for
  import-time results. `meshoptimizer.h` stays private to this target.
- `Index_range` -- offset and count for a specific primitive type within the index buffer
- `Buffer_range` -- byte offset, element count, and element size within a buffer; identifies the owning pool via `{pool_id, buffer_id}`.
- Enums: `Normal_style`, `Primitive_mode`, `Primitive_type`, `Blending_class` ("Blending classes" below)

## Public API
- Create a `Primitive` from a `Geometry` + `Build_info` to get a renderable mesh.
- `build_buffer_mesh()` -- builds vertex/index data from a GEO::Mesh.
- `Vertex_buffer_sink` / `Index_buffer_sink` subclasses control where data goes. The GPU-backed implementation lives in `erhe::scene_renderer::Mesh_memory` (which implements both sink interfaces directly); CPU-backed allocators use `Cpu_vertex_buffer_sink` / `Cpu_index_buffer_sink`.
- `Material` is an `Item` with PBR properties and optional texture samplers. The five slot textures are object properties (`base_color_texture_property` ..., `register_member` over the `Material_data` slot's `texture_reference`, `doc/erhe/property_system.md` section 4.1): write a live material's slot through `set_base_color_texture()` and the other setters, `set_slot_texture()` for a slot held by reference, `set_slot_sampler()` for a slot's `Material_sampler_state`, `set_slot_uv_transform()` for a slot's rotation / offset / scale, `set_slot_texgen_mode()` for a slot's texgen source, or `set_data()` for a whole `Material_data` snapshot; every slot field (texture, transform, texgen source, sampler state) mirrors a property. `Material_sampler_state` is plain data (wrap, filters, mipmap mode, anisotropy, LOD bias); the renderer resolves the GPU `Sampler` from it. `erhe::primitive` links `erhe::graphics` publicly for the sampler enums.
- `Material_data` is closed: `get_data()` returns it const and the functions above are the only write path, so every change to what a material's GPU record and shader variant are built from passes through `on_property_changed`. A slot identifies itself by address, so a caller that holds one passes `get_data().texture_samplers.<slot>` to the setter.
- `Material::get_change_serial()` is a counter that advances on every such change: any property of the material (a local, style, reference, inherited or default change), and a texture graph bake that lands a new texture behind the `Texture_reference` a slot holds. The bake path is `erhe::graphics::Texture_reference_user`: a material registers with a slot's reference while it holds it, and a reference whose resolved texture changes over its lifetime (`editor::Graph_texture`) calls `on_referenced_texture_changed()` on each registered user. A reader that recorded a serial and reads the same one has nothing to re-derive.
- Which channel of a slot's texture each scalar input reads is a material property of its own: `metallic_channel`, `roughness_channel`, `occlusion_channel` and `opacity_channel`, each an `erhe::primitive::Texture_channel` (`r`, `g`, `b`, `a`, whose values are the component index). The defaults are glTF's fixed packing - metallic B and roughness G of the metallic-roughness slot, occlusion R of the occlusion slot, opacity A of the base color slot, erhe having no opacity slot of its own - so a glTF material authors none of them; a `UsdPreviewSurface` names the channel in its connection, which is where a non-default value comes from. The four travel to the shader as one `uvec4 texture_channels` of the material record, in that order, and the shader indexes the sampled texel with them.
- Where the base color and the fragment alpha come from is a material property of its own: `base_color_source` and `opacity_source`, each an `erhe::primitive::Material_input_source`. `value` is the material's own - the factor, times the slot's texture where one is bound, times the mesh's vertex color where the mesh carries one - and is what every glTF material and every plain `UsdPreviewSurface` wants; it is the default and the multiply is unchanged by this property. `vertex_color` is the mesh's color attribute alone: the factor and the texture of that input are not read at all. A `UsdPreviewSurface` input fed by a `UsdPrimvarReader` of `displayColor` / `displayOpacity` is what names it (`doc/erhe/usd.md`); glTF has no form for it, so a glTF material never authors one and the exporter writes nothing new. The pair travels to the shader as the `uvec2 input_sources` of the material record.
- A bound normal texture is decoded as `texel * normal_texture_decode_scale + normal_texture_decode_bias`, two `vec4` properties whose defaults `(2, 2, 2, 2)` and `(-1, -1, -1, -1)` are the mapping glTF fixes and UsdPreviewSurface's fallbacks, so a material that authors neither decodes a 0..1 map into -1..1. USD carries them as the normal slot's `UsdUVTexture` `inputs:scale` and `inputs:bias`; glTF has no field for either, so a glTF import and export leave both at the defaults. The scalar `normal_texture_scale` is a different thing and stays what glTF's `normalTexture.scale` is: the bumpiness multiplier the shader applies to the decoded X and Y.

- `Primitive_shape::add_memory_statistics(out, counted)` sums a shape's CPU bytes into a `Shape_memory_statistics` (triangle soup, element mappings, prepared topology and BVH, stored BVH, and the current, pending and retired raytraces with their backend figures). Objects already in the caller's `counted` set are skipped, so one set over many shapes counts a shared soup or prepared data once; the GPU `Buffer_mesh` ranges are not counted (they belong to the mesh pools). MCP `get_memory_usage` `cpu_mesh` is built from it.

## Skinning weights

The builder writes `WEIGHTS_0` - the set the skinning shader blends with
(`erhe_skinning.glsl`, a plain weighted sum of four joint matrices) -
normalized (`normalize_joint_weights()`): negative and non-finite weights
become 0, the rest are divided by their sum, and a vertex whose sum is
then 0 gets weight 1 on its first influence (slot 0, the joint `JOINTS_0`
names there; a vertex without joint values is built as joint 0 at weight
1). Every built skinned vertex is therefore a convex combination of its
joints' posed images, which `Buffer_mesh::joint_bounding_boxes` relies on:
`include_vertex_in_joint_bounding_boxes()` adds a vertex to the box of
every joint the normalized weights give a positive weight (and, for the
boxes' other consumers, every positive `WEIGHTS_1` joint), at the build and
from in-place vertex edits. Storage keeps the sum exact: unorm8 weights
are rounded to sum to exactly 255 (`quantize_joint_weights_unorm8()`, largest
remainder; a zero weight stays zero), and the unorm16 implicit-sum encoding
(`encode_implicit_sum_joint_weights()`) of a vertex with fewer than four
influences gives the rounding shortfall to the largest stored weight, so
the reconstructed fourth weight is zero. `WEIGHTS_1` is written as
authored (the shader does not read it).

## Blending classes

`Blending_class` (`enums.hpp`) is the draw order class of a
`Material_blending_mode` in the color passes, and
`get_blending_class(Material_blending_mode)` is the one mapping from mode to
class; `get_blending_class(const Material*)` classifies a primitive without
a material as the default material record (`Material_values`, opaque). The
draw lists and the render buckets of `erhe::scene_renderer` both classify
through it, and `blending_class_draw_order` is the order every color pass
that takes several classes draws them in:

| Class | Modes | Depth write | Discard | Blending |
| :--- | :--- | :--- | :--- | :--- |
| `opaque` | `opaque` | yes | no | off |
| `alpha_tested` | `alpha_test`, `screen_door` | yes | yes | off |
| `translucent` | `alpha_blend`, `multiply`, `add`, `subtract` | per pass pipeline | no | premultiplied |

The order exists for the GPU's early depth test (on Apple GPUs, hidden
surface removal): a draw that writes depth and may discard turns it off for
the draws that follow it, so every opaque draw goes first, the alpha-tested
draws after them, and the translucent draws blend over both. The standard
fragment shader evaluates the alpha test and the screen-door dither before
the ambient term and the light loops (`doc/erhe/scene_renderer.md`), and
writes alpha 1 for both discard modes.

## Releasing the triangle soup

A soup shape can give up its CPU `Triangle_soup` once nothing needs it to
build any more, and read it back on demand:

- `Primitive_shape::set_triangle_soup_source()` gives the shape a
  `Triangle_soup_source`, the loader's description of where the soup comes
  from. `load()` reads the soup again, bit-identical to the one the loader
  made, or returns null with an error logged when it cannot (erhe::gltf
  refuses a source file whose size or modification time changed since the
  load; doc/erhe/gltf.md "Triangle soup reload").
- `Primitive_shape::release_triangle_soup()` frees the in-memory soup of a
  shape that has a source and returns true; a shape without a source keeps its
  soup and returns false. `Primitive::release_triangle_soup()` releases the
  render shape's soup and, when the optimized variant is built over the same
  soup (a prepared primitive), the variant's with it, under the same source.
- `get_triangle_soup()` is the one read path of the soup, and returns it by
  value. For a released soup it reads the soup back through the source
  (synchronously; a main-thread caller that is not a one-off user action
  reads it in a task), holds it as a weak reference so every caller that asks
  while one holds it gets the same object, and lets it go with the last
  holder. A reload whose vertex or index count differs from the released soup
  is refused. Every soup reader goes through it: `get_geometry()` /
  `make_geometry()`, the soup path of `make_buffer_mesh()`,
  `make_prepared_raytrace()`, `Primitive::make_prepared_meshes()` (each step
  reads the soup only when it builds, so a repeat call on a built primitive
  reads nothing back), the mesh optimizer, `prepare_primitive_for_export()`
  (the retained core holds the reloaded soup for the export), and the
  exporters.
- `has_triangle_soup()` (resident or released with a source) and
  `get_triangle_soup_counts()` (recorded at construction) never reload; the
  `erhe::scene::Mesh_primitive` count properties read the counts.
- `get_triangle_soup_reload_statistics()` counts the process's reloads, their
  bytes and the refused ones; `Shape_memory_statistics::triangle_soup_released_count`
  counts released soups (once per source).

Tests: `src/erhe/primitive/test/test_triangle_soup_release.cpp`.

## Triangle soup to mesh

`mesh_from_triangle_soup()` (`triangle_soup.hpp`) converts a
`Triangle_soup` into a `GEO::Mesh` plus the soup-index `Element_mappings`
(`triangle_to_mesh_facet`, `mesh_corner_to_vertex_buffer_index`). Only the
vertices the indices reference become mesh points. The conversion has two
halves:

- **Colocation**: which soup vertices share a point (geogram vertex), and
  the point numbering. A `Vertex_colocation` holds it as `vertex_points[v]`,
  the point of soup vertex `v` (`Vertex_colocation::no_point` for a vertex
  no index references), plus `point_count`.
- **Build**: points, facets, corners and attributes from the colocation.
  A triangle whose corners land on fewer than three points is dropped
  (`triangle_to_mesh_facet` holds `GEO::NO_INDEX` for it). Per-point
  attributes (position, joints) take the value of the point's last
  referenced vertex; per-corner attributes (normal, tangent, texcoord,
  color) are copied per corner.

The colocation comes from one of two sources, and the build is the same
code for both:

- `mesh_from_triangle_soup(soup, mesh, mappings)` runs
  `GEO::Geom::colocate()` (tolerance 1e-5) under
  `erhe::geometry::geogram_lock()`. Each vertex's representative is the
  smallest vertex index of its cluster, and the points are numbered by
  ascending representative, which is the order of first reference by
  ascending vertex index. The overload with a `Vertex_colocation&` out
  parameter also returns the colocation it used.
- `mesh_from_triangle_soup(soup, vertex_points, point_count, mesh, mappings)`
  takes the colocation from the caller (the `ERHE_mesh_topology` glTF
  payload, `doc/gltf_extensions/ERHE_mesh_topology.md`). It takes no
  geogram lock and runs no geogram algorithm, so conversions of different
  soups run fully in parallel. It validates before it builds:
  `vertex_points` has one entry per soup vertex; every index is below the
  vertex count; every referenced vertex names a point below `point_count`;
  every point is named by a referenced vertex; an unreferenced vertex
  carries `no_point` or an id below `point_count`; the soup has one vertex
  stream with a position attribute. On any failure it returns false and
  leaves the mesh and the mappings untouched. The point order is the
  caller's; the extension's first-reference order check belongs to the
  loader.

Given the colocation the colocate path returned, the supplied path builds
a bit-identical mesh and mappings, and `Geometry::process()` then gives
identical edges and smooth vertex normals
(`src/erhe/primitive/test/test_stored_colocation.cpp`).

## Export preparation

`prepare_primitive_for_export(shape)` (`prepared_primitive.hpp`) turns a
soup primitive into the saved core of `doc/plans/gltf_fast_load.md` ("Saved
core primitive") plus the payloads of `ERHE_mesh_optimized`,
`ERHE_mesh_topology` and `ERHE_raytrace_bvh`, as a
`Prepared_primitive_export`:

1. Gets the shape's `Geometry` (`get_geometry()`, which builds it if
   missing) and refuses a facet that is not a triangle.
2. Runs the content build (`build_buffer_mesh()`,
   `Normal_style::corner_normals`, fill triangles) into CPU buffers with a
   float vertex format: POSITION, NORMAL, TANGENT always, TEXCOORD_0..2,
   COLOR_0..1, JOINTS_0..1 (the soup's uint8 / uint16 width) and
   WEIGHTS_0..1 as the soup carries them. Every corner therefore carries
   exactly what the build writes: the colocated point position, the
   tangent computed when the soup had none, normalized WEIGHTS_0. A soup
   with a set the build does not write (TEXCOORD_3, COLOR_2, JOINTS_2, a
   gap such as TEXCOORD_1 without TEXCOORD_0) or another JOINTS format is
   refused and exports as its raw soup.
3. Drops triangles with two bitwise-identical corner positions, then runs
   `optimize_indexed_mesh()` (bitwise weld, vertex cache, overdraw 1.05,
   vertex fetch).
4. `vertex_points[v]` is the geogram point of the first built vertex the
   weld mapped to output vertex `v`, renumbered in first-reference order.
   `mesh_from_triangle_soup()` over the core with these `vertex_points` and
   with `colocate()` builds the same mesh bit for bit, and facet `t` is core
   triangle `t`.
5. `compute_mesh_content_hash()` and `erhe::math::calculate_bounding_volume()`
   over the core give `content_hash` and the bounding sphere.
6. The BVH is the shape's committed real raytrace BVH when its triangles map
   1:1 onto the core triangles (through `Primitive_raytrace` facet ids and the
   optimizer's triangle permutation) and hash to the committed
   `triangle_hash` in raytrace order: its primitive ids are remapped and the
   hash recomputed with `erhe::raytrace::compute_triangle_hash()`
   (`Prepared_bvh_source::reused`). Otherwise a BVH is built over the core
   (`rebuilt`). Backends without BVH export give `none`.

Shapes are prepared in parallel; one shape by one caller at a time. Tests:
`src/erhe/primitive/test/test_prepared_primitive.cpp`.

## Deferred payload

`Primitive(source, counts)` / `Primitive_shape(source, counts)` make a soup
shape whose payload is not read yet (a deferred glTF load,
doc/editor/async_asset_loading.md "Mesh build jobs"): only the
`Triangle_soup_source` and the vertex / index counts are known, and the
shape reads as released (`is_payload_pending()`). `load_payload()` reads the
soup (`Triangle_soup_source::load()`) and its validated prepared mesh data
(`Triangle_soup_source::load_prepared_data()`, null by default and for a
rejected marker); afterwards the shape is what the soup + prepared data
constructor makes. It refuses, with an error logged, a soup that cannot be
read or whose counts differ, and leaves the payload pending. It runs on a
loader worker before the shape is built or shared (the prepared data slot is
not locked); a second call does nothing. Tests:
`Gltf_roundtrip.deferred_mesh_payload_loads_like_the_parse`
(`src/erhe/gltf/test/test_gltf_roundtrip.cpp`).

## Prepared mesh data on load

A soup primitive loaded with accepted prepared mesh data carries it as an
immutable `Prepared_primitive_data` on its render shape
(`Primitive(soup, prepared_data)`, `Primitive_shape::get_prepared_data()`,
null for every other shape):

- `options`, `content_hash`, `bounding_box` (the POSITION accessor min /
  max) and `bounding_sphere` from `ERHE_mesh_optimized`;
- `colocation` (`Vertex_colocation`, first-reference order) when
  `ERHE_mesh_topology` was accepted;
- `bvh` (`erhe::raytrace::Prebuilt_bvh` for the running backend and library
  tag) when `ERHE_raytrace_bvh` was accepted.

The data describes the shape's soup exactly: the soup is the core the file
stores. The content checks of the specs' "Validation / fallback" sections
are `validate_prepared_core()` (index count and range, every vertex
referenced, no degenerate triangle, content hash) and
`validate_vertex_points()` (size, first-reference order, point count,
bitwise-identical positions per point); both return a fixed reason phrase,
or an empty string when the input passes.

`Primitive::make_prepared_meshes(build_info)` is the load build of such a
primitive, with no Geometry:

- the base render shape gets the fill-only soup buffer mesh
  (`build_buffer_mesh_from_triangle_soup()` with `build_info.buffer_info`);
- when `buffer_info.optimize_meshes` and `optimized_vertex_format` are set,
  the optimized variant is a `Primitive_render_shape` over the same core soup
  with empty `Element_mappings`, built into the optimized vertex format and
  published with `publish_optimized_render_shape()`: the core already is in
  meshoptimizer order, so this is one linear pack and no meshoptimizer call;
- `Primitive_shape::make_prepared_raytrace()` builds
  `Primitive_raytrace(soup, &bvh)`, which offers the stored BVH to the
  backend before the commit (`IGeometry::set_prebuilt_bvh()`) and maps
  triangle `t` to facet `t`. It is installed only when the backend used the
  stored BVH (`Primitive_raytrace::get_bvh_source()` is `prebuilt`);
  otherwise nothing is installed and the caller takes the proxy path.

It is idempotent and has the threading of `make_renderable_mesh()`; a step
that is already built reads nothing, so a repeat call on a primitive whose
soup was released does not read it back. The
`Prepared_primitive_data::bvh` moves into the shape at construction
(`has_stored_bvh()`) and from there into the raytrace backend, so no second
copy of the BVH stays in memory.

`get_geometry()` on a prepared shape builds the `Geometry` from the soup with
the stored colocation (`mesh_from_triangle_soup()` with `vertex_points`, no
`colocate()`, no geogram lock), then runs `Geometry::process()` as for any
soup; a refused colocation falls back to `colocate()` with a warning.

`prepare_primitive_for_export()` retains a shape whose prepared data is
current (`is_prepared_data_current()`): it returns the shape's soup as
`retained_core`, the stored colocation, marker values and the BVH read back
from the raytrace (`IGeometry::export_bvh()`, `Prepared_bvh_source::stored`),
with no `Geometry`, no content build and no meshoptimizer.
`get_export_core()` gives the core to write. An in-place content edit marks
the data stale (`mark_prepared_data_stale()`, called by
`erhe::scene::Mesh::begin_optimized_variant_edit()`, which every GPU vertex
edit - paint, weight paint, mesh component drags - takes), and the export then
prepares the shape afresh; an edit that replaces the primitive makes a new
shape without prepared data.
Tests: `Prepared_primitive.prepared_meshes_build_without_geometry`.

## Empty primitives

A `Geometry` with no facets is a legal source: mesh operations produce one when
every face is deleted or a merge by distance collapses the whole mesh, and the
mesh keeps it as its primitive (the way Blender keeps an empty mesh object).
A `Primitive` built from it is a valid empty primitive that renders and
raytraces nothing:

- `build_buffer_mesh()` (and so `Primitive::make_renderable_mesh()`) succeeds
  with an empty `Buffer_mesh`: no vertex or index buffer ranges, no
  allocations, every `Index_range` empty. Its bounding box and sphere cover the
  mesh vertices (a lone vertex left by a merge gives a point box; no vertices
  give the point box at the origin). Draw lists, the ID renderer, the lightmap
  baker and the ray trace TLAS read the empty ranges as "nothing to draw".
- `Primitive_raytrace` of a mesh with no fill triangles holds no buffers and no
  `IGeometry`; `Primitive::make_raytrace()` succeeds and installs it, and
  `erhe::scene::Mesh::update_rt_primitives()` creates no raytrace instance for
  it. `has_renderable_triangles()` and `has_raytrace_triangles()` are false.
- `Cpu_buffer` has no zero-size state (its constructor verifies a non-zero
  capacity), so the empty builds create none; no pool allocation of zero
  elements is made either.
- The glTF exporter writes no primitive for such a geometry, and a mesh left
  with no exportable primitive is written as a node without a mesh
  (`doc/erhe/gltf.md`).

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
- Raytrace geometry is built separately from render geometry, using CPU buffers. `Primitive_raytrace` builds its own vertex / index copies, commits them to the backend and then frees them when the backend does not read them after the commit (`erhe::raytrace::IGeometry::release_buffers()`: true for the `bvh` and `tinybvh` backends, false for `embree`, which shares them). Its `Buffer_mesh` keeps the ranges, index ranges and bounds (the developer Properties "Raytrace Buffer Mesh" view and `Primitive::get_bounding_box()` read them) and drops its allocations first (`Buffer_mesh::release_buffer_allocations()`). A raytrace built from a soup maps triangle t to facet t through a flag, with no table; a raytrace built from a `GEO::Mesh` keeps its triangle-to-facet table.
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
  edit's commit re-optimizes in the background. See `doc/erhe/meshoptimizer_integration.md`.
