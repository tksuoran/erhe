# Compact attribute encodings for the optimized mesh variant

Stability: mostly stable

**Status: implemented and verified.** All six steps are in, the optimized
vertex is 36 bytes as planned, the unit tests pass, and the A/B image set
in `logs/attr_encoding_ab/` has been **inspected and accepted by the user**
-- which is the acceptance criterion for this work (see Verification).
VS 2026 Vulkan Debug and VS 2026 OpenGL Debug both build clean. Still
outstanding: the headless pick / export interaction sanity pass. Quest is
future work, not an outstanding item.

Scope of this document: **which encoding to use for each
non-position vertex attribute** of the optimized variant, what
meshoptimizer gives us, and what each choice costs. It deliberately stops
there -- no phase breakdown, no file-level task list.

Read `doc/meshoptimizer_integration.md` first; this builds directly on the
encoding split it describes (requirements 8-11).

## Ground rules (inherited, not up for discussion here)

- The **base** (`render_shape`) variant keeps full-precision float
  attributes, exactly as today. Every encoding below applies **only** to
  the optimized variant (`optimized_render_shape`), and only when
  `optimize_meshes` is on.
- The optimized variant is **fill-only**, welded, and carries no facet id.
  Edge lines, corner/centroid points and the expanded solid-wireframe copy
  are the base variant's, untouched.
- Picking / ID rendering, CPU raytrace, BLAS sources, physics collision,
  glTF export and geometry reconstruction all read the **original** shape.
  They are unreachable from the optimized format by construction, so no
  encoding below can reach them -- the same argument that made position
  quantization safe (`get_blas_position_input()` returns early on
  passthrough; the RT instance records derive their encoding from the
  original variant).
- In-place GPU vertex edits (paint, weight paint, live drag) address the
  base variant. Encoded attributes are therefore never written through.
- **The half float formats are banned.** `16_vec2_float` and its siblings
  are not to be used for vertex attributes here.

## Where the format lives today

`Mesh_memory` derives `vertex_format_{not_,}skinned_optimized` from the
content formats in its constructor: drop `custom_attribute_id`, substitute
`optimized_position_format` for the position. That derivation is the single
place a per-attribute substitution table belongs.

Current per-vertex cost, non-skinned, quantized position, after repack
(stride alignment 4):

| stream | attributes | bytes |
|---|---|---|
| 0 | position `16_vec3_snorm` (6) | 8 |
| 1 | normal `32_vec3_float` 12, tangent `32_vec4_float` 16, texcoord 0/1/2 `32_vec2_float` 8+8+8, color_0 `32_vec4_float` 16 | 68 |
| 2 | normal_smooth `32_vec3_float` 12, aniso_control `8_vec2_unorm` 2, valency_edge_count `16_vec2_uint` 4 | 20 |
| | **total** | **96** |

(The base variant is 104: float3 position and the 4-byte facet id.)

Stream 1 is 71% of the vertex and is entirely float. That is the target.

## Three independent axes, only one of which keys on component count

An encoding choice has to be judged on three separate things, and they do
not move together:

1. **Memory** -- bytes per vertex, after the stride and attribute alignment
   the repack imposes. Purely a function of the stored bit width and the
   resulting padding. A 6-byte attribute in a stream that pads to 4 can
   cost 8.
2. **Precision** -- what the encoding does to the value. Independent of
   both of the others: a 4-byte octahedral normal is *more* accurate than a
   4-byte `8_vec4_snorm` one, and a 6-byte `16_vec3_snorm` normal is
   accurate enough to be indistinguishable from float while still not being
   the cheapest option.
3. **Shader-side cost** -- and *this* is the one that keys on component
   count. `Shader_stages_create_info::attributes_source()` declares each
   attribute with `to_glsl_attribute_type(format)`, and the vertex-fetch
   hardware normalizes snorm/unorm for free. So a change that preserves the
   declared GLSL type (`32_vec3_float` -> `16_vec3_snorm`,
   `32_vec4_float` -> `8_vec4_unorm`) needs no shader work at all, while a
   change of component count or of meaning (octahedral, quaternion,
   implicit-sum, an affine range) needs a decode helper and a
   format-derived `ERHE_VERTEX_<attribute>_ENCODING` axis, the way
   `ERHE_VERTEX_POSITION_ENCODING` / `erhe_vertex_position.glsl` already
   work for positions.

The plan below deliberately spends shader-side cost where it buys the most
memory and precision, rather than taking the free-but-weaker option.

## The per-attribute plan

| attribute | today | proposed | bytes | decode axis |
|---|---|---|---|---|
| normal + tangent + handedness | `32_vec3_float` 12 + `32_vec4_float` 16 = 28 | one **TBN quaternion**, `16_vec4_sint` | 8 | `ERHE_VERTEX_TBN_ENCODING` |
| texcoord_0 | `32_vec2_float` 8 | `16_vec2_unorm` + per-primitive UV affine | 4 | `ERHE_VERTEX_TEXCOORD_ENCODING` |
| texcoord_1 | `32_vec2_float` 8 | `16_vec2_unorm` + per-primitive UV affine | 4 | same axis |
| texcoord_2 (lightmap) | `32_vec2_float` 8 | `16_vec2_unorm`, no affine | 4 | none needed (see below) |
| color_0 | `32_vec4_float` 16 | `8_vec4_unorm` | 4 | none -- `vec4` either way |
| joint_weights | `8_vec4_unorm` 4 | `16_vec3_unorm` **implicit-sum** | 6 | `ERHE_VERTEX_JOINT_WEIGHTS_ENCODING` |
| joint_indices | `8_vec4_uint` 4 | unchanged (but permuted, see below) | 4 | none |
| aniso_control | `8_vec2_unorm` 2 | unchanged | 2 | none |
| normal_smooth | `32_vec3_float` 12 | **dropped** from the optimized format | 0 | none |
| valency_edge_count | `16_vec2_uint` 4 | **dropped** from the optimized format | 0 | none |

Resulting layout, non-skinned:

| stream | attributes | bytes |
|---|---|---|
| 0 | position `16_vec3_snorm` (6) | 8 |
| 1 | TBN quat 8, texcoord 0/1/2 4+4+4, color_0 4 | 24 |
| 2 | aniso_control 2 | 4 |
| | **total** | **36** |

**96 -> 36 bytes per vertex (-62%)**, and -65% against the 104-byte base
variant, on top of the -62% vertex-fetch win the reordering already
delivers.

Skinned, stream 0 becomes position 6 + joint_indices 4 + joint_weights 6 =
16, which is the stride it already has -- the 16-bit implicit-sum weights
are free in stride terms, so they buy precision and exact normalization at
no memory cost.

### TBN quaternion

Normal, tangent and handedness are one orthonormal frame, so store one.
`meshopt_encodeFilterQuat` encodes a unit quaternion into stride 8 with
K-bit components, dropping the largest component and recording its index in
the low 2 bits; `meshopt_decodeFilterQuat` is the reference decode to port
to GLSL.

Two things this forces:

- The attribute is declared **`16_vec4_sint`** (`ivec4` in GLSL), not
  `16_vec4_snorm`. The 2-bit component index lives in the low bits of a
  lane, and reading through the hardware snorm conversion would mean
  recovering it by multiplying back by 32767 and rounding. Exact integer
  access is cleaner and the decode is doing arithmetic anyway.
- **Handedness needs an explicit carrier, and meshopt cannot carry it.**
  `q` and `-q` are the same rotation, so the quaternion's sign is the
  conventional place to put the bitangent sign -- but
  `meshopt_encodeFilterQuat()` canonicalizes on the largest component and
  *discards* that sign, and its decode recovers its scale from the w lane
  with `| 3`, so the spare bits there are not spare either. **Checked
  against the filter, answer no**, so the implementation took this
  section's recorded fallback: erhe's own largest-component-dropped
  encoding in the same 8 bytes (`encode_tbn_quaternion()` in
  `erhe_primitive/mesh_optimizer.cpp`), structurally identical, with the
  control lane holding the omitted component index in bits 0..1 and the
  handedness in bit 2. `res/shaders/erhe_vertex_tbn.glsl` is the matching
  decode; the two must be read together.
- erhe's tangent is not guaranteed orthogonal to the normal, so the encode
  Gram-Schmidts first. That is a real (small) precision change, distinct
  from the quantization itself.

### Texcoord affine

`16_vec2_unorm` alone cannot represent tiling UVs outside [0, 1], so
texcoord_0 and texcoord_1 carry a **per-primitive UV scale/offset**,
mirroring exactly what the position AABB affine already does -- same shape
of side data, same per-primitive record, same decode-in-the-vertex-shader
plumbing.

texcoord_2 is the lightmap UV set and is in [0, 1] by construction, so it
needs no affine and no side data. Whether it shares the decode axis with
the other two (with an identity affine) or is simply declared as a plain
`vec2` attribute the shader reads unchanged is a layout decision for the
next pass; the second option is free.

### Implicit-sum joint weights

Skinning weights sum to 1, so the fourth is redundant. Encoding, per
vertex:

1. Sort the four (index, weight) pairs so the **smallest weight is last**,
   permuting `joint_indices` in lockstep.
2. Quantize the first three to `16_vec3_unorm`, choosing the roundings so
   the three stored values plus the implied fourth sum to exactly one unit.
3. The shader reconstructs `w3 = max(0, 1 - (w0 + w1 + w2))`.

Call the encoding **`unorm16x3_implicit_sum`**
(`erhe::dataformat::Vertex_joint_weights_encoding::{passthrough,
unorm16x3_implicit_sum}`, axis `ERHE_VERTEX_JOINT_WEIGHTS_ENCODING`).

Dropping the smallest weight is what bounds the reconstruction error, and
it makes the stored order canonical -- which incidentally helps the
geometry-path weld, since that is a bitwise compare and two corners with
the same influences in different author order currently do not merge. It
depends on the weights actually being normalized, which is an import-time
responsibility (see Verification and sanitization).

### Dropped attributes

`normal_smooth` (`a_normal_1`) and `valency_edge_count` (`a_custom_2`) are
dropped from the optimized format outright -- 16 of the 60 saved bytes, and
the cheapest 16. No `res/shaders/*` file reads `a_normal_1`; the smooth
normal the wide-line compute pass uses comes from the separate edge-line
format. `valency_edge_count` feeds a fragment debug path, which uses the
base variant.

`Shader_key` does not need presence axes for either. It already enumerates
presence booleans for normal_0, tangent, texcoords 0-2, color_0,
aniso_control and the joint pair, and the facet id is already dropped from
the optimized format on the same footing.

## What meshoptimizer gives us, and what it does not

- `meshopt_encodeFilterQuat` / `meshopt_decodeFilterQuat` -- **turned out
  not to be usable**, see the TBN section: the encoder discards the
  double-cover sign the handedness bit needs, and the w lane has no spare
  bits. The scheme is still the model erhe's own encoder follows.
- `meshopt_quantizeSnorm` / `meshopt_quantizeUnorm` -- drop straight in.
  They are redundant with `erhe::dataformat::convert()`, but routing the
  quantization through meshopt where the two agree is the precedent
  positions set, and it keeps the encode auditable against one reference.
- `meshopt_encodeFilterOct` -- not used by this plan; the quaternion
  subsumes it for normal + tangent.
- `meshopt_encodeFilterColor` (YCoCg) -- **future work**. It costs the same
  4 bytes as plain `8_vec4_unorm`, so it buys quality, not size.
- **No affine quantization helper** -- the same gap positions already hit.
  The UV affine is ours to write, modelled on the position one.
- `meshopt_quantizeHalf` is unusable here: half formats are banned.

## Where the work lands

- **Format derivation**: the substitution table in the `Mesh_memory`
  constructor, beside the existing facet-id drop and position
  substitution.
- **Soup path**: `Primitive_shape::make_buffer_mesh()` already calls
  `erhe::dataformat::convert()` for every non-position attribute, so the
  plain re-encodings (color, lightmap UV) come for free. The quaternion,
  the UV affine and the implicit-sum weights each need an encode branch
  beside the existing `encode_position` one.
- **Geometry path**: `Build_context::take_optimizable_snapshot()` today
  `return false`s -- i.e. silently builds **no optimized variant at all** --
  whenever an optimized attribute format differs from the staged one,
  position excepted. Every encoding here needs a conversion case there.
  This is the mandatory change, and it is also where the encodings pay
  twice: the geometry-path weld is a bitwise compare over the **encoded**
  bytes, so quantized attributes merge strictly more corners than float
  ones. The soup path welds on source floats before the sink conversion, so
  it gets the memory win but not the extra weld; its disk cache stores only
  the remap pair keyed on the source soup and is unaffected either way.
- **Shader side**: three new `ERHE_VERTEX_<attribute>_ENCODING` axes, each
  emitted from the vertex format the way the position one is (never from
  `Shader_key::get_defines()`), each with its own
  `erhe_vertex_<attribute>.glsl` decode and its own `#ifndef` backstop.
  These do not multiply the prewarm set: the axes are derived from the
  vertex format, and the prewarm walks already enumerate the base and the
  optimized format side by side (`Shadow_renderer::prewarm_pipelines()`
  explicitly, `Forward_renderer::prewarm_standard_variants()` by bucketing
  real meshes with `Mesh_variant::optimized`). Keep each axis binary
  (passthrough / encoded) anyway.
- **Import sanitization**: normals and tangents must arrive finite and
  within [-1, 1], and joint weights normalized. `convert()`'s
  `16_vec3_snorm` destination **asserts** on out-of-range input rather than
  clamping, and that assert stays -- the fix for a bad asset belongs
  upstream, at import, not in a silent clamp in the encoder.

## Verification

Rendering identity is not the criterion here: every encoding above is
lossy by design, so the expected result is a small non-zero difference, the
same way the position quantization epsilon is expected and is itself the
proof the optimized variant is being rendered.

The acceptance criterion is **user visual inspection**.
`scripts/attr_encoding_ab.py` produces the image set, on **ABeautifulGame
only** -- Bistro is out of scope for this work. It builds on the harness in
`doc/meshoptimizer_integration.md` ("How to verify"): MCP port 3743, DDGI
off, paused time, pinned layout, control pair first. Four editor launches,
into `logs/attr_encoding_ab/`:

| capture | `optimize_meshes` | `quantize_vertex_positions` |
|---|---|---|
| `off_a`, `off_b` | false | false |
| `on_qoff` | true | false |
| `on_qon` | true | true |

There is only one `off` side because `quantize_vertex_positions` affects
the optimized variant alone -- which is what makes the `on_qoff` / `on_qon`
split separate the attribute encodings from the already-accepted position
epsilon. Each comparison gets a side-by-side PNG, a difference PNG
amplified 8x so a 1-LSB difference is actually visible, and a percentage
over the cropped viewport.

Measured (differing pixels of the cropped viewport, VS 2026 Vulkan Debug):

| comparison | differing | worst | within 4 LSB |
|---|---|---|---|
| control `off_a` vs `off_b` | **0.000%** | 0 | -- |
| attributes `off_a` vs `on_qoff` | 1.085% | 17 LSB | 99.9% |
| all `off_a` vs `on_qon` | 1.492% | 107 LSB | 97.1% |
| positions `on_qoff` vs `on_qon` | 1.037% | 107 LSB | 95.9% |

So the attribute encodings together move about 1% of viewport pixels,
99.9% of the differing channel samples by 4 LSB or less, on top of a
control pair that is exactly identical. Side by side the two renders are
indistinguishable by eye. **The user inspected this image set and accepted
it** -- that, not a pixel threshold, is what "verified" means here.

### Unit tests

`src/erhe/primitive/test/test_attribute_encodings.cpp`, 9 tests, run via
the `erhe_primitive_tests` target. Each encoding is an encoder/decoder pair
that must agree exactly and whose mismatch renders subtly wrong rather than
failing, so the decoders in that file are **line-by-line C++ mirrors of the
GLSL**: a change made to one side and not the other fails there instead of
becoming a puzzling image. Covered: the TBN quaternion round trip across
orientations reaching all four omitted-component cases (handedness compared
exactly -- it is a bit, and mirrored UV shells depend on it), degenerate TBN
input yielding a finite orthonormal frame rather than NaN, the joint
influence sort (descending, stable, indices permuted along, normalizing and
rejecting negative weights), the implicit-sum weight round trip and its
never-sum-past-one invariant, the texcoord affine (identity without a
range, round trip for tiling UVs), the three encoding axes derived from the
format including the texcoord axis being keyed on channel 0, and the
geometry path building the whole encoded variant end to end -- where a
missing conversion surfaces as a null `optimized_render_shape`.
Mutation-checked: moving the handedness bit in the encoder fails two of
them.

Two harness notes worth keeping:

- **A non-zero control row invalidates the whole batch.** Two identical
  runs must render identically; one batch here came back with a 0.398%
  control (255-LSB outliers, a shot taken before the scene had settled)
  and every other row in it was measuring that too. The script says so in
  its own summary now. Re-run.
- **A passing control pair is necessary and not sufficient** -- it cannot
  tell you the optimized variant was built or selected at all. The script
  therefore also dumps the per-variant GPU strides and attribute formats,
  and that dump is what caught the texcoord substitution being missing
  from step 4 while every other part of that step was in place.

## Implementation plan

Six steps, ordered cheapest-and-least-risky first, each a complete vertical
slice (format substitution -> soup path -> geometry path -> shader ->
unit test) that leaves the tree renderable and revertible on its own. Per
step: edit, build the primary tree (`build_ninja_win_vulkan`, Debug),
self-review the diff, commit. Test suite, build sweep and A/B capture run
**once**, at the end.

Two pieces of shared machinery get built the first time a step needs them
and are reused unchanged afterwards:

- **The substitution table.** `Mesh_memory`'s constructor lambda
  `make_optimized_format()` currently hardcodes "erase the facet id,
  substitute the position format". It becomes a table walked per attribute:
  drop, keep, or substitute a format. Every step below is an entry in that
  table.
- **The snapshot converter.** `Build_context::take_optimizable_snapshot()`
  today gathers attribute bytes with a `memcpy` and `return false`s -- i.e.
  silently builds no optimized variant -- whenever the staged and optimized
  formats differ, position excepted. It gains a per-attribute conversion
  dispatch beside the existing `encode_position` branch. Declining stays the
  behavior for a genuinely unknown pair, so an unhandled combination is a
  missing variant, never wrong bytes.

### Step 1 -- drop `normal_smooth` and `valency_edge_count`

Pure removal, no encoding, no shader work: -16 bytes and it exercises the
substitution table on its own.

- `mesh_memory.cpp`: extend the erase predicate to
  `normal_attribute_smooth` and `custom_attribute_valency_edge_count`.
- Stream 2 of the optimized formats is then `aniso_control` alone, 2 bytes
  at a 4-byte stride. It has to stay a stream: `take_optimizable_snapshot()`
  requires the optimized and content formats to have equal stream counts.
- Nothing else changes -- both paths already handle an attribute the
  optimized format does not ask for.

Watch for: a shader that reads `a_normal_1` or `a_custom_2` under a
`#ifdef` we did not find. `Shader_key` carries no presence axis for either,
so a variant built from the optimized format must compile and render
identically without them.

### Step 2 -- `color_0` to `8_vec4_unorm`

`vec4` on both sides, so no shader change; this is where the snapshot
converter gets written.

- `mesh_memory.cpp`: substitution entry.
- `primitive.cpp` (soup path): nothing -- `make_buffer_mesh()` already
  routes non-position attributes through `erhe::dataformat::convert()`,
  which has both the source and the destination case.
- `primitive_builder.cpp`: the conversion dispatch, implemented by calling
  the same `erhe::dataformat::convert()` per vertex, so the two paths share
  one reference encoder.
- Test: extend `test_optimized_variant_build.cpp` to assert the optimized
  stride and the decoded color round-trip within one unorm8 LSB.

### Step 3 -- `texcoord_2` to `16_vec2_unorm`

Lightmap UVs are in [0, 1] by construction, so no affine, no side data, no
shader change (`vec2` both ways). Same two touch points as step 2; the
converter already handles it.

Watch for: the lightmap path composes `primitive.lightmap_scale_offset`
with `a_texcoord_2` -- that composition is unchanged, but it is the reason
this UV set must *not* pick up an affine of its own.

### Step 4 -- `texcoord_0` / `texcoord_1` to `16_vec2_unorm` + UV affine

First new shader axis and first new per-primitive record data.

- `buffer_mesh.hpp`: a per-UV-set bounds pair on `Buffer_mesh`, computed
  during the build the way `bounding_box` is, so the record writer can
  reproduce the exact affine the encoder used. Both paths must compute it
  before any vertex is converted (the soup path already orders the
  bounding-volume pass first for exactly this reason).
- `primitive_buffer.hpp/.cpp`: `Uv_quantization` beside
  `Position_quantization`, `texcoord_scale` / `texcoord_offset` fields on
  `Primitive_struct`, written in the three record writers that already
  write `position_scale` / `position_offset` (`primitive_buffer.cpp` twice,
  `draw_list_scene.cpp` once, plus the identity default site).
- `vertex_format.hpp/.cpp`: `Vertex_texcoord_encoding::{passthrough,
  unorm16x2_affine}` and its getter.
- `shader_stages_create_info.cpp`: emit `ERHE_VERTEX_TEXCOORD_ENCODING`
  beside the position define, from both `attributes_source()` and
  `attribute_defines_source()`.
- `shader_key.hpp/.cpp`: a `TEXCOORD_ENCODING` axis, set in `derive()`,
  omitted from `get_defines()` like the position one.
- `res/shaders/erhe_vertex_texcoord.glsl`: the decode plus the `#ifndef`
  backstop; `standard.vert` calls it where it assigns `v_texcoord_0` /
  `v_texcoord_1`.

Watch for: tiling UVs. The affine is what makes out-of-[0,1] UVs
representable, so a primitive whose UV bounds are degenerate on an axis
must fall back to the same epsilon rule the position affine uses.

### Step 5 -- TBN quaternion

The largest step. Normal and tangent leave the format, one `16_vec4_sint`
attribute replaces them.

- Encode in both paths (via erhe's own encoder -- see the TBN section for
  why `meshopt_encodeFilterQuat` could not be used): Gram-Schmidt the
  tangent against the normal, build the frame, encode, and carry the
  bitangent sign. **Settle the sign carrier first** -- negating the
  quaternion is the conventional home for it, and whether that survives
  meshopt's drop-the-largest-component layout has to be checked against the
  filter before the rest of the step is written. The fallback is our own
  largest-component-dropped encoding in the same 8 bytes.
- `16_vec4_sint` (not snorm) so the 2-bit component index stays exactly
  readable in GLSL as an `ivec4`.
- `res/shaders/erhe_vertex_tbn.glsl`: a GLSL port of
  `meshopt_decodeFilterQuat` returning normal, tangent and sign;
  `standard.vert` calls it where it currently reads `a_normal` and
  `a_tangent`, including the `v_tangent_scale` assignment.
- `shader_key.cpp`: `derive()`'s `has_normal_0` / `has_tangent` presence
  booleans must report true for the quaternion attribute, or the optimized
  variant silently loses its tangent-space shader features. This is the
  single highest-risk line in the whole plan.
- Test: a box round-trip asserting the decoded frame is within tolerance of
  the source normal and tangent, and that handedness survives on a mesh
  with mirrored UVs.

### Step 6 -- implicit-sum joint weights

Skinned meshes only, and stride-neutral (stream 0 stays 16 bytes).

- Encode: sort the four influences so the smallest weight is last,
  permuting `joint_indices` in lockstep, quantize the first three to
  `16_vec3_unorm` such that the implied fourth is exact.
- `vertex_format.hpp/.cpp`: `Vertex_joint_weights_encoding::{passthrough,
  unorm16x3_implicit_sum}`.
- `res/shaders/erhe_vertex_joint_weights.glsl`: reconstruct
  `w3 = max(0, 1 - (w0 + w1 + w2))` and hand a `vec4` to
  `erhe_skin_matrices()`, which is already parameterized on a `vec4` and
  needs no change.
- Depends on weights arriving normalized, which is the import-side
  sanitization this plan already assigns upstream.

### Final pass (once, after step 6)

- Unit tests: `build_tests` (non-asan), `ERHE_MCP_TEST_TIMEOUT_S=1`. Note
  `ctest` at the `build_tests` root aborts on the
  `erhe_graphics_gpu_tests` discovery include unless that target was built
  -- build it explicitly or run ctest per test directory.
- Build sweep: VS 2026 Vulkan Debug and VS 2026 OpenGL Debug, both clean.
  The OpenGL one matters on its own: this work touches vertex formats and
  GLSL, and the Vulkan build cannot speak for that backend. **Quest is
  future work** (see below), so no APK build here.
- Headless interaction sanity, optimize on and off: `pick_at` probes hit
  the same node / mesh / facet id, glTF export round-trips to the same
  element counts. These read the original variant, so they must be
  **unchanged**, which is the check that no encoding leaked out of the
  optimized variant.
- Byte check via MCP `get_mesh_buffer_info` / `get_mesh_buffer_data`: base
  format unchanged at stride 104, optimized at stride 36 with the expected
  per-attribute formats.
- A/B captures into `logs/attr_encoding_ab/` on ABeautifulGame, one
  off/on pair per step plus the all-on pair, each against a same-config
  control pair, with the mutation check. Then hand the folder to the user
  for visual inspection -- that is the acceptance criterion, not a pixel
  threshold.

Not affected, deliberately: the soup path's disk cache. Its key hashes the
**source** soup, its format and the optimize options; the sink format is
not part of it, and an entry stores only the remap pair. No format-version
bump.

## Future work

- **Quest.** Not built and not tested there for this work, deliberately.
  Two things to settle when it happens: whether every substituted format is
  accepted as vertex input on the device (`format_16_vec4_sint` and
  `format_16_vec3_unorm` are the new ones; the position quantization
  already has a `Device_info` gate and a declined-request warning to model
  a fallback on), and whether the -62% vertex fetch actually shows up in
  frame time on tiler hardware, which is where it should matter most. Note
  the config is APK-bundled, so a config change needs an uninstall and a
  clean reinstall, and every OpenXR launch needs a fresh prompt and
  explicit confirmation.
- YCoCg vertex colors via `meshopt_encodeFilterColor` /
  `meshopt_decodeFilterColor` -- quality at the same 4 bytes.
- `format_packed1010102_vec4_snorm` is declared but `convert()` handles it
  neither as source nor destination. It is the classic 4-byte normal
  format; if a future encoding wants it, that gap is the work.
- Stream 2 of the optimized format is left holding `aniso_control` alone,
  2 bytes padded to a 4-byte stride. Folding it into stream 1 would remove
  a whole pool and its allocation, but the optimized and content formats
  would then differ in stream *count*, which
  `take_optimizable_snapshot()` currently rejects outright.
- 8-bit variants (`8_vec4_snorm` tangents, `8_vec3_snorm` normals) as a
  low-end profile, if Quest ever wants to trade precision for another few
  bytes.
