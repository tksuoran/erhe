# Vertex position quantization

Status: all phases (1-7) implemented and verified; quantization survives ray
tracing, and is confirmed working on Quest 3 (see Implementation notes at the
end -- the phase 5 fallback decision is SUPERSEDED)
Date: 2026-08-26
Revision: 8 (verified over six independent review rounds; see Provenance)

**2026-08-29 SCOPE CHANGE (meshoptimizer requirements 9-11): quantization
now applies ONLY to the meshoptimizer OPTIMIZED variant's vertex formats.**
The content (base) formats are back to float3 unconditionally - the base
variant is the always-renderable build that in-place GPU edits write
through, and a float position can express any value, so the out-of-AABB
edit clamp (§7's deviation, and the "Still not done" drag below) is gone by
construction. `Mesh_memory::position_format` became
`optimized_position_format`; the acceleration-structure clause of the
format choice was dropped (every BLAS source is pinned to the original
variant, float3 by construction); the live-drag snorm16 write-back branch
was removed. The encode sites, shader axis, per-draw scale/offset records,
RT record plumbing and packing rules this document describes all still
exist and work exactly as written - they are simply exercised by the
optimized formats only. See doc/meshoptimizer_integration.md ("Position
quantization (optimized variant only)") for the current wiring; sections
below describing quantization of the *content* formats document the
original design.

## Goal

Shrink the position attribute of the content vertex streams from
`format_32_vec3_float` (12 B) to `format_16_vec3_snorm` (6 B) by storing each
vertex position normalized into its primitive's object-space AABB, and
dequantizing in the vertex shader from that AABB. Most shaders read it from the
primitive buffer; the few with their own per-draw UBO carry it there instead
(§5).

The encoding is a new *shader key axis*, so passthrough (today's float3) and
quantized meshes can coexist and each draw compiles / selects the matching
shader variant. The axis is named for the concrete storage format -
`snorm16x3_aabb` - so a future `snorm16x4_aabb` (padded, or with a payload in
`w`) is a new enumerator rather than a reinterpretation of this one.

### Expected win

Stride is a property of the **`Vertex_stream`**, not of an attribute:
`Vertex_stream::stride` (`vertex_format.hpp:114`) is the per-vertex step for the
whole binding, and each `Vertex_attribute` carries only an `offset` into it.
Attributes are packed in declaration order, each aligned up to its own component
size, and the stream stride is padded once at the end to the stream's largest
component alignment (`vertex_format.cpp:44-58` for the initializer-list
constructor, `:73-85` for `emplace_back()` and `:87-92` for `finalize_stride()`).

So a quantized position packs directly against whatever follows it in the same
stream, and any stride padding is paid **once per vertex per stream**, not per
attribute:

| stream | today | quantized | saving |
| --- | --- | --- | --- |
| stream 0, not skinned (`position`) | 12 B | 6 B | 50 % of stream 0 |
| stream 0, skinned (`position` + `joint_indices` + `joint_weights`) | 20 B | 14 B | 30 % of stream 0 |

Skinned layout: position at offset 0 size 6, `joint_indices`
(`format_8_vec4_uint`, alignment 1) at offset 6, `joint_weights`
(`format_8_vec4_unorm`, alignment 1) at offset 10 → 14, max alignment 2 → stride
stays 14. Note that quantization also *lowers* the stream's max alignment from 4
to 2, which is what lets 14 stand un-padded.

**What this win is and is not.** It is a reduction in vertex-pool footprint and
in the bytes fetched for stream 0 on every pass that reads positions - which is
every pass. It is **not** a "position-only pass reads less" win: there is no
position-only vertex-input variant. `bucket_vertex_ranges()`
(`mesh_memory.cpp:759-767`) returns *all* of the mesh's vertex ranges
(`expanded_vertex_buffer_ranges` for `Primitive_mode::solid_wireframe`,
`vertex_buffer_ranges` otherwise), `draw_list_scene.cpp:141-143` records every
one of them regardless of `Draw_purpose`, and `draw_list_scene.cpp:1238-1241`
binds them all - so the shadow and depth-only passes bind streams 1..3 as well.
ID render is not position-only either: `standard.vert:239-246` reads
`a_custom_0`, which lives in stream 2 (`mesh_memory.cpp:86`). Quote the win as
"stream 0 shrinks by 50 % / 30 %", not as a per-pass bandwidth figure.

## Current state (verified in-tree)

* `erhe::scene_renderer::Mesh_memory` (`src/erhe/scene_renderer/erhe_scene_renderer/mesh_memory.cpp:24`)
  owns the canonical `Vertex_format`s: `vertex_format_skinned`,
  `vertex_format_not_skinned`, the two `_wireframe` variants and the two
  edge-line formats. **Four** of them - `vertex_format_skinned` (`:34`),
  `vertex_format_not_skinned` (`:66`), and the two `_wireframe` variants (`:96`,
  `:135`) - declare
  `{ Format::format_32_vec3_float, Vertex_attribute_usage::position, 0 }` in
  stream 0. Those four are the quantization target. `vertex_format_edge_line`
  carries a position but as `format_32_vec4_float` (`w` is payload);
  `vertex_format_edge_line_joints` (`:182-190`) and `vertex_format_empty` (`:28`)
  carry no position at all.
* `Shader_key` (`src/erhe/scene_renderer/erhe_scene_renderer/shader_key.hpp`)
  already has an int-axis mechanism (`ERHE_SHADER_INT`) that emits
  `#define ERHE_<NAME> <value>` (`shader_key.cpp:43-47`) and folds the value into
  the variant hash. `Shader_key::derive(material, vertex_format, mesh_has_skin)`
  inspects the **actual** vertex format of the primitive being drawn: both
  `draw_list_scene.cpp:118` and `mesh_memory.cpp:895` pass
  `mesh_memory.get_vertex_input(buffer_mesh->vertex_input_key).vertex_format`.
  **But see §2.1 - two call sites deliberately throw the derived key away.**
* `Shader_stages_create_info::attributes_source()`
  (`src/erhe/graphics/erhe_graphics/shader_stages_create_info.cpp:53`) emits the
  `in` declaration from the attribute format via `to_glsl_attribute_type()`.
  `format_16_vec3_snorm` maps to `Glsl_type::float_vec3` (`enums.cpp:451`), so
  `a_position` stays a **`vec3`**, now in [-1, 1] instead of object space, and
  the shader-side difference is purely the decode expression.
* **Backend vertex-format gaps - two of them, and one is fatal.** GL is
  complete: all three switches in `gl_vertex_input_state.cpp` (`:114`, `:195`,
  `:276`) carry `format_16_vec3_snorm`. Vulkan and Metal do not:
  * **Vulkan.** The vertex-input path uses `to_vk_vertex_format()`
    (`vulkan_helpers.cpp:1447-1497`), called from `vulkan_render_pipeline.cpp:75`
    and `vulkan_render_command_encoder.cpp:261`. It has `format_16_vec2_snorm`
    (`:1478`) and `format_16_vec4_snorm` (`:1479`) but **no
    `format_16_vec3_snorm`**, and its default is
    `ERHE_FATAL("Unsupported vertex format")` (`:1495`). Enabling quantization
    without adding this case aborts at pipeline creation on the primary backend.
    Note that `vulkan_helpers.cpp:801` *does* map the format, but that is inside
    `to_vulkan()`, a general format mapping the vertex-input path never calls -
    do not mistake it for vertex-input support.
  * **Metal.** `to_mtl_vertex_format()` (`metal_helpers.cpp:101`) covers
    scalar / vec2 / vec4 snorm (`:131-133`) but skips vec3, the same hole as
    `format_16_vec3_unorm`. `MTL::VertexFormatShort3Normalized` exists; add the
    case.
* **Vulkan vertex-buffer support for a 3-component 16-bit format is not
  guaranteed.** Adding the switch case may not be sufficient: Vulkan's
  mandatory-format table guarantees `VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT` for
  the 1/2/4-component 16-bit snorm formats, not for `VK_FORMAT_R16G16B16_SNORM`.
  Query `vkGetPhysicalDeviceFormatProperties(...).bufferFeatures &
  VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT` at device init and record it in
  `Device_info`; a device lacking it falls back to passthrough (the config flag
  is per-session, §6.1) or to a padded `snorm16x4_aabb`. Check on the desktop
  AMD 890M iGPU and on Quest in phase 3 - cheap to check there, expensive to
  discover in phase 7.
* `Primitive_struct` (`primitive_buffer.hpp:32`) is the per-draw GPU record,
  declared with `Shader_resource::add_*` at `primitive_buffer.cpp:42`. It is
  written at **four** places - see §3.
* Positions reach the buffer through
  `Vertex_buffer_writer::write(attribute, GEO::vec3f)` →
  `write_low3(span, attribute.format, ptr)` (overload at `buffer_writer.cpp:617`,
  dispatch at `:619`), which switches on the destination format and converts,
  but knows nothing about an AABB. It **already has a `format_16_vec3_snorm` case**
  (`buffer_writer.cpp:122`, via `float_to_snorm16`, which maps -1 → -32767 and
  so round-trips against the GPU snorm decode), so the encoder keeps using the
  existing `write(…, GEO::vec3f)` path and needs no `buffer_writer` change. What
  it must add is the **packing**: the affine that maps object space into the
  snorm range - subtract the AABB centre (bias), divide by the half extent
  (scale), clamp - applied to the value before it is handed to `write()`.
  `write_low3` supplies only the float → int16 quantization step.
* There are **two** Buffer_mesh build paths - see §4.
* The CPU ray-trace geometry is built from a **separate** float3-only
  `Vertex_format` local to `primitive.cpp:156-161`, into a `Cpu_buffer`. It is
  unaffected and must stay unquantized: `bvh_geometry.cpp:245` and
  `tinybvh_geometry.cpp:113` hard-require `format_32_vec3_float`.

## Design

### 1. The encoding

Per primitive, with object-space AABB `[min, max]`:

```
center      = 0.5 * (min + max)
half_extent = 0.5 * (max - min)                        // per axis, >= 0
scale       = max(half_extent, epsilon)                // stored
encoded     = clamp((p - center) / scale, -1, 1)       // snorm16 x, y, z
decoded     = encoded * scale + center
```

Encoder and decoder must use the *same* `scale`, so a degenerate axis
(`half_extent == 0`, e.g. a flat quad) encodes to exactly 0 and decodes back to
`center` exactly - the `epsilon` never contributes because it is multiplied by
zero. Use `epsilon = 1e-6f` or the smallest normal that keeps the division
finite.

The quantization step per axis is `half_extent / 32767` - worst-case error is
half a step, since `float_to_snorm16` rounds to nearest
(`dataformat.cpp:39-49`). That is a 30 µm step for a 2 m mesh, 3 mm for a 200 m
mesh in a single primitive. That is why the switch is per format
(and later per primitive) rather than unconditional - see Rollout and Risks.

`format_16_vec3_snorm` (6 B) is the storage format: it is the smallest that
holds the encoding, it is already wired through `write_low3` and the GL
vertex-input tables (Vulkan and Metal need one switch case each, above), and it
keeps `a_position` a `vec3` in the shader. A padded
`format_16_vec4_snorm` (8 B) variant is **future work** - it would only earn its
two extra bytes if something wants a per-vertex payload in `w`, if the alignment
notes below force padding, or if the BLAS format problem in §7 forces it.

#### 1.1 Alignment, to confirm in phase 6

Two *separate* alignment questions, both consequences of the 2-byte format:

**Stream stride.** The stream-0 strides become 6 (not skinned) and 14 (skinned);
neither is a multiple of 4. GL and Vulkan core impose no stride alignment (only
`maxVertexInputBindingStride`). Metal is documented to require
`MTLVertexBufferLayoutDescriptor.stride` to be a multiple of 4. Verify against
the Metal backend in phase 6, the first phase in which a 6- or 14-byte stream exists (the formats are hard-coded `format_32_vec3_float` at `mesh_memory.cpp:34/66/96/135` until then). The *hook* is built in phase 3 regardless, defaulting to alignment 1 so it is inert; phase 6 only sets the value. The fix is a device-supplied
**minimum stream stride alignment**, applied where the stride is finalized -
both in the initializer-list constructor (`vertex_format.cpp:56`) and in
`finalize_stride()` (`:87-92`), whose declaration comment already reads "pad
stride for Vulkan alignment" (`vertex_format.hpp:92`), so the hook belongs
there.

**Attribute offsets.** Independently, the skinned stream-0 attribute offsets
move from 12 / 16 to **6 / 10** (`mesh_memory.cpp:34-36`; the two joint
attributes have component alignment 1, so `Vertex_stream` places them
immediately after the 6-byte position). Those are no longer 4-byte aligned. A
stride-only padding hook does **not** fix this; if a backend constrains
attribute offsets (Metal, or MoltenVK's portability-subset limits), the fix is a
device-supplied minimum *attribute* alignment in the same packing loop, or
reordering the stream so the 1-byte attributes precede the position.

| stream | GL / Vulkan (no pad) | Metal (pad to 4) | today |
| --- | --- | --- | --- |
| not skinned | 6 | 8 | 12 |
| skinned | 14 | 16 | 20 |

Under a 4-byte stride rule `snorm16x3` (padded) and `snorm16x4` land on exactly
the same stride for both of these streams - so vec3 is never worse than vec4,
and is strictly better wherever the rule does not apply. Padding also amortizes:
a stream that packs more attributes after the position wastes at most 3 bytes
per vertex regardless of how many attributes it holds.

### 2. Shader key axis

Add to `ERHE_SHADER_INT` in `shader_key.hpp`:

```
X(VERTEX_POSITION_ENCODING)
```

with a matching enum. Declare it in **`erhe_graphics`** (next to the vertex
format machinery), not in `shader_key.hpp` - §5 puts the `#define` emitter in
`attributes_source()`, which lives in `erhe_graphics` and cannot see
`erhe_scene_renderer`. The encoding is a property of the vertex format, so it
belongs there anyway:

```cpp
enum class Vertex_position_encoding : uint32_t
{
    passthrough    = 0, // a_position is object-space float3
    snorm16x3_aabb = 1  // a_position is snorm16x3 normalized into the primitive AABB
    // snorm16x4_aabb = 2 // future work: padded / w carries a payload
};
```

`Shader_key::derive()` sets it from the vertex format's position attribute:

```cpp
const erhe::dataformat::Attribute_stream position =
    (vertex_format != nullptr)
        ? vertex_format->find_attribute(usage::position, 0)
        : erhe::dataformat::Attribute_stream{};
// Assign unconditionally, do not only set the non-zero case: derive() copies
// int_values from *this (shader_key.cpp:116-118), so a stale non-zero encoding
// on an environment key would otherwise be carried through.
key.set(
    Shader_int::VERTEX_POSITION_ENCODING,
    static_cast<uint32_t>(
        ((position.attribute != nullptr) &&
         (position.attribute->format == erhe::dataformat::Format::format_16_vec3_snorm))
            ? Vertex_position_encoding::snorm16x3_aabb
            : Vertex_position_encoding::passthrough
    )
);
```

`derive(material, nullptr, …)` - the material-identity hash at
`draw_list_scene.cpp:157` - then pins the axis to 0, which is what that hash
wants.

#### 2.1 The call sites that build a key without `derive()`

`derive()` is **not** sufficient on its own. Two places build a `Shader_key`
from scratch and fill it partly, and both must be taught to carry the new axis.
A third (`Primitive_buffer::update(nodes …)`, `primitive_buffer.cpp:506`) writes
primitive records for node proxies but builds no key at all; it is covered as
§3.1 item 3.

* **`Draw_purpose::shadow`**, `draw_list_scene.cpp:121-133` (the quoted lines
  are `:126-128`):

  ```cpp
  const Shader_key full = Shader_key{}.derive(nullptr, &vertex_input_entry.vertex_format, skinned);
  result.key = Shader_key{};
  result.key.set(Shader_bool::USE_SKINNING, full.get(Shader_bool::USE_SKINNING));
  ```

  A fresh `Shader_key{}` with only `USE_SKINNING` copied back (the documented R4
  intent, `draw_list_key.hpp:64`). Left as-is, **every shadow draw of a
  quantized mesh would use a passthrough shader and rasterize the caster from
  raw [-1,1] positions.** Copy `VERTEX_POSITION_ENCODING` alongside
  `USE_SKINNING`. The rule is: the shadow key keeps exactly those axes that
  describe how to *get to a position*, and the encoding is now one of them.
* **The shadow-renderer prewarm**, `shadow_renderer.cpp:783-789`:

  ```cpp
  Shader_key key{};
  key.bool_mask = mask;
  static_cast<void>(m_shader_variant_cache.get(key, &m_mesh_memory.vertex_format_not_skinned));
  key.set(Shader_bool::USE_SKINNING, true);
  static_cast<void>(m_shader_variant_cache.get(key, &m_mesh_memory.vertex_format_skinned));
  ```

  No `derive()` at all - the key is hand-built while the *vertex format* passed
  alongside it is the quantized one. `Shader_variant_cache::get()` keys its map
  on the `Shader_key` alone (`shader_variant_cache.cpp:38-40`; the vertex format
  is only used at compile time, `:57-61`), so this would silently prewarm six
  encoding-0 shadow variants and every real shadow draw (encoding 1, after the
  fix above) would miss the cache and compile on the render thread. Set the axis
  from the format here too - or better, derive it from the same format that is
  passed to `get()`, so the two can never disagree.

Audit for any other `Shader_key{}` construction that is then partly filled
before phase 1 lands; the "correct by construction" property only holds for call
sites that use `derive()`'s result whole. The forward-renderer prewarm
(`forward_renderer.cpp:690-706`) is fine - it goes through
`environment_key.derive(material.get(), &vertex_format, has_skin)` at `:699`.

**Variant count.** The axis doubles the *reachable* variant space, but only one
value is reachable per vertex format, so live variant count grows only by the
number of distinct formats actually in play.

### 3. Primitive buffer: AABB record

Extend `Primitive_struct` with two `vec4`s:

```cpp
std::size_t position_scale;   // vec4 - xyz = scale  (half extent, >= epsilon), w unused
std::size_t position_offset;  // vec4 - xyz = center (AABB center),             w unused
```

registered in `Primitive_interface` (`primitive_buffer.cpp:42`):

```cpp
.position_scale  = primitive_struct.add_vec4("position_scale" )->get_offset_in_parent(),
.position_offset = primitive_struct.add_vec4("position_offset")->get_offset_in_parent(),
```

The record is currently mat4+mat4+vec4+vec4+uint+float+float+uint+uint = 180 B,
rounded up to the struct's base alignment by `Shader_resource::get_size_bytes()`
(`shader_resource.cpp:663-672`) → 192 B. Two `vec4`s appended after the five
4-byte scalars have 16-byte base alignment, so the member offset pads 180 → 192
first and the record becomes 192 + 32 = **224 B** (+17 %). `Primitive_interface(…, max_primitive_count)` sizes the block from the
struct size, so growing the struct shrinks primitives-per-ring-buffer-block -
but **only on the UBO path**: the `max_uniform_block_size` division at
`primitive_buffer.cpp:57-60` is in the `else` branch of
`if (use_shader_storage_buffers)`, and the SSBO path uses
`Shader_resource::unsized_array` and leaves `max_primitive_count` alone
(`:54-56`). Verify a loaded scene's frame still fits on a UBO-only device and log
loudly if it does not.

#### 3.1 All four write sites

The decode lives behind `#if ERHE_VERTEX_POSITION_ENCODING == …`, so a
**passthrough variant never reads these fields** and their contents are
irrelevant for passthrough draws. What matters is that every path that can
produce a *quantized* draw writes them. Those paths are:

1. `Primitive_buffer::update(std::span<const std::shared_ptr<Mesh>>, Item_filter,
   Primitive_mode, settings)` - overload at `primitive_buffer.cpp:98`, write at
   `:251`. `buffer_mesh` is in scope (`:198`); write from
   `buffer_mesh->bounding_box` (`buffer_mesh.hpp:46`).
2. `Primitive_buffer::write_primitive(…)` - `primitive_buffer.cpp:370`. Same.
3. `Primitive_buffer::update(std::span<const std::shared_ptr<Node>> …)` -
   `primitive_buffer.cpp:535`. **This site has no `Buffer_mesh`, no primitive
   and no material at all** - it iterates `Node`s and correspondingly never
   writes `offsets.base_vertex` (contrast `:259` and `:378`, which do). It can
   only ever feed passthrough draws, so write the identity
   `scale = (1,1,1,0)`, `offset = (0,0,0,0)` for hygiene, or nothing.
4. **`Draw_list_scene::write_entry_record()` - `draw_list_scene.cpp:444`.**
   The *default* path: `use_draw_lists` is ON in HEAD settings. It does
   `std::memset(record, 0, m_primitive_record_stride)` and then writes only
   transforms, lightmap scale/offset, material/skin slots and `base_vertex`;
   `Primitive_buffer::update()`'s fast path (`primitive_buffer.cpp:454-483`)
   `memcpy`s that record verbatim and patches only `color` and `size`. Left
   as-is, every quantized mesh would get `scale = offset = (0,0,0,0)` and
   collapse to a point at the origin. The AABB is static per `Buffer_mesh`, so
   write it here once per entry, next to `base_vertex`; the companion helpers
   `write_transform_fields` (`:412`) and `write_slot_fields` (`:430`) are the
   pattern to follow. `mesh_primitive.primitive->get_renderable_mesh()` gives
   the `Buffer_mesh` in scope.

**Rejected alternative:** folding the dequantization affine into
`world_from_node` costs zero bytes, but it is wrong for skinned meshes
(`erhe_skin_matrices()` *builds* `world_from_node` from the joint palette, so
there is nothing to fold into), and it corrupts `v_node_position`
(`ERHE_USE_VERTEX_VARYING_NODE_POSITION`) plus the corner-cap paths that need
real object-space positions. Keep the AABB explicit, as specified.

### 4. Encoder: both build paths

There are two paths from source data to a `Buffer_mesh`, selected at
`primitive.cpp:840-846` on `m_geometry_published`. Both need an encoder.

#### 4.1 `Primitive_builder` (published geometry)

The favourable path. `Build_context_root::calculate_bounding_volume()` runs as
the second-to-last statement of the `Build_context` constructor
(`primitive_builder.cpp:538`, ctor at `:494-540`), i.e. **before**
`build_polygon_fill()` / `build_expanded_polygon_fill()` / `build_edge_lines()` /
`build_centroid_points()` (`:462`, `:467`, `:472`, `:477`). The AABB is already
known when the first position is written; no extra pass, no reordering.

1. `Build_context_root` gains `position_encoding`, `position_encode_inv_scale`
   (`1 / scale`) and `position_encode_center`, computed in the ctor right after
   `calculate_bounding_volume()`, only when the sink vertex format's position
   attribute is `format_16_vec3_snorm`.
2. Add one helper on `Build_context`:

   ```cpp
   void write_position(const Vertex_attribute_info& info, GEO::vec3f p);
   ```

   which forwards to `attribute_writers.position->write(info, p)` in the
   passthrough case, and to the same call with
   `clamp((p - center) * inv_scale, -1, 1)` in the quantized case. Both go
   through the existing `write(…, GEO::vec3f)` → `write_low3` path, whose
   `format_16_vec3_snorm` case already exists - so the only new code is the
   pack itself: the bias (`- center`), the scale (`* inv_scale`) and the clamp.
3. Route both position writes through it. `grep attribute_writers.position->write`
   returns exactly two:
   * `build_vertex_position()` (`primitive_builder.cpp:562`, write at `:569`)
   * `build_centroid_position()` (`:799`, write at `:810`) - facet centroids are
     inside the AABB by construction, so no extra range work

   The expanded solid-wireframe path needs no third change: `:1042` only
   *redirects* `attribute_writers.position` at a different `Vertex_buffer_writer`,
   and `:1136` calls `build_vertex_position()` again, so routing the first bullet
   covers it. `calculate_bounding_volume()` (`:357-363`) bounds all source mesh
   vertices, so centroids and expanded corners are inside the same AABB.

#### 4.2 `build_buffer_mesh_from_triangle_soup()` (unpublished geometry)

`Primitive_render_shape::make_buffer_mesh_build_locked()` (`primitive.cpp:840`)
falls through to `make_buffer_mesh_build_locked(build_info.buffer_info)`
(`:867`) whenever `m_geometry_published` is false, and that calls
`build_buffer_mesh_from_triangle_soup()` (`:882-1007`), which converts source
attributes **straight into `buffer_info.vertex_format`** - a Mesh_memory format
for content - via `erhe::dataformat::convert`, with no AABB pack. Note the
failure mode differs from §4.1's: `convert()`'s `format_16_vec3_snorm` sink case
(`dataformat.cpp:1945-1958`) **asserts** rather than clamps -
`ERHE_VERIFY(fx >= -1.001f)` / `<= 1.001f` per component - so an unencoded
position is a fatal abort here, where the `write_low3` path (`:122-127`) clamps
silently inside `float_to_snorm16` (`dataformat.cpp:39-49`). Loud is better;
just do not expect the two paths to fail the same way.

Worse for ordering: this path computes the bounding volume at `:994-1005`,
**after** the vertex data has already been enqueued at `:988-991`. Three changes:

Gate the encode on the sink format exactly as §4.1 does - this function is
**also** called with a non-Mesh_memory sink: `Primitive_raytrace`'s
`Triangle_soup` constructor builds a local float3-only `Vertex_format`
(`primitive.cpp:357-362`, ctor at `:353`) and calls it at `:380`. That path must stay
unquantized (the CPU BVH backends require `format_32_vec3_float`).

1. Move the position-scanning loop that builds
   `Point_vector_bounding_volume_source` and the
   `calculate_bounding_volume()` call to **before** the per-stream conversion
   loop. It is already a self-contained pass over the *source* positions, so
   this is a cheap, local reordering.
2. Apply the affine in the position attribute's conversion, the same way §4.1
   does.

#### 4.3 Left alone

* The three `custom_attribute_corner_position_*` attributes (stream 3 of the
  wireframe formats) stay `format_32_vec3_float`. They only project corner caps
  into screen space; leaving them exact places each cap within one quantization
  step of the decoded corner, sub-pixel at any reasonable distance. Revisit if
  caps visibly drift (see Risks).
* `vertex_format_edge_line` stays `format_32_vec4_float`. It is a separate
  SSBO-consumed stream whose `position.w` already carries payload. This also
  keeps `enqueue_gpu_edge_line_positions()` (§7) working unchanged.

### 5. Shader: decode

New `res/shaders/erhe_vertex_position.glsl`, included by every vertex shader
that reads `a_position` from a Mesh_memory format. **It must not reference
`primitive.primitives[]`**: `content_edge_lines.vert` is built by
`build_shader_stages()` with only its own `view_block` (`editor.cpp:2013-2030`)
and never goes through `Program_interface::make_prototype()`, so `primitive` is
undeclared there and an unguarded reference is a compile error the moment the
quantized branch is taken. Take the two vectors as parameters instead - each
shader already knows where its own per-draw data lives:

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

One function, not two. Call it as
`erhe_decode_vertex_position(primitive.primitives[ERHE_DRAW_ID].position_scale.xyz,
primitive.primitives[ERHE_DRAW_ID].position_offset.xyz)` where the primitive
block is in scope, and with the shader's own uniforms where it is not.

**The arguments are evaluated in both variants** - they sit outside the `#if` -
so any shader converted this way requires `position_scale` / `position_offset`
to already exist in whatever block it reads them from. That is what forces the
primitive-buffer fields to land *before* the `standard.vert` conversion in the
Rollout, not after. (In the passthrough variant the values are then dead and the
compiler drops the loads, so the uniform signature costs nothing at runtime.)

**The lightmap baker's two programs must inline the helper, not `#include` it.** The
lightmap baker's G-buffer and origins vertex programs pass their shader as an
**inline source string** (`lightmap_baker.cpp:1299`, `:1812` -
`{ Shader_type::vertex_shader, std::string_view{c_vertex_source} }`), and
`Shader_stages_create_info::final_source()` appends inline source verbatim
(`shader_stages_create_info.cpp:437-441`) while running the include loader only
over `shader.paths` (`:442-445`). On GL that string goes straight to
`glCompileShader` (`gl_shader_stages_prototype.cpp:343-370`), so an `#include`
is never expanded and the compile fails. Adding `extra_include_paths` does not
rescue it: on Vulkan the glslang includer (`glsl_to_spirv.cpp:253-267`) *would*
resolve an include inside an inline string, but on GL nothing does - so the
program would build on one backend and fail on the other. Copy the four lines
into `c_vertex_source` instead. `content_edge_lines.vert` is compiled from a
path (`editor.cpp:2024`, `:2049`) and can `#include` normally. (The `extra_include_paths` precedent
at `lightmap_baker.cpp:1600` is not applicable - that is a compute program gated
behind `use_ray_query` (`:1373`), i.e. Vulkan-only, whereas the G-buffer program
is built unconditionally in the `Lightmap_baker` constructor.)

`a_position` is declared `vec3` in both cases (`format_32_vec3_float` and
`format_16_vec3_snorm` both map to `Glsl_type::float_vec3`), so there are no
swizzle differences and no `#if` outside this helper.

**The call sites still split into two classes** - not over the helper's
signature, which is now uniform, but over *where the scale/offset come from*.
Class A shaders read them from the Forward_renderer primitive block, which
already carries them after §3; Class B shaders read per-draw data from their own
UBO and need the two vectors added to *that* block.

Class A - reads `primitive.primitives[ERHE_DRAW_ID]`, no new uniforms needed:

* `res/shaders/standard.vert:213` (`world_from_node * vec4(a_position, 1.0)`)
  and `:398` (`v_node_position = a_position`). This one shader covers the lit
  forward pass, depth-only, ID render, brush preview, shadow distance, shadow
  cube, points, solid wireframe and the edge-lines-from-id variants.
  **Reminder: the shadow variants only get the right define once §2.1's shadow
  key fix lands.**
* `res/editor/shaders/tool.vert:12` - reads
  `primitive.primitives[ERHE_DRAW_ID].world_from_node` at `:9`, so it has the
  scale/offset in scope. The `tool` program does not go through `Shader_key`,
  but it *is* compiled with `.vertex_format = &vertex_format_not_skinned`
  (`programs.cpp:95`), so the `attributes_source()` emitter below covers it and
  no explicit define is needed. **Note it is an orphan:** `Programs::tool`
  (`programs.hpp:46`) has no consumer - the only `shader_stages_override`
  assignment is `composition_pass.cpp:433`, and tool meshes render through the
  ordinary forward path (`app_rendering.cpp:1764`), i.e. through
  `standard.vert`. So the gizmo is already covered by phase 2 and converting
  `tool.vert` is hygiene, not a rendering fix.

  (`src/example` is not a separate case: it uses the shared `res/shaders` +
  `Shader_key` + `Forward_renderer` path and is covered by `standard.vert`. The
  editor is the only app that owns a `Mesh_memory` carrying content formats; if
  a second one is added - a replacement `rendering_test`, say - it must be
  written against whatever the encoding rules are at that time.)
* `res/editor/shaders/brush.vert:19`, `res/shaders/error.vert:18` and
  `textured.vert:11` are Class A too (all read `primitive.primitives[…]` at
  `:12`, `:7-8` and `:7`), but are commented out in `programs.cpp:96-98`.
  `res/shaders/edge_lines.vert` is likewise Class A (`:7`) but has **no compile
  site anywhere in the tree** - only `content_edge_lines.vert` is built
  (`editor.cpp:2024`, `:2049`). Convert them if and when they are enabled.

Class B - **no `primitive.primitives[]` in scope**; these need new fields in
their own per-draw block *and* the define. A define alone compiles and renders
garbage. Three programs are in this class:

* `res/shaders/content_edge_lines.vert:39` - compiled against
  `vertex_format_not_skinned` / `vertex_format_skinned` at
  `src/editor/editor.cpp:2022` and `:2047`, but reads `view.world_from_node`
  (`:35`), `view.base_joint_index` (`:28`) and `view.cameras[…]` (`:38`) from the
  wide-line renderer's own view UBO; its header comment (`:1-10`) states it "does
  not borrow Forward_renderer's descriptor set". Add the AABB to that view block
  (`content_wide_line_interface.cpp` / `content_wide_line_view_writer.hpp`).
* **The lightmap baker's two vertex programs.** `lightmap_baker.cpp:396` inside
  `c_vertex_source`:
  `vec3 world_position = (lightmap_draw.world_from_node * vec4(a_position, 1.0)).xyz;`
  - a private per-draw UBO (`m_draw_block`). Compiled with
  `.vertex_format = &mesh_memory.vertex_format_not_skinned` at
  `lightmap_baker.cpp:1297` (G-buffer pass) and again at `:1810` (origins pass).
  Add the AABB to `m_draw_block` and write it per draw. Left unconverted, every
  baked position / normal G-buffer texel is wrong. (The unrelated `a_position` at
  `:1200` is a fullscreen-quad shader with its own format - not affected.)

**A missing define is silent.** `#if ERHE_VERTEX_POSITION_ENCODING == …` with the
macro undefined evaluates to `0 == 1` in the GLSL preprocessor, i.e. passthrough,
with no diagnostic. So a shader compiled against a quantized format but never
told about it renders garbage rather than failing to build.

**Emit the define from exactly one place: `attributes_source()`.** Its value is
a pure function of the vertex format, and `attributes_source()`
(`shader_stages_create_info.cpp:53-73`) is where the format is already in hand,
next to the existing `ERHE_ATTRIBUTE_<name>` defines (`:68`). That retires the
"pass the define explicitly" instruction for every Class A shader and reduces
phase 4 to the two Class B per-draw blocks.

One consequence, already reflected in §2: this crosses a library boundary. `attributes_source()`
lives in `erhe_graphics` while `Vertex_position_encoding` lives in
`shader_key.hpp` (`erhe_scene_renderer`), which depends on `erhe_graphics` and
not the reverse. So the format → value mapping has to exist in `erhe_graphics` -
either move the enum down into `erhe_dataformat` / `erhe_graphics` and have
`shader_key.hpp` use it, or duplicate the one-line mapping and static_assert the
values agree. Moving it down is cleaner: the encoding is a property of the vertex
format, not of the shader key.

Two further conditions come with it, both load-bearing:

* **Exclude the axis from `Shader_key::get_defines()`.** If the axis is in
  `ERHE_SHADER_INT` (§2) *and* `attributes_source()` emits it, every
  key-compiled vertex shader gets the macro twice - `attributes_source()` is
  written into the preamble at `shader_stages_create_info.cpp:388`, the
  create_info defines at `:399-404`. Identical redefinitions are harmless, but a
  hand-built key that forgets the axis (precisely the
  `shadow_renderer.cpp:783-789` case §2.1 fixes) would emit a *conflicting*
  redefinition. Keep the axis in the key for variant hashing and identity, and
  skip it in `get_defines()`. Note this does **not** make §2.1 optional: the key
  hash still differs, so a hand-built key still misses the variant cache.
* **`attributes_source()` emits nothing when `vertex_format == nullptr`** - the
  whole body is inside `if (vertex_format != nullptr)`, and the field defaults
  to null (`shader_stages.hpp:76`). Every remaining consumer does pass one
  (`programs.cpp:95`, `editor.cpp:2022`/`:2047`, `lightmap_baker.cpp:1297`/
  `:1810`), so the mechanism covers all of them today - but it is a precondition,
  not a guarantee. The `#ifndef` / `#error` in the helper above is the backstop
  that turns a future format-less shader into a build failure instead of silent
  garbage; keep both.
* Not affected: `debug_line.vert`, `line_simple.vert`,
  `line_after_compute.vert`, `text.vert`, `sky*.vert`, `grid.vert`,
  `shadow_debug.vert`, `post_processing.vert` (own formats / SSBO structs /
  fullscreen passes).

### 6. Format plumbing

`Mesh_memory` keys each `Buffer_pool` on the **address** of a `Vertex_stream`
instance - the rule is stated at `mesh_memory.cpp:214-220` and `:246` and
applied at `:258-261`, with the long-form rationale at `buffer_pool.hpp:89`. A
second, quantized *set* of format members would therefore allocate a parallel
set of pools, doubling pool overhead and splitting content across pools even
when only one encoding is in use.

For the MVP, therefore:

**6.1 Make the position format a construction parameter of `Mesh_memory`, not a
new set of members.** Add to
`src/erhe/scene_renderer/definitions/mesh_memory_config.py` (bump `version` to
2, `added_in=2`):

```python
field("quantize_vertex_positions", Bool, added_in=2, default="false",
      short_desc="Quantize Vertex Positions",
      long_desc="Store vertex positions as snorm16x3 normalized into the primitive AABB (6 bytes instead of 12).",
      visible=True, developer=True)
```

The `Mesh_memory` constructor then builds **the four formats whose stream-0
position is `format_32_vec3_float`** (see §Current state) with
`position_format = config.quantize_vertex_positions ? format_16_vec3_snorm :
format_32_vec3_float`. `vertex_format_edge_line` keeps its
`format_32_vec4_float` position per §4.3, and the other two formats have no
position. One pool set, one encoding per session. After changing a codegen
definition, **build twice** or the binary is stale.

If the §1.1 padding is needed, it must be applied when these formats are
constructed, not later. The reason is that the stride is *captured* at pool
creation - `Buffer_pool` sizes its blocks from it (`mesh_memory.cpp:290-300`)
and all `byte_offset / stride` arithmetic depends on it - so a stride changed
after allocation would silently mismatch the pool. (`Buffer_pool::is_compatible()`
is pure pointer identity, `buffer_pool.cpp:94-119`; it would not catch this.
`Vertex_stream::is_buffer_compatible()` does compare strides but has **no
callers in `src/`** - do not rely on it.)

**6.2** The warmup loops that hard-code `vertex_format_not_skinned` /
`vertex_format_skinned` (`forward_renderer.cpp:696`, `shadow_renderer.cpp:786`)
keep working unchanged, because those members now carry whichever position
format is active.

**6.3 Follow-up work (not in the MVP):** a per-primitive heuristic - quantize only when
the AABB diagonal is under a threshold, or only above a vertex count - requires
the parallel format set and the extra pools. Defer until the global switch has
been measured. **One trap to solve first:** solid-wireframe draws derive their
key from `buffer_mesh->vertex_input_key` (`draw_list_scene.cpp:110`) but bind
`bucket_vertex_input_key(*buffer_mesh, primitive_mode)` =
`expanded_vertex_input_key` (`:136`, `mesh_memory.cpp:749-757`). Under the MVP's
one-global-encoding design both formats always agree, so this is harmless; under
a per-primitive scheme the key could describe a different encoding than the
bound format. Derive from the *bound* key before enabling 6.3.

### 7. Consumers that read stream-0 positions directly

These bypass the vertex-input path. Each must be handled, or quantization must
be refused while the feature is enabled:

* **Ray tracing BLAS build.**
  `src/erhe/graphics/erhe_graphics/vulkan/vulkan_acceleration_structure.cpp:73`
  hard-codes `vertexFormat = VK_FORMAT_R32G32B32_SFLOAT` over the stream-0
  range; `metal_acceleration_structure.cpp:50` likewise hard-codes
  `MTL::AttributeFormatFloat3`. Both are fed from
  `Acceleration_structure_triangles` (`acceleration_structure.hpp:30`), which
  today carries `vertex_buffer` / `vertex_byte_offset` / `vertex_byte_stride`
  and **no transform field** - so the create-info needs one, and **two**
  editor-side builders must fill it: `Scene_tlas::get_or_create_blas()`
  (`scene_tlas.cpp:109`, geometry at `:153`) and the lightmap baker's own
  parallel BLAS cache (`lightmap_baker.cpp:3568`, geometry at `:3608`, whose
  comment says it "Mirrors `Ray_trace_renderer::get_or_create_blas`").
  `VkAccelerationStructureGeometryTrianglesDataKHR::transformData` takes a 3x4
  transform per geometry - exactly the dequantization affine - so the offset and
  scale side is free on Vulkan. **Metal needs its own plumbing:**
  `metal_acceleration_structure.cpp:44-56` sets no transform at all, so the
  `MTLAccelerationStructureTriangleGeometryDescriptor` transformation-matrix
  buffer has to be wired up separately - the create-info field is shared, the
  backend mechanism is not. **This is the one place where the vec3 choice costs
  something:** the Vulkan-mandated AS vertex format set includes
  `VK_FORMAT_R16G16_SNORM` and `VK_FORMAT_R16G16B16A16_SNORM` but **not** the
  3-component `VK_FORMAT_R16G16B16_SNORM`, and a 4-component format cannot be
  read over a 6-byte stride. So the BLAS path must either
  (a) query `VK_FORMAT_R16G16B16_SNORM` for
  `VK_FORMAT_FEATURE_ACCELERATION_STRUCTURE_VERTEX_BUFFER_BIT_KHR` and use it
  where supported (widely, but optional), or
  (b) build the BLAS from a separate float3 position copy.
  (A third option - refusing quantization per primitive - is *not* available in
  the MVP: §6.1 makes the encoding one per-session construction parameter, and
  per-primitive encoding is deferred to §6.3 precisely because it needs the
  parallel format set.)
  Decide in phase 5; it does not block phases 1-4, and the flag stays off until phase 6. It is also the one argument
  that could pull the storage format to `snorm16x4_aabb` later - hence keeping
  that enumerator reserved.
* **Manual position fetch in compute shaders - a record redesign, not a format
  case.** `Instance_record_data` (`scene_tlas.hpp:54`) and `Lm_instance_record`
  (`lightmap_baker.hpp:588`) carry `position_address` +
  `position_stride_uints`, filled at `scene_tlas.cpp:243-245` and
  `lightmap_baker.cpp:3945-3946` as `element_size / 4` - where `element_size`
  *is* the stream stride (`primitive_builder.cpp:143-151` allocates per
  `sink_stream`). Both fill sites are guarded by
  `(element_size % 4) != 0 → continue` (`scene_tlas.cpp:231`,
  `lightmap_baker.cpp:3936`), so a 6- or 14-byte stride does **not** produce a
  truncated stride - it makes the guard fire and **the whole instance is
  silently dropped from the TLAS / bake**. That is the failure to expect:
  disappearing geometry, not misplaced geometry. Either way the uint-granular
  addressing cannot express a 6-byte stride, so the record needs a byte-granular
  stride. **Carry the encoding in the record, not as a define:** none of these
  shaders is compiled through `Shader_key`, so none receives
  `ERHE_VERTEX_POSITION_ENCODING`; add an encoding enum (or a flag bit in the
  existing `flags` field, `scene_tlas.hpp:63`) plus scale/offset to
  `Instance_record_data` / `Lm_instance_record` and branch on it in `fetch_vec3`'s
  callers. A record-carried selector keeps the float path working with the flag
  off during phase 5 and survives §6.3's per-primitive encoding, which a
  compile-time define would not. **Growing the record touches four coupled
  places:** `static_assert(sizeof(Instance_record_data) == 48)`
  (`scene_tlas.hpp:71`), the constructor's layout `VERIFY`s against the generated
  `Shader_resource` offsets (see the class comment at `:51-53`), and the GLSL
  struct in all three copies - `res/shaders/erhe_ray_hit.glsl` plus the two
  embedded ones at `lightmap_baker.cpp:528` and `:938`. Keep the 16-byte padding
  rule the `reserved0` / `reserved1` fields exist for. The consumers are
  `res/shaders/erhe_ray_hit.glsl:51` (`fetch_vec3`) used at `:117-119` - shared
  by DDGI and the ray-trace renderer - plus the two
  embedded copies in the lightmap baker (`lightmap_baker.cpp:528` used at
  `:654-656`, and `:938` used at `:970-972`), all of which read through a
  `uint`-typed buffer reference. Additionally `position_range.byte_offset` is no
  longer guaranteed 4-byte aligned. The record needs a byte-granular stride and
  a snorm16 fetch, plus the primitive's scale/offset. This path only runs when
  `use_ray_tracing_position_fetch` is false; the position-fetch path reads
  decoded positions from the AS and is fine **iff** the BLAS carries the dequant
  transform above.
* **Mesh component editing write-back.** `enqueue_gpu_position()`
  (`mesh_component_transform.cpp:691-758`, the format switch at `:742`) writes
  individual vertex positions back into the GPU stream and handles only
  `format_32_vec3_float` / `format_32_vec4_float` - anything else hits
  `continue`, i.e. the edit is silently dropped. Add the snorm16 case (encode
  with the primitive's stored AABB, clamp). **An edit that moves a vertex
  outside the stored AABB cannot be represented**: detect it and trigger a
  primitive rebuild, which recomputes the AABB, rather than clamping silently.
  This is the one place where quantization changes semantics, not just
  precision. The sibling `enqueue_gpu_edge_line_positions()` (`:762`, float
  writes at `:807-809`) targets `vertex_format_edge_line`, which §4.3 leaves
  unquantized, so it needs no change - but do not let that format drift without
  revisiting this. (`:926` is the *normal* write in the `write_stream` lambda,
  `:915-941`, used at `:990-991`; it is not a position path.)
* **CPU ray-trace / picking** (`bvh_geometry.cpp`, `tinybvh_geometry.cpp`,
  `embree_geometry.cpp`) read the separate `Cpu_buffer` float3 build - no change.

## Rollout

**Ordering principle: everything gets ready first, then the flag flips once.**
The encoder is the *last* step, not the third. Every phase before it is a
provable no-op (the config flag defaults false), and the phase that flips the
flag is the only one that can produce a visual regression - which makes
bisecting trivial and keeps each phase individually shippable.

The encoder cannot land earlier than it does: the moment the flag can be true,
the remaining shader conversions (phase 4) and the ray-tracing record work
(phase 5) must already be in, or the editor renders wrong and RT geometry
silently disappears - and RT is default-on on the dev machine.

| phase | content | verification |
| --- | --- | --- |
| 1 | **Primitive buffer AABB fields first** - `position_scale` / `position_offset` in `Primitive_struct`, written at all four sites of §3.1, **including `Draw_list_scene::write_entry_record()`**. Nothing reads them yet. | Renders identically with `use_draw_lists` ON and OFF; RenderDoc shows the two new vec4s carrying real AABBs on the draw-list path, not zeros. |
| 2 | Shader key axis + `Vertex_position_encoding` enum, `derive()` wiring (assigning the axis unconditionally, §2), the single define emitter in `attributes_source()` with the axis excluded from `Shader_key::get_defines()` (§5), **both §2.1 fixes: the shadow `Draw_purpose` key and the shadow-renderer prewarm**, `erhe_vertex_position.glsl`, `standard.vert` conversion. | Build opengl / vulkan / quest / metal; nothing changes. `standard.vert` now *reads* `position_scale` / `position_offset`, which is why phase 1 had to land first. `Shader_key::describe()` still shows the axis at 0 on color *and* shadow draws (it re-enumerates `ERHE_SHADER_INT` independently of `get_defines()`, so the exclusion does not hide it); no `Shader_variant_cache miss` lines in `logs/log.txt` after the first frames (`shader_variant_cache.cpp:47-55`). |
| 3 | Backend format plumbing: `format_16_vec3_snorm` cases in `to_vk_vertex_format()` **and** `to_mtl_vertex_format()`; the `VK_FORMAT_R16G16B16_SNORM` vertex-buffer support query into `Device_info`; **and the §1.1 minimum stride / attribute alignment hook itself** - a device-supplied value threaded into `Vertex_stream`'s packing (`vertex_format.cpp:44-58`, `:87-92`) and into `Mesh_memory` construction, defaulting to 1 so it is inert. Build the *fix* here even if the Metal constraint is still unconfirmed; it is a few lines and it keeps phase 6 to one variable. | Log the support query on the AMD 890M iGPU and on Quest. Still a no-op at runtime: with alignment 1 and the float3 formats, every stride is byte-identical to today - assert that in a unit test. (The §1.1 stride *behaviour* cannot be exercised here: no 6- or 14-byte stream exists until phase 6, since the formats are hard-coded `format_32_vec3_float` at `mesh_memory.cpp:34/66/96/135`.) |
| 4 | Remaining shaders (§5): the three Class B programs - `content_edge_lines.vert` plus the lightmap baker's two - and the `position_scale` / `position_offset` fields in their per-draw blocks. Also `tool.vert`'s decode conversion, for consistency (it is currently an orphan program, see §5). | Still a no-op; edge lines and a lightmap bake unchanged. |
| 5 | Direct stream-0 consumers (§7): BLAS vertex format + dequant transform, the byte-granular instance record + snorm fetch, mesh component edit write-back. | Still a no-op; RT, DDGI and a bake unchanged with the flag off. |
| 6 | **Encoder, both build paths** - `primitive_builder` (§4.1) and `build_buffer_mesh_from_triangle_soup()` (§4.2, including the bounding-volume reorder) - and `quantize_vertex_positions` in `Mesh_memory_config` (§6.1). **Flip the flag.** | The one phase that changes pixels. Box, brush, Sponza, Bistro and an unpublished-geometry import against phase-2 screenshots; RT and DDGI on (watch for *missing* instances, §7's real failure mode); a lightmap bake; edge lines; gizmo; a vertex drag inside the AABB and one that escapes it. Measure stream-0 pool `used` bytes via `get_memory_usage` before and after. Confirm the §1.1 stride / attribute-offset behaviour on every backend here, including Metal - this is the first phase in which a 6- or 14-byte stream exists. |
| 7 | Quest / OpenXR pass. | Fresh prompt + confirmation before the launch, per the standing rule; confirm Vulkan validation stays at 0 errors. |

Each phase: edit → build → independent review → fix severe findings → commit,
one commit per phase.

## Risks

* **Shadow / lit disagreement.** Once §2.1 is fixed, caster and lit pass decode
  from the same record and agree exactly. Until it is fixed they do not - this
  is the single highest-risk item in the plan, and phase 2 must not be
  considered done without a shadow-pass check.
* **Z-fighting on coplanar surfaces.** Quantization moves surfaces by up to
  `half_extent / 65534`. Two coplanar meshes with different AABBs pick up
  different errors and can interpenetrate. Watch the Bistro / Sponza floors and
  the ABeautifulGame board.
* **Edge lines over quantized fill.** `vertex_format_edge_line` stays exact
  float, so edge lines are drawn on the *unquantized* surface while the fill is
  quantized. `erhe_line_surface_bias.glsl` already biases lines toward the
  camera; confirm the existing bias covers the added mismatch - and if it does
  not, quantize the edge-line stream with the same AABB rather than widening the
  bias.
* **Shadow acne.** `SHADOW_BIAS` was tuned against exact positions. Re-check on
  a quantized Bistro.
* **Large single meshes.** A 200 m mesh in one primitive gets 3 mm steps. If
  that shows, the answer is §6.3's per-primitive heuristic (or splitting the
  import), not a wider format.
* **Skinned meshes.** The AABB is the rest-pose AABB from
  `calculate_bounding_volume()` over the source `GEO::Mesh` - exactly the space
  the quantized attribute lives in, since skin matrices are applied after
  decode. `joint_bounding_boxes` are unaffected. No special case needed, but
  verify on the frog and dolphin rigs.
* **Precision-sensitive debug views.** `Shader_debug::vertex_valency`,
  `polygon_edge_count` and the corner-cap paths read positions; check the debug
  visualizations after phase 6.

## Open question

Store `scale` / `offset` (chosen here) or raw `min` / `max`? `scale` / `offset`
costs the shader one `fma` instead of a `mix`. Since the decode is behind a
compile-time `#if`, the "identity for passthrough" argument does not apply (a
passthrough shader never reads the fields), so this is purely an ALU / clarity
call. Keep `scale` / `offset` unless a consumer needs the raw AABB.

## Implementation notes

Recorded as the phases land; the sections above are the design as planned, this
is where it differs.

### Phase 1 (commit 733c11648)

As specified. One addition: the UBO branch of `Primitive_interface` now logs the
resulting primitives-per-block, so §3's "log loudly if a frame does not fit" has
something to read. The desktop dev machine takes the SSBO path, so that line is
first exercised on Quest.

`Draw_list_scene::write_entry_record()` does **not** abort when the primitive has
no renderable `Buffer_mesh`. `classify_primitive()` guarantees one at
registration, but `refresh_object_records()` replays entries against a scene that
may have been mutated since - the same shape as the queued-op-vs-mutated-scene
abort that function has produced before - so it falls back to the identity affine.

### Phase 2 (commit 6f00e68ac)

As specified. `Vertex_position_encoding` and `get_vertex_position_encoding()`
landed in `erhe_dataformat` (`vertex_format.hpp`), which is the "move it down"
option §5 preferred over duplicating the mapping.

Verified on Vulkan and OpenGL: 50 shader variants each (13 shadow, 37 color),
every one reporting the axis at 0, and every cache miss during prewarm.

### Phase 3

**Deviation from §1.1's schedule.** The alignment hook is *not* threaded into the
`Vertex_stream` constructor and `finalize_stride()` as §1.1 proposed. Instead:

* `erhe::dataformat::Vertex_stream_packing` carries the two minimums
  (`min_attribute_alignment`, `min_stride_alignment`), both defaulting to 1.
* `Vertex_stream::repack()` / `Vertex_format::repack()` re-derive every offset and
  the stride under a given packing, and `Mesh_memory`'s constructor calls
  `repack()` on all seven of its formats - before anything can capture a stride.

The reason is that the constructors are shared with *source-side* formats
(`Triangle_soup`'s in the glTF importer, `Primitive_raytrace`'s float3-only one),
which must not be subject to a backend's vertex-buffer packing rules. Repacking
at the `Mesh_memory` boundary applies the constraint exactly where it belongs.
Phase 6 only has to set the two values on `Device_info`.

Consequences, both accepted:

* `repack()` always finalizes the stride, so it can differ from an *un*finalized
  `emplace_back()`-built stream. Tested both ways.
* The constraint is applied at one site, not enforced globally. The other
  GPU-facing vertex formats (`lightmap_baker`'s seam format,
  `content_wide_line_interface`'s, the debug/ImGui/hextiles renderers') bypass it.
  All of them are 32-bit float throughout, so every stride is already a multiple
  of 4 and a Metal-style rule is satisfied by accident - but that is an accident,
  not an invariant. Revisit if any of them gains an 8- or 16-bit attribute.

`Device_info::use_16_vec3_snorm_vertex_buffer` is written and logged but not yet
consumed; phase 6 consumes it alongside the config flag.

**Open question answered:** the desktop AMD 890M iGPU reports
`VK_FORMAT_R16G16B16_SNORM` with `VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT`, so vec3 is
viable there. Quest is still unchecked - the query logs itself at device init, so
the answer arrives with the first Quest run.

### Phase 4 (commit 60a0ba9b6)

As specified. Two things worth knowing:

* `content_edge_lines.vert` is not compiled on a normal desktop at all - the
  geometry-shader wide-line backend is built only when the device has no compute
  shaders, and the Vulkan backend rejects geometry shaders outright. It was
  verified by forcing that backend on under OpenGL, and the check was confirmed
  able to fail with a deliberate bad field reference. Any future change to that
  shader has the same coverage problem.
* Every lightmap draw record is pre-filled with the identity affine right after
  the `memset`, so a record the draw filter skips is wrong-but-finite instead of
  collapsing its mesh to a point.

### Phase 5

**BLAS: build transform, not instance transform.** Section 7 left the mechanism
open. The dequantization affine is applied as a *build* transform
(`VkAccelerationStructureGeometryTrianglesDataKHR::transformData`, Metal's
transformation matrix buffer), so the structure stores object space positions and
the instance transform is untouched.

Folding the affine into the *instance* transform instead would need no new GPU
plumbing at all, and it is tempting - but it is wrong. Normals and tangents are
fetched from stream 1, which is never quantized, and
`res/shaders/erhe_ray_hit.glsl` transforms them with the inverse transpose of
`world_from_object`. Making that matrix `world_from_node * dequant` would apply
the per axis half extent scale to attributes that never lived in encoded space,
warping every shading normal on a non-cubic AABB. Do not revisit this.

`get_blas_position_input()` (`mesh_memory.hpp`) is the single source of both the
format and the transform, so the two BLAS builders - the scene TLAS and the
lightmap baker's own cache - cannot disagree with each other or with the affine
the instance records carry.

**Answered, and it constrains the feature:** the desktop AMD 890M reports
`VK_FORMAT_R16G16B16_SNORM` with `VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT` but
**without** `VK_FORMAT_FEATURE_ACCELERATION_STRUCTURE_VERTEX_BUFFER_BIT_KHR`. A
quantized mesh therefore cannot feed a BLAS on the primary dev machine.

Decision (user, at the end of phase 5) - **SUPERSEDED, see "Revised policy"
below**: fall back to unquantized positions
when the device cannot ray trace the format - option (a) of section 7, resolved
at the vertex format rather than per BLAS. Phase 6 implements the gate in
`Mesh_memory`: `quantize_vertex_positions` only takes effect when the device can
also use the format as acceleration structure build input, and says so in the log
when it declines. Section 7's option (b), a separate float3 position copy for
BLAS input, is **not** implemented. Future work: a padded `snorm16x4_aabb`, which
Vulkan's mandatory AS format table does include - that is the enumerator the
design reserved for exactly this.

The `supported == false` path in the two BLAS builders is therefore a backstop
that the shipped configuration never reaches; it logs once rather than once per
mesh per frame.

**Instance records are byte granular now.** `position_stride_uints` became
`position_stride_bytes` in both `Instance_record_data` (48 -> 80 B) and
`Lm_instance_record` (64 -> 96 B), each gaining `position_encoding` plus the
scale / offset vec4s, and the `(element_size % 4) != 0 -> continue` guards that
would have silently dropped a quantized instance out of the TLAS and out of the
bake are gone. The stream-1 guard stays: that fetch is still uint granular.

**Pool ranges are now lcm(stride, 4) aligned** (`buffer_pool.cpp`). Element size
alignment alone is what makes `byte_offset / element_size` exact, but a 6 or 14
byte stride would then put half the ranges on a `2 mod 4` address - and the
instance records hand that address to a GLSL buffer reference declared
`buffer_reference_align = 4`. Every fetched position would have been 2 bytes off.
For every stride in use today lcm(stride, 4) == stride, so nothing moved.

**Deviation in the edit write-back.** Section 7 asked for an out-of-AABB vertex
edit to trigger a primitive rebuild rather than clamp. It clamps, warns once per
drag, and lets commit rebuild: `Move_mesh_vertices_operation` already constructs a
fresh `Primitive` from the geometry, which recomputes the AABB, so the committed
result is exact and only the live drag preview pins the vertex to the box face.
Rebuilding mid-drag per frame was not worth it.

**Known, not fixed:** the snorm16 fetch reads the `uint` word containing byte
`offset + 4`, which for the last vertex of an allocation can read up to 2 bytes
past it. Inside a pool block that is harmless; it is only a real over-read if an
allocation ends exactly at the end of the buffer. Revisit in phase 6 if a
validation layer flags it.

### Phase 6

**The stream-0 strides are 8 and 16, not 6 and 14.** Mesh_memory requires a stride
that is a multiple of 4 (`Vertex_stream_packing::min_stride_alignment`), i.e. the
"Metal (pad to 4)" column of the 1.1 table, on every backend. Two independent
reasons, both found by actually turning the flag on:

* `Buffer_pool` aligns each allocation to its own stream stride, and the lockstep
  invariant needs `byte_offset / stride` to advance identically across streams
  whose strides differ. Aligning stream 0 to `lcm(14, 4) = 28` instead - the
  obvious way to get a 4-byte aligned range start - pads a 14-byte stream by two
  elements and a 68-byte stream by one, and the two diverge. A skinned glTF
  import then fails with "vertex stream allocations out of lockstep". A
  4-byte-multiple stride keeps alignment == stride, which is what the invariant
  needs, and gets the aligned range start for free.
* The ray-tracing instance records hand the stream-0 range address to a GLSL
  buffer reference declared `buffer_reference_align = 4`. A range starting at
  2 mod 4 would read every position 2 bytes off.

So the saving is 33 % of stream 0 unskinned (12 -> 8) and 20 % skinned (20 -> 16),
not the 50 % / 30 % the Expected win table quotes for the unpadded layout. Update
that table's numbers when reading it; the unpadded layout is only reachable if
both constraints above are solved, and neither is a backend limitation.

**Measured** on the default scene (7 meshes), by forcing the encoding on past the
device gate: stream-0 pool 48696 -> 32464 bytes (-33.3 %), total mesh memory
573984 -> 555208 bytes. The rendered viewport differs in 201 of 1 469 460 pixels
(0.014 %), maximum channel delta 6/255, mean 1.04 - antialiasing edges moving by
a fraction of a pixel, which is exactly what a sub-quantization-step geometry
shift looks like. A skinned glTF import (the unpublished-geometry path, 4.2)
completes with no errors.

**The gate is real on this hardware.** With `quantize_vertex_positions` set in
`config/editor/mesh_memory.json`, the desktop AMD 890M declines and logs:

    Vertex position quantization requested but declined: the device reports
    format_16_vec3_snorm as vertex buffer input = true, as acceleration structure
    build input = false. Positions stay unquantized (float3).

That warning goes to `erhe.scene_renderer.startup`, not
`erhe.scene_renderer.mesh_memory` - the latter has no entry in
`config/editor/logging.json` and is filtered out of `logs/log.txt`, so the
warning would have been invisible exactly when it matters.

Note the config field is `added_in=2`, so the JSON file needs `"_version": 2`
before the flag is read at all; the codegen loader ignores newer fields in an
older file.

#### Phase 6: known limits and follow-ups

* **The BLAS assumes the position is at offset 0 of stream 0.** Both builders
  pass `vertex_byte_offset = vertex_range.byte_offset` with no attribute offset
  added. That is true for every content format today. It is also the reason
  stream 0 was NOT reordered to put the two 1-byte joint attributes before the
  position - which would have put the position at a 4-byte aligned offset 8 and
  made 1.1's attribute-alignment question moot. Anyone reordering stream 0 must
  add the position attribute's offset at both BLAS build sites first.
* **The attribute-alignment half of the 1.1 hook is still inert.**
  `Device_info::min_vertex_attribute_alignment` is read in `Mesh_memory` and
  assigned by no backend, so it stays 1. With quantization on, the skinned
  stream 0 puts `joint_indices` at offset 6 and `joint_weights` at offset 10 -
  neither 4-byte aligned. GL and Vulkan core do not care. **Metal and MoltenVK's
  portability subset may**, and no Metal run with the flag forced on has been
  done. Set `min_vertex_attribute_alignment = 4` on Metal, or reorder stream 0
  per the point above, before calling this cross-backend.
* **Far-from-origin precision.** The measured 201-pixel figure was taken on a
  scene near the origin. For a primitive centred ~500 m out, `ulp(500 m)` is
  about the size of the quantization step itself, so the effective worst-case
  error is roughly twice the nominal half-step. Encoder and decoder pay it
  symmetrically, so they never disagree - but this is the regime where the
  Z-fighting risk bites, and it is why the Bistro / Sponza pass is still owed.
* **The affine is written out in four places** (`primitive_builder.cpp`,
  `primitive.cpp`, `primitive_buffer.cpp`, `mesh_component_transform.cpp`) with
  the same epsilon, because `erhe_primitive` cannot see `erhe_scene_renderer`.
  They agree today only by inspection. Moving `Position_quantization` /
  `get_position_quantization` down into `erhe_math` next to `Aabb` - the same
  move phase 2 made for `Vertex_position_encoding` - would make divergence
  impossible. Worth doing before anyone touches the encoding.
* **16-bit indices would reintroduce the alignment problem.** The instance
  records hand `index_address` to the same `buffer_reference_align = 4` buffer
  reference. Every `Buffer_info::index_type` in the tree is
  `format_32_scalar_uint`, so index ranges are 4-aligned; a 16-bit index path
  would need the same stride rule stream 0 got.
* Phase 5's "known, not fixed" 2-byte over-read is **retired** by the 4-byte
  stride rule: with stride 8 or 16 the word containing byte `offset + 4` is
  inside the same vertex.

### Revised policy: quantize everywhere that can, fall back only where it cannot

**This supersedes the phase 5 decision above.** That decision - "fall back to
unquantized positions on a device that cannot ray trace the format" - turned the
whole session off because of one consumer. The policy is now:

> Every path that can use quantized positions does. A path that genuinely
> cannot gets a fallback of its own, and only that path pays for it - which in
> the general case means a primitive may carry more than one copy of its
> positions.

Applying that turned out to remove the need for a copy entirely, at least on
every Vulkan device.

**The one consumer that cannot take `format_16_vec3_snorm` is the acceleration
structure build**, and only on devices that do not advertise it (the desktop AMD
890M is one; the 3-component format is not in Vulkan's mandatory table). Every
other consumer was already handled by phases 1-6: the vertex shaders decode, the
instance-record fallback fetch decodes, the position-fetch path reads decoded
positions out of the structure, the CPU BVH backends have always used a separate
float3 `Cpu_buffer`, and the mesh-component edit write-back encodes.

**`format_16_vec4_snorm` IS in the mandatory table**, and section 7 dismissed it
only because "a 4-component format cannot be read over a 6-byte stride". That
premise expired in phase 6: the stream-0 stride is padded to 8 (unskinned) or 16
(skinned), both whole numbers of int16 lanes. So the BLAS reads the *same*
quantized buffer in place as snorm16x4 - the build takes xyz from the position
format and ignores any further component - with the same dequantization build
transform as before. No second copy, no extra memory, no separate pool.

`Device_info` therefore carries two acceleration-structure queries, and
`get_blas_position_input` prefers the 3-component format where it exists and
falls back to reading the same bytes as 4-component where it does not. The
session gate declines quantization only when ray tracing is available and
*neither* format is.

Measured on the AMD 890M, which needs the 4-component path:

    VK_FORMAT_R16G16B16_SNORM  ... acceleration structure build input: NOT supported
    VK_FORMAT_R16G16B16A16_SNORM ... acceleration structure build input: supported

    quantized + ray tracing:  7 BLAS built, 0 errors
    stream-0 pool             48696 -> 32464 bytes, unchanged by ray tracing
    vs float3 + ray tracing:  201 of 1469460 viewport pixels differ, max 6/255

That 201-pixel figure is exactly the raster-only figure, i.e. ray tracing adds no
error of its own. Note it can only be measured with DDGI OFF: DDGI accumulates
temporally and is not deterministic between runs - two *identical* float3 runs
differ in 17.9 % of pixels with DDGI on, and in 0 pixels with it off. Any future
comparison of a rendering change must disable DDGI or it measures nothing.

#### Metal

Metal is the one backend where the in-place read is not merely an alternative but
the only legal option, and where it is still not enough. Two documented rules on
`MTLAccelerationStructureTriangleGeometryDescriptor::vertexStride`:

* the stride must be a multiple of the vertex format size -
  `MTL::AttributeFormatShort3Normalized` is 6 bytes and neither 8 nor 16 is a
  multiple of it, while `Short4Normalized` is 8 and divides both. So the Metal
  device init clears `use_16_vec3_snorm_acceleration_structure_vertex_buffer`;
  without that the 3-component branch would win and the mapping added here would
  be dead code on Metal.
* the stride must be at least 12 bytes. The unskinned quantized stream-0 stride
  is 8, which no choice of format fixes.

`Device_info::min_acceleration_structure_vertex_stride` publishes the second rule
(Vulkan 1, Metal 12), and `choose_position_format` declines quantization when ray
tracing is available and that minimum exceeds the smallest quantized stream-0
stride. On Metal, therefore, quantization and ray tracing remain mutually
exclusive for now, loudly, rather than producing meshes missing from the TLAS.
Lifting it means raising the stream-0 stride to 12 or 16 on Metal (which gives
back part of the saving) - not attempted; **untested on hardware**, there is no
Metal device in this setup.

#### If a device ever supports neither format

Then, and only then, the copy the policy describes becomes necessary: a second,
unquantized position range used solely as BLAS build input. It is NOT
implemented. The shape it should take, so nobody re-derives it:

* An optional `Buffer_mesh::raytrace_position_buffer_range` + allocation, exactly
  like the existing optional `edge_line_joint_buffer_range` - a side buffer
  populated only when needed.
* Its own `Buffer_pool` (float3, stride 12), so it stays out of the stream-0
  lockstep group entirely. BLAS indices are relative to the range start, which
  the build already handles via `vertex_byte_offset`, so the copy only has to
  hold the same vertices in the same order.
* Written by the two encoders alongside the quantized stream, where the
  unencoded positions are already in hand.
* **Watch the arithmetic before implementing it.** A mesh carrying both pays
  8 + 12 = 20 bytes per vertex where float3 alone costs 12 - a *regression* for
  any mesh that is ray traced. It is only a win if the copy is confined to
  meshes actually in the TLAS, which argues for allocating it lazily per BLAS
  cache entry rather than at mesh build time. Do not implement the always-on
  variant.
* Both BLAS builders assume the position sits at offset 0 of stream 0; a copy
  would make that assumption false for the copy's range too. Fix that first.

### Phase 7 (Quest / OpenXR)

Run on a Quest 3 over two launches, with `quantize_vertex_positions` on.

**The device query, which is what phase 7 existed to answer:**

    VK_FORMAT_R16G16B16_SNORM as a vertex buffer format: supported,
                              as acceleration structure build input: supported
    VK_FORMAT_R16G16B16A16_SNORM as acceleration structure build input: supported
    Vertex position storage: snorm16x3 quantized into the primitive AABB

So Quest supports the 3-component format outright, in both roles - it does not
need the in-place snorm16x4 read the desktop AMD 890M does. The gate engages,
and ray query is enabled at the same time (`Ray query (GPU ray tracing):
enabled, position fetch: not available`), so the quantized geometry feeds real
BLAS builds AND the fallback instance-record fetch path, which is the one that
decodes snorm16 lanes in the shader. User-verified visually on both runs:
correct rendering, 73/72 fps.

**Vulkan validation: 0 errors** over a ~4 minute run with the layer enabled and
the SPIR-V cache wiped by a clean reinstall. Every remaining message is a
BestPractices/lint warning of a family that predates this feature:
`ImageBarrierAccessLayout` (20 - the known ID-render barrier access-mask bug),
`vkBindImageMemory-small-dedicated-allocation` (18),
`vkCreateFramebuffer-attachment-should-be-transient` (14),
`ShaderOutputNotConsumed` (10), `deprecated-extension` (10),
`TransitionUndefinedToReadOnly` (6), `Error-Result` (2). Nothing about the
8/16-byte stream 0, nothing about the acceleration-structure transform buffer,
and nothing about a snorm16x4 read over a snorm16x3 buffer. Phase 5's suspected
2-byte over-read stayed retired.

**The Primitive UBO budget line is unreachable on Quest**, contrary to what
phase 1 assumed. `vulkan_device_init.cpp` sets `use_shader_storage_buffers =
true` unconditionally, so every Vulkan device - Quest included - takes the SSBO
path, and Metal sets it true as well. The record growing 192 -> 224 bytes
therefore costs nothing anywhere the editor currently runs; the UBO branch is
reachable only on the OpenGL backend without compute shaders. Anyone wanting to
exercise that line must force that backend, not reach for a mobile device.

### The desktop verification sweep the plan owed

Phase 6 was measured on the 7-mesh default scene only. The rest of what the plan
asked for was run after phase 7, with the flag on by default and DDGI off, by
driving the in-editor MCP server through a fixed sequence (default scene ->
skinned rigs -> Sponza -> four debug views), capturing each view only once two
consecutive frames were identical over the viewport region, and diffing against
the same sequence run with the flag off.

**The harness was controlled first**: two float3 runs are pixel-identical in
every view. So every difference below is the encoding and nothing else. (Settle
on the VIEWPORT CROP, not the whole image - the ImGui status bar prints a
per-frame millisecond count, so a full-image comparison never repeats.)

| view | pixels differing | max channel delta |
| --- | --- | --- |
| default scene | 201 / 1546800 (0.013 %) | 6 |
| `vertex_valency` debug | 0 | 0 |
| `polygon_edge_count` debug | 0 | 0 |
| Sponza interior | 16.9 % | 32 (mean 0.095) |
| frog pond (skinned rigs) | 77.9 % | 132 |

The default-scene figure reproduces phase 6's exactly. The two precision
sensitive debug views are bit-identical, which is the strongest single result
here: valency and edge count are computed per vertex and would break loudly on a
mis-decoded position.

Sponza is the large-AABB case, and 16.9 % of pixels differing at a mean of 0.095
is antialiasing edges moving by a fraction of a pixel across a scene that is
almost entirely silhouette - no Z-fighting, no shadow acne, no cracks at the
arch seams.

**The frog pond's 77.9 % is Z-fighting, and it is not a quantization defect**
(confirmed by the user on inspection). The pond bottom and the water plane are
modelled coincident; any sub-millimetre shift decides the depth test differently
over a large area, and a quantization step on that AABB is well under a
millimetre. Coincident geometry is exactly the case the Risks section flags:
quantization does not create the ambiguity, it re-rolls it.

Memory, measured on the same scenes: stream-0 pool 3974664 -> 2634800 B with
Sponza loaded (-33.7 %) and 1381512 -> 921008 B for the two rigs (-33.3 %), i.e.
the predicted 33 % of stream 0, at scene scale.

**The lightmap bake runs with the flag on.** `lightmap_prepare_tiles` (7 meshes,
12 pieces, 4 tiles) followed by `lightmap_bake_gbuffer` and `lightmap_bake_direct`
all succeed with no errors, which means the baker's own BLAS cache built from
quantized positions - the path that phase 5's device gate would have refused.
Rendering with lightmaps on shows the skinned frog rig correctly, which also
retires the "the rest-pose AABB was never visually confirmed" item.

**The gizmo and the selection edge lines are correct** with quantization on: the
translate / rotate gizmo lands on the selected mesh and the outline follows the
frog's silhouette.

**Still not done**: an out-of-AABB vertex drag. It needs real mouse interaction,
which the MCP path does not reach, so the clamp-and-warn behaviour described
above is still only verified by reading the code.

## Provenance

This plan was verified against the tree over six rounds of independent review;
the final round found no blocking or major defects and no unlisted stream-0
consumers. Every file:line citation was checked against the source at the time
of writing. Treat it as the spec: the non-obvious findings it records - the two
key-rebuild sites in §2.1, the four primitive-buffer write sites in §3.1, the
two build paths in §4, the backend format gaps in §Current state - each cost a
review round to find, and are easy to miss when re-deriving the design from
scratch.
