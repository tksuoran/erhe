# Vertex position quantization

Stability: experimental

Vertex positions of the **meshoptimizer optimized variant** are stored as
`format_16_vec3_snorm` (6 bytes) normalized into the primitive's object-space
AABB, and dequantized in the vertex shader from that AABB. The content (base)
formats always store `format_32_vec3_float`: the base variant is the
always-renderable build that in-place GPU edits write through, and a float
position can express any value, so there is no out-of-AABB edit clamp by
construction. See `doc/erhe/meshoptimizer_integration.md` ("Position quantization
(optimized variant only)") for how the two variants relate.

The encoding is a **shader key axis**, so passthrough and quantized meshes
coexist and each draw compiles and selects the matching shader variant. The
axis is named for the concrete storage format - `snorm16x3_aabb` - so a future
`snorm16x4_aabb` (padded, or with a payload in `w`) is a new enumerator rather
than a reinterpretation of this one.

## What it saves

Stride is a property of the **`Vertex_stream`**, not of an attribute:
`Vertex_stream::stride` is the per-vertex step for the whole binding, and each
`Vertex_attribute` carries only an `offset` into it. Attributes are packed in
declaration order, each aligned up to its own component size, and the stream
stride is padded once at the end. A quantized position therefore packs
directly against whatever follows it in the same stream, and stride padding is
paid once per vertex per stream, not per attribute.

`Mesh_memory` additionally requires a stream stride that is a multiple of 4
(see 1.1), so the optimized stream-0 strides are 8 (not skinned) and 16
(skinned):

| stream | float3 | quantized | saving |
| --- | --- | --- | --- |
| stream 0, not skinned (`position`) | 12 B | 8 B | 33 % of stream 0 |
| stream 0, skinned (`position` + `joint_indices` + `joint_weights`) | 20 B | 16 B | 20 % of stream 0 |

**What this win is and is not.** It is a reduction in vertex-pool footprint
and in the bytes fetched for stream 0 on every pass that reads positions -
which is every pass. It is **not** a "position-only pass reads less" win:
there is no position-only vertex-input variant. `bucket_vertex_ranges()`
returns all of a mesh's vertex ranges, the draw-list recorder records every
one of them regardless of `Draw_purpose`, and the binding step binds them all,
so the shadow and depth-only passes bind streams 1..3 as well. ID render is
not position-only either: `standard.vert` reads `a_custom_0`, which lives in
stream 2. Quote the win as "stream 0 shrinks by 33 % / 20 %", not as a
per-pass bandwidth figure.

Measured at scene scale, with quantization on versus off: the stream-0 pool
holds 33.7 % fewer bytes with Sponza loaded and 33.3 % fewer for two skinned
rigs.

## 1. The encoding

Per primitive, with object-space AABB `[min, max]`:

```
center      = 0.5 * (min + max)
half_extent = 0.5 * (max - min)                        // per axis, >= 0
scale       = max(half_extent, epsilon)                // stored
encoded     = clamp((p - center) / scale, -1, 1)       // snorm16 x, y, z
decoded     = encoded * scale + center
```

Encoder and decoder use the *same* `scale`, so a degenerate axis
(`half_extent == 0`, a flat quad) encodes to exactly 0 and decodes back to
`center` exactly - the `epsilon` never contributes, because it is multiplied
by zero.

The quantization step per axis is `half_extent / 32767`; worst-case error is
half a step, since the encoders round to nearest. That is a 30 um step for a
2 m mesh and 3 mm for a 200 m mesh in a single primitive, which is why the
switch is per format rather than unconditional.

`format_16_vec3_snorm` is the storage format: the smallest that holds the
encoding, already wired through `write_low3` and the backend vertex-input
tables, and it keeps `a_position` a `vec3` in the shader.

**Far from the origin**, `ulp(500 m)` is about the size of the quantization
step itself, so the effective worst-case error for a primitive centred that
far out is roughly twice the nominal half-step. Encoder and decoder pay it
symmetrically, so they never disagree - but that is the regime where the
Z-fighting risk below bites.

### 1.1 Alignment

Two separate alignment questions follow from the 2-byte format.

**Stream stride.** GL and Vulkan core impose no stride alignment (only
`maxVertexInputBindingStride`); Metal requires a stride that is a multiple of
4. `Mesh_memory` requires a multiple of 4 on every backend anyway, for two
reasons of its own:

- `Buffer_pool` aligns each allocation to its own stream stride, and the
  lockstep invariant needs `byte_offset / stride` to advance identically
  across streams whose strides differ. Aligning stream 0 to `lcm(14, 4) = 28`
  instead - the obvious way to get a 4-byte aligned range start - pads a
  14-byte stream by two elements and a 68-byte stream by one, and the two
  diverge; a skinned glTF import then fails with "vertex stream allocations
  out of lockstep". A 4-byte-multiple stride keeps alignment == stride, which
  is what the invariant needs, and gets the aligned range start for free.
- The ray-tracing instance records hand the stream-0 range address to a GLSL
  buffer reference declared `buffer_reference_align = 4`. A range starting at
  2 mod 4 would read every position 2 bytes off.

So the unpadded 6 / 14 layout is not reachable until both constraints are
solved, and neither is a backend limitation.

The mechanism is `erhe::dataformat::Vertex_stream_packing`, which carries
`min_attribute_alignment` and `min_stride_alignment`, both defaulting to 1
(no constraint). `Vertex_stream::repack()` / `Vertex_format::repack()`
re-derive every offset and the stride under a given packing, and
`Mesh_memory`'s constructor calls `repack()` on all of its formats, before
anything can capture a stride. Repacking at the `Mesh_memory` boundary is
deliberate: the `Vertex_format` constructors are shared with *source-side*
formats (`Triangle_soup`'s in the glTF importer, `Primitive_raytrace`'s
float3-only one), which must not be subject to a backend's vertex-buffer
packing rules. Two consequences:

- `repack()` always finalizes the stride, so it can differ from an
  *un*finalized `emplace_back()`-built stream.
- The constraint is applied at one site, not enforced globally. The other
  GPU-facing vertex formats (the lightmap baker's seam format,
  `content_wide_line_interface`'s, the debug / ImGui / hextiles renderers')
  bypass it. All of them are 32-bit float throughout, so every stride is
  already a multiple of 4 and a Metal-style rule is satisfied by accident -
  an accident, not an invariant. Revisit if any of them gains an 8- or 16-bit
  attribute.

**Attribute offsets.** Independently, the quantized skinned stream-0 attribute
offsets are 6 (`joint_indices`) and 10 (`joint_weights`), because both joint
attributes have component alignment 1. Neither is 4-byte aligned, and a
stride-only rule does not fix it. The hook is
`Device_info::min_vertex_attribute_alignment`, which no backend assigns today,
so it stays 1 (see the plan for the Metal question it leaves open).

## 2. Shader key axis

`erhe::dataformat::Vertex_position_encoding` (in `vertex_format.hpp`, beside
the vertex format machinery, because the encoding is a property of the format)
has `passthrough = 0` and `snorm16x3_aabb = 1`, with `snorm16x4_aabb`
reserved. `get_vertex_position_encoding(vertex_format)` is the one mapping
from a format to a value.

`Shader_key::derive()` sets the `VERTEX_POSITION_ENCODING` axis from the
vertex format's position attribute, **unconditionally** - not only the
non-zero case: `derive()` copies `int_values` from `*this`, so a stale
non-zero encoding on an environment key would otherwise be carried through.
`derive(material, nullptr, ...)` - the material-identity hash - pins the axis
to 0, which is what that hash wants.

**The axis doubles the reachable variant space**, but only one value is
reachable per vertex format, so live variant count grows only by the number of
distinct formats actually in play.

### 2.1 Keys built without `derive()`

`derive()` is not sufficient on its own: two places build a `Shader_key` from
scratch and fill it partly, and both carry the axis explicitly.

- **`Draw_purpose::shadow`** classification builds a fresh `Shader_key` with
  only `USE_SKINNING` copied back from the derived key. The shadow key keeps
  exactly those axes that describe how to *get to* a position, and the
  encoding is one of them; without it, every shadow draw of a quantized mesh
  would use a passthrough shader and rasterize the caster from raw [-1, 1]
  positions.
- **The shadow-renderer prewarm** hand-builds keys while passing the vertex
  format alongside. `Shader_variant_cache::get()` keys its map on the
  `Shader_key` alone (the vertex format is only used at compile time), so a
  key that did not carry the axis would prewarm encoding-0 shadow variants
  while every real shadow draw missed the cache and compiled on the render
  thread. The prewarm walks the content and optimized formats side by side,
  one variant per {skinning state, position encoding}.

Any new `Shader_key{}` construction that is then partly filled needs the same
treatment; the "correct by construction" property holds only for call sites
that use `derive()`'s result whole.

## 3. Primitive buffer: the AABB record

`Primitive_struct` carries two `vec4`s, `position_scale` (xyz = scale, the
half extent clamped to epsilon) and `position_offset` (xyz = center),
registered in `Primitive_interface`. They are stored as scale / offset rather
than raw min / max because the decode is then one `fma`.

`Primitive_interface(..., max_primitive_count)` sizes the block from the
struct size, so growing the struct shrinks primitives per ring-buffer block -
but **only on the UBO path**; the SSBO path uses `Shader_resource::unsized_array`
and leaves `max_primitive_count` alone. The UBO branch logs the resulting
primitives-per-block. Every Vulkan and Metal device takes the SSBO path
(`use_shader_storage_buffers` is set unconditionally there), so the UBO branch
is reachable only on the OpenGL backend without compute shaders - exercise it
by forcing that backend, not by reaching for a mobile device.

**Rejected alternative:** folding the dequantization affine into
`world_from_node` costs zero bytes, but it is wrong for skinned meshes
(`erhe_skin_matrices()` *builds* `world_from_node` from the joint palette, so
there is nothing to fold into), and it corrupts `v_node_position` plus the
corner-cap paths that need real object-space positions. The AABB stays
explicit.

### 3.1 The four write sites

The decode lives behind `#if ERHE_VERTEX_POSITION_ENCODING == ...`, so a
passthrough variant never reads these fields. What matters is that every path
that can produce a *quantized* draw writes them:

1. `Primitive_buffer::update(std::span<const std::shared_ptr<Mesh>>, ...)` -
   writes from `buffer_mesh->bounding_box`.
2. `Primitive_buffer::write_primitive(...)` - same.
3. `Primitive_buffer::update(std::span<const std::shared_ptr<Node>> ...)` -
   this site has no `Buffer_mesh`, no primitive and no material at all; it
   iterates `Node`s and can only feed passthrough draws, so it writes the
   identity `scale = (1,1,1,0)`, `offset = (0,0,0,0)`.
4. `Draw_list_scene::write_entry_record()` - the default path, since
   `use_draw_lists` is on. It `memset`s the record and then writes the fields
   it owns; `Primitive_buffer::update()`'s fast path `memcpy`s that record
   verbatim and patches only `color` and `size`. Left unwritten, every
   quantized mesh would get `scale = offset = (0,0,0,0)` and collapse to a
   point at the origin. The AABB is static per `Buffer_mesh`, so it is written
   once per entry next to `base_vertex`. This site does not abort when the
   primitive has no renderable `Buffer_mesh`: `refresh_object_records()`
   replays entries against a scene that may have been mutated since, so it
   falls back to the identity affine.

## 4. The encoder: both build paths

There are two paths from source data to a `Buffer_mesh`, selected on
`m_geometry_published`, and both encode.

### 4.1 `Primitive_builder` (published geometry)

The favourable path. `Build_context_root::calculate_bounding_volume()` runs
near the end of the `Build_context` constructor, before
`build_polygon_fill()` / `build_expanded_polygon_fill()` / `build_edge_lines()`
/ `build_centroid_points()`, so the AABB is already known when the first
position is written: no extra pass, no reordering.

`Build_context_root` holds the encoding plus `1 / scale` and the center,
computed in the constructor right after the bounding volume, only when the
sink format's position attribute is `format_16_vec3_snorm`. One helper on
`Build_context` applies the pack (bias, scale, clamp) before handing the value
to the existing `write(..., GEO::vec3f)` -> `write_low3` path, whose
`format_16_vec3_snorm` case does the float-to-int16 step. Both position
writes - the vertex position and the facet centroid - go through it; facet
centroids are inside the AABB by construction. The expanded solid-wireframe
path needs no third site, because it only redirects the position writer at a
different `Vertex_buffer_writer` and calls the same vertex-position builder.

### 4.2 `build_buffer_mesh_from_triangle_soup()` (unpublished geometry)

This path converts source attributes straight into the sink vertex format via
`erhe::dataformat::convert`. The bounding-volume pass runs **before** the
per-stream conversion loop, so the affine is known when the position attribute
is converted.

The encode is gated on the sink format exactly as 4.1 gates it, because this
function is also called with a non-`Mesh_memory` sink: `Primitive_raytrace`'s
`Triangle_soup` constructor builds a local float3-only `Vertex_format` and
calls it. That path stays unquantized - the CPU BVH backends require
`format_32_vec3_float`.

The two paths fail differently on an unencoded position, which is worth
knowing: `convert()`'s `format_16_vec3_snorm` sink case **asserts**
(`ERHE_VERIFY(fx >= -1.001f)` per component), where the `write_low3` path
clamps silently inside the float-to-snorm16 conversion.

### 4.3 Left alone

- The three `custom_attribute_corner_position_*` attributes (stream 3 of the
  wireframe formats) stay `format_32_vec3_float`. They only project corner
  caps into screen space; leaving them exact places each cap within one
  quantization step of the decoded corner, sub-pixel at any reasonable
  distance.
- `vertex_format_edge_line` stays `format_32_vec4_float`. It is a separate
  SSBO-consumed stream whose `position.w` already carries payload, and the
  wide-line compute backend reads positions from it and decodes nothing. This
  also keeps `enqueue_gpu_edge_line_positions()` (7) working unchanged - do
  not let that format drift without revisiting that write-back.

## 5. Shader decode

`res/shaders/erhe_vertex_position.glsl` is included by every vertex shader
that reads `a_position` from a `Mesh_memory` format:

```glsl
#define ERHE_VERTEX_POSITION_ENCODING_PASSTHROUGH    0
#define ERHE_VERTEX_POSITION_ENCODING_SNORM16X3_AABB 1

#ifndef ERHE_VERTEX_POSITION_ENCODING
#   error "ERHE_VERTEX_POSITION_ENCODING is not defined - this shader was compiled without a vertex format"
#endif

vec3 erhe_decode_vertex_position(vec3 scale, vec3 offset)
{
#if ERHE_VERTEX_POSITION_ENCODING == ERHE_VERTEX_POSITION_ENCODING_SNORM16X3_AABB
    return a_position * scale + offset;
#else
    return a_position;
#endif
}
```

One function, not two, taking the two vectors as parameters. **It must not
reference `primitive.primitives[]`**: `content_edge_lines.vert` is built with
only its own view block and never goes through `Program_interface::make_prototype()`,
so `primitive` is undeclared there and an unguarded reference is a compile
error the moment the quantized branch is taken. Each shader passes the
vectors from wherever its own per-draw data lives.

**The arguments are evaluated in both variants** - they sit outside the `#if` -
so a shader converted this way requires `position_scale` / `position_offset`
to exist in whatever block it reads them from. In the passthrough variant the
values are dead and the compiler drops the loads, so the uniform signature
costs nothing at runtime.

`a_position` is declared `vec3` in both cases (`format_32_vec3_float` and
`format_16_vec3_snorm` both map to `Glsl_type::float_vec3`), so there are no
swizzle differences and no `#if` outside this helper.

Call sites split into two classes over *where the scale and offset come from*:

- **Class A** reads `primitive.primitives[ERHE_DRAW_ID]` and needs no new
  uniforms: `standard.vert` (the lit forward pass, depth-only, ID render,
  brush preview, shadow distance, shadow cube, points, solid wireframe and the
  edge-lines-from-id variants) and `tool.vert`.
- **Class B** has no `primitive.primitives[]` in scope and carries the two
  vectors in its own per-draw block: `content_edge_lines.vert` (the wide-line
  renderer's view UBO) and the lightmap baker's G-buffer and origins vertex
  programs (`m_draw_block`). A define alone compiles and renders garbage here.
  Every lightmap draw record is pre-filled with the identity affine right
  after its `memset`, so a record the draw filter skips is wrong-but-finite
  instead of collapsing its mesh to a point.

**The lightmap baker's two programs inline the helper rather than
`#include`ing it.** Their shader source is passed as an inline string, and
`Shader_stages_create_info::final_source()` appends inline source verbatim
while running the include loader only over `shader.paths`. On GL that string
goes straight to `glCompileShader`, so an `#include` is never expanded and the
compile fails; `extra_include_paths` does not rescue it, because on Vulkan the
glslang includer *would* resolve it and the program would then build on one
backend and fail on the other.

**The define is emitted from exactly one place: `attributes_source()`.** Its
value is a pure function of the vertex format, and that is where the format is
already in hand, next to the existing `ERHE_ATTRIBUTE_<name>` defines. Two
conditions come with it:

- **The axis is excluded from `Shader_key::get_defines()`.** With the axis in
  the key *and* emitted by `attributes_source()`, every key-compiled vertex
  shader would get the macro twice; identical redefinitions are harmless, but
  a hand-built key that forgot the axis would emit a *conflicting* one. The
  axis stays in the key for variant hashing and identity, and out of
  `get_defines()`. This does not make 2.1 optional: the key hash still
  differs, so a hand-built key without the axis still misses the variant
  cache.
- **`attributes_source()` emits nothing when `vertex_format == nullptr`**, and
  the field defaults to null. Every consumer passes one today, but that is a
  precondition, not a guarantee. The `#ifndef` / `#error` above is the
  backstop that turns a future format-less shader into a build failure instead
  of silent garbage; keep both.

**A missing define is otherwise silent**: `#if ERHE_VERTEX_POSITION_ENCODING == ...`
with the macro undefined evaluates to `0 == 1` in the GLSL preprocessor, that
is, passthrough, with no diagnostic.

Not affected: `debug_line.vert`, `line_simple.vert`, `line_after_compute.vert`,
`text.vert`, `sky*.vert`, `grid.vert`, `shadow_debug.vert`,
`post_processing.vert` (own formats, SSBO structs, fullscreen passes).

## 6. Format plumbing

`Mesh_memory` keys each `Buffer_pool` on the **address** of a `Vertex_stream`
instance, so a second, quantized *set* of format members would allocate a
parallel set of pools and split content across pools even when only one
encoding is in use. Instead the position format is a construction-time choice:
`Mesh_memory_config::quantize_vertex_positions` (code-generated from
`definitions/mesh_memory_config.py`; after changing a codegen definition,
**build twice** or the binary is stale) plus a device query select
`Mesh_memory::optimized_position_format`, which the optimized-format
derivation substitutes for the position attribute. One pool set, one encoding
per session.

`choose_optimized_position_format()` returns `format_32_vec3_float` unless the
flag is set and `Device_info::use_16_vec3_snorm_vertex_buffer` is true. Vulkan
guarantees `VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT` for the 1/2/4-component
16-bit snorm formats but **not** for `VK_FORMAT_R16G16B16_SNORM`, so the
Vulkan backend queries it per physical device; GL and Metal have it
unconditionally. A declined request logs a warning at startup naming what the
device reported - to `erhe.scene_renderer.startup`, because
`erhe.scene_renderer.mesh_memory` has no entry in `config/editor/logging.json`
and would be filtered out of `logs/log.txt` exactly when it matters.

If the 1.1 packing minimums are needed they must be applied when the formats
are constructed, not later: the stride is *captured* at pool creation and all
`byte_offset / stride` arithmetic depends on it, so a stride changed after
allocation would silently mismatch the pool. `Buffer_pool::is_compatible()` is
pure pointer identity and would not catch it.

The warmup loops that name `vertex_format_not_skinned` /
`vertex_format_skinned` keep working unchanged, because those members carry
whichever position format is active.

## 7. Consumers that read stream-0 positions directly

These bypass the vertex-input path.

**Ray tracing acceleration structure build.** Every BLAS source is pinned to
the *original* variant, which is float3 by construction, so a quantized
optimized format never reaches an acceleration structure build in the shipped
configuration. `get_blas_position_input()` (in `mesh_memory.hpp`, the single
source of both the format and the transform, so the two BLAS builders - the
scene TLAS and the lightmap baker's own cache - cannot disagree with each
other or with the affine the instance records carry) still answers per
`Buffer_mesh` and returns early on a passthrough format. For a quantized one:

- The dequantization affine is applied as a **build** transform
  (`VkAccelerationStructureGeometryTrianglesDataKHR::transformData`, Metal's
  transformation matrix buffer), so the structure stores object-space
  positions and the instance transform is untouched. Folding the affine into
  the *instance* transform instead would need no new GPU plumbing and is
  tempting, but it is wrong: normals and tangents are fetched from stream 1,
  which is never quantized, and `res/shaders/erhe_ray_hit.glsl` transforms
  them with the inverse transpose of `world_from_object`. Making that matrix
  `world_from_node * dequant` would apply the per-axis half extent to
  attributes that never lived in encoded space, warping every shading normal
  on a non-cubic AABB. Do not revisit this.
- Vulkan's mandatory acceleration-structure format table does **not** include
  the 3-component 16-bit snorm format (the desktop AMD 890M advertises it as a
  vertex buffer format but not as build input) while it **does** include the
  4-component one. Since stream 0 is padded to a whole number of int16 lanes
  and the position sits at offset 0, the very same buffer can be read as
  snorm16x4 in place - a build takes xyz from the position format and ignores
  further components - so no second copy is needed.
  `get_blas_position_input()` prefers the 3-component format where the device
  advertises it and falls back to the in-place 4-component read where it does
  not, subject to `Device_info::min_acceleration_structure_vertex_stride`
  (Vulkan 1, Metal 12).

**Manual position fetch in compute shaders.** `Instance_record_data` and
`Lm_instance_record` carry a **byte-granular** `position_stride_bytes` plus
`position_encoding` and the scale / offset vectors, and the consumers -
`fetch_vec3` in `res/shaders/erhe_ray_hit.glsl` (shared by DDGI and the
ray-trace renderer) and the two copies embedded in the lightmap baker - branch
on the encoding. A record-carried selector rather than a compile-time define
is deliberate: none of these shaders is compiled through `Shader_key`, so none
receives `ERHE_VERTEX_POSITION_ENCODING`, and a record selector also survives
a future per-primitive encoding. Growing the record touches four coupled
places: the `static_assert` on its size, the constructor's layout `VERIFY`s
against the generated `Shader_resource` offsets, and the GLSL struct in all
three copies. Keep the 16-byte padding rule the `reserved` fields exist for.
This path only runs when `use_ray_tracing_position_fetch` is false; the
position-fetch path reads decoded positions out of the structure.

Pool ranges are aligned to `lcm(stride, 4)`: element-size alignment alone is
what makes `byte_offset / element_size` exact, but a 6- or 14-byte stride
would then put half the ranges on a `2 mod 4` address, and the instance
records hand that address to a buffer reference declared
`buffer_reference_align = 4`.

**Mesh component editing write-back.** `enqueue_gpu_position()` writes
individual vertex positions back into the GPU stream. It addresses the base
variant, which is float3, so no encode is involved in the shipped
configuration; the snorm16 case clamps, warns once per drag, and lets the
commit rebuild. `Move_mesh_vertices_operation` constructs a fresh `Primitive`
from the geometry, which recomputes the AABB, so a committed result is exact
and only a live drag preview could pin a vertex to the box face.

**CPU ray trace / picking** (`bvh_geometry.cpp`, `tinybvh_geometry.cpp`,
`embree_geometry.cpp`) read a separate float3 `Cpu_buffer` build and are
unaffected. They hard-require `format_32_vec3_float`.

## Risks to watch when changing this

- **Shadow / lit disagreement.** Caster and lit pass must decode from the same
  record; 2.1 is what keeps their keys in agreement. Never consider a change
  to the shadow key path done without a shadow-pass check.
- **Z-fighting on coplanar surfaces.** Quantization moves surfaces by up to
  `half_extent / 65534`, and two coplanar meshes with different AABBs pick up
  different errors. Quantization does not create the ambiguity, it re-rolls
  it: a modelled-coincident pond bottom and water plane flip over a large area
  with any sub-millimetre shift.
- **Edge lines over quantized fill.** `vertex_format_edge_line` stays exact
  float, so edge lines are drawn on the unquantized surface while an optimized
  fill is quantized. `erhe_line_surface_bias.glsl` biases lines toward the
  camera; if that bias ever stops covering the mismatch, quantize the
  edge-line stream with the same AABB rather than widening the bias.
- **Shadow acne.** `SHADOW_BIAS` was tuned against exact positions.
- **Large single meshes.** A 200 m mesh in one primitive gets 3 mm steps. The
  answer is a per-primitive heuristic (see the plan) or splitting the import,
  not a wider format.
- **Skinned meshes.** The AABB is the rest-pose AABB over the source
  `GEO::Mesh` - exactly the space the quantized attribute lives in, since skin
  matrices are applied after decode. `joint_bounding_boxes` are unaffected, so
  no special case is needed.

## Verifying a change

Compare rendering with quantization on and off over a fixed sequence (default
scene, skinned rigs, Sponza, the precision-sensitive debug views), capturing
each view only once two consecutive frames are identical over the viewport
region.

- **Control the harness first**: two runs with the same setting must be
  pixel-identical. Settle on the VIEWPORT CROP, not the whole image - the
  ImGui status bar prints a per-frame millisecond count, so a full-image
  comparison never repeats.
- **DDGI must be off.** It accumulates temporally and is not deterministic
  between runs: two *identical* float3 runs differ in about 18 % of pixels
  with DDGI on and in 0 pixels with it off. Any comparison of a rendering
  change measures nothing with DDGI on.
- Expected magnitudes: the default scene differs in about 0.013 % of pixels at
  a maximum channel delta of 6; the `vertex_valency` and `polygon_edge_count`
  debug views are bit-identical (they are computed per vertex and break loudly
  on a mis-decoded position); Sponza differs in about 17 % of pixels at a mean
  delta of 0.095, which is antialiasing edges moving by a fraction of a pixel
  across a scene that is almost entirely silhouette. Ray tracing adds no error
  of its own: the figure with ray tracing on matches the raster-only figure.
- Measure stream-0 pool `used` bytes with the `get_memory_usage` MCP tool
  before and after.

## Future work

- [Mesh memory and primitive shapes](../plans/mesh_memory.md) - one definition of
  the affine, Metal attribute alignment, per-primitive encoding, the padded
  `snorm16x4_aabb` format, the BLAS float3 copy.
