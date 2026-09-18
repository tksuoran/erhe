# Compact attribute encodings for the optimized mesh variant

Stability: mostly stable

Which encoding each non-position vertex attribute of the optimized mesh
variant uses, what meshoptimizer gives us, and what each choice costs. Read
`doc/meshoptimizer_integration.md` first; this builds directly on the encoding
split it describes (requirements 9-11). Position quantization has its own
document, `doc/vertex_position_quantization.md`.

## Ground rules

- The **base** (`render_shape`) variant keeps full-precision float attributes.
  Every encoding below applies **only** to the optimized variant
  (`optimized_render_shape`), and only when `optimize_meshes` is on.
- The optimized variant is **fill-only**, welded, and carries no facet id.
  Edge lines, corner and centroid points and the expanded solid-wireframe copy
  are the base variant's, untouched.
- Picking / ID rendering, CPU raytrace, BLAS sources, physics collision, glTF
  export and geometry reconstruction all read the **original** shape. They are
  unreachable from the optimized format by construction, so no encoding here
  can reach them - the same argument that makes position quantization safe
  (`get_blas_position_input()` returns early on a passthrough format; the
  ray-tracing instance records derive their encoding from the original
  variant).
- In-place GPU vertex edits (paint, weight paint, live drag) address the base
  variant, so encoded attributes are never written through.
- **The half float formats are banned.** `16_vec2_float` and its siblings are
  not used for vertex attributes here.

## Where the format lives

`Mesh_memory` derives `vertex_format_{not_,}skinned_optimized` from the
content formats in its constructor by walking a per-attribute substitution
table: drop, keep, or substitute a format. That derivation is the single place
a per-attribute rule belongs, and `get_all_vertex_formats()` is the single
list that repack, lockstep block sizing and vertex-input registration read.

Non-skinned per-vertex cost, quantized position, after repack (stride
alignment 4):

| stream | attributes | bytes |
|---|---|---|
| 0 | position `16_vec3_snorm` (6) | 8 |
| 1 | TBN quat 8, texcoord 0/1/2 4+4+4, color_0 4 | 24 |
| 2 | aniso_control 2 | 4 |
| | **total** | **36** |

The base variant is 104 bytes (float3 position and the 4-byte facet id), and
the same content attributes stored as floats in the optimized format would be
96, so the encodings below are **96 -> 36 bytes per vertex (-62 %)**, or -65 %
against the base variant, on top of the vertex-fetch win the reordering
already delivers.

Skinned, stream 0 is position 6 + `joint_indices` 4 + `joint_weights` 6 = 16,
which is the stride it has with 8-bit weights as well - the 16-bit
implicit-sum weights are free in stride terms, so they buy precision and exact
normalization at no memory cost.

## Three independent axes, only one of which keys on component count

An encoding choice is judged on three separate things, and they do not move
together:

1. **Memory** - bytes per vertex after the stride and attribute alignment the
   repack imposes. Purely a function of the stored bit width and the resulting
   padding: a 6-byte attribute in a stream that pads to 4 can cost 8.
2. **Precision** - what the encoding does to the value, independent of both of
   the others. A 4-byte octahedral normal is *more* accurate than a 4-byte
   `8_vec4_snorm` one, and a 6-byte `16_vec3_snorm` normal is accurate enough
   to be indistinguishable from float while still not being the cheapest
   option.
3. **Shader-side cost** - and this is the one that keys on component count.
   `Shader_stages_create_info::attributes_source()` declares each attribute
   with `to_glsl_attribute_type(format)`, and the vertex-fetch hardware
   normalizes snorm / unorm for free. So a change that preserves the declared
   GLSL type (`32_vec3_float` -> `16_vec3_snorm`, `32_vec4_float` ->
   `8_vec4_unorm`) needs no shader work at all, while a change of component
   count or of meaning (octahedral, quaternion, implicit-sum, an affine range)
   needs a decode helper and a format-derived `ERHE_VERTEX_<attribute>_ENCODING`
   axis, the way `ERHE_VERTEX_POSITION_ENCODING` /
   `res/shaders/erhe_vertex_position.glsl` work for positions.

The encodings below spend shader-side cost where it buys the most memory and
precision, rather than taking the free-but-weaker option.

## The per-attribute table

| attribute | content format | optimized format | bytes | decode axis |
|---|---|---|---|---|
| normal + tangent + handedness | `32_vec3_float` 12 + `32_vec4_float` 16 = 28 | one **TBN quaternion**, `16_vec4_sint` | 8 | `ERHE_VERTEX_TBN_ENCODING` |
| texcoord_0 | `32_vec2_float` 8 | `16_vec2_unorm` + per-primitive UV affine | 4 | `ERHE_VERTEX_TEXCOORD_ENCODING` |
| texcoord_1 | `32_vec2_float` 8 | `16_vec2_unorm` + per-primitive UV affine | 4 | same axis |
| texcoord_2 (lightmap) | `32_vec2_float` 8 | `16_vec2_unorm`, no affine | 4 | none |
| color_0 | `32_vec4_float` 16 | `8_vec4_unorm` | 4 | none - `vec4` either way |
| joint_weights | `8_vec4_unorm` 4 | `16_vec3_unorm` **implicit-sum** | 6 | `ERHE_VERTEX_JOINT_WEIGHTS_ENCODING` |
| joint_indices | `8_vec4_uint` 4 | unchanged, but permuted | 4 | none |
| aniso_control | `8_vec2_unorm` 2 | unchanged | 2 | none |
| normal_smooth | `32_vec3_float` 12 | **dropped** | 0 | none |
| valency_edge_count | `16_vec2_uint` 4 | **dropped** | 0 | none |

### TBN quaternion

Normal, tangent and handedness are one orthonormal frame, so one frame is
stored: a unit quaternion in 8 bytes, largest component dropped and its index
recorded in the control lane, the shape `meshopt_encodeFilterQuat` uses.

Two things this forces:

- The attribute is declared **`16_vec4_sint`** (`ivec4` in GLSL), not
  `16_vec4_snorm`. The component index lives in the low bits of a lane, and
  reading through the hardware snorm conversion would mean recovering it by
  multiplying back by 32767 and rounding. Exact integer access is cleaner, and
  the decode is doing arithmetic anyway.
- **Handedness needs an explicit carrier, and `meshopt_encodeFilterQuat`
  cannot carry it.** `q` and `-q` are the same rotation, so the quaternion's
  sign is the conventional place for the bitangent sign - but that encoder
  canonicalizes on the largest component and *discards* the sign, and its
  decode recovers its scale from the `w` lane with `| 3`, so the spare bits
  there are not spare either. erhe therefore uses its own
  largest-component-dropped encoding in the same 8 bytes
  (`encode_tbn_quaternion()` in `erhe_primitive/mesh_optimizer.cpp`),
  structurally identical, with the control lane holding the omitted component
  index in bits 0..1 and the handedness in bit 2.
  `res/shaders/erhe_vertex_tbn.glsl` is the matching decode; the two are read
  together.

erhe's tangent is not guaranteed orthogonal to the normal, so the encode
Gram-Schmidts first. That is a real (small) precision change, distinct from
the quantization itself.

### Texcoord affine

`16_vec2_unorm` alone cannot represent tiling UVs outside [0, 1], so
texcoord_0 and texcoord_1 carry a **per-primitive UV scale / offset**,
mirroring what the position AABB affine does: same shape of side data on
`Buffer_mesh`, same per-primitive record fields (`texcoord_scale` /
`texcoord_offset` on `Primitive_struct`, written by the same record writers
that write `position_scale` / `position_offset`), same decode in the vertex
shader. A primitive whose UV bounds are degenerate on an axis falls back to
the same epsilon rule the position affine uses.

texcoord_2 is the lightmap UV set and is in [0, 1] by construction, so it
needs no affine and no side data. It must **not** pick up an affine of its
own: the lightmap path composes `primitive.lightmap_scale_offset` with
`a_texcoord_2`.

### Implicit-sum joint weights

Skinning weights sum to 1, so the fourth is redundant. Per vertex:

1. Sort the four (index, weight) pairs so the **smallest weight is last**,
   permuting `joint_indices` in lockstep.
2. Quantize the first three to `16_vec3_unorm`, choosing the roundings so the
   three stored values plus the implied fourth sum to exactly one unit.
3. The shader reconstructs `w3 = max(0, 1 - (w0 + w1 + w2))` and hands a
   `vec4` to `erhe_skin_matrices()`, which is already parameterized on a
   `vec4`.

The encoding is `erhe::dataformat::Vertex_joint_weights_encoding::unorm16x3_implicit_sum`,
axis `ERHE_VERTEX_JOINT_WEIGHTS_ENCODING`.

Dropping the smallest weight is what bounds the reconstruction error, and it
makes the stored order canonical - which incidentally helps the geometry-path
weld, since that is a bitwise compare and two corners with the same influences
in different author order do not otherwise merge. It depends on the weights
arriving normalized, which is an import-time responsibility.

### Dropped attributes

`normal_smooth` (`a_normal_1`) and `valency_edge_count` (`a_custom_2`) are
dropped outright - 16 of the 60 saved bytes, and the cheapest 16. No
`res/shaders/*` file reads `a_normal_1`; the smooth normal the wide-line
compute pass uses comes from the separate edge-line format.
`valency_edge_count` feeds a fragment debug path, which renders from the base
variant.

`Shader_key` needs no presence axis for either. It enumerates presence
booleans for normal_0, tangent, texcoords 0-2, color_0, aniso_control and the
joint pair, and reports normal and tangent presence from the TBN encoding; the
other two attributes have no presence axis at all, so dropping them cannot
collide two distinct shader variants onto one key.

## What meshoptimizer gives us, and what it does not

- `meshopt_encodeFilterQuat` / `meshopt_decodeFilterQuat` are **not usable**
  here, for the handedness reason above. The scheme is still the model erhe's
  own encoder follows.
- `meshopt_quantizeSnorm` / `meshopt_quantizeUnorm` drop straight in. They are
  redundant with `erhe::dataformat::convert()`, but routing the quantization
  through meshopt where the two agree is the precedent positions set, and it
  keeps the encode auditable against one reference.
- `meshopt_encodeFilterOct` is unused: the quaternion subsumes it for normal
  plus tangent.
- There is **no affine quantization helper** - the same gap positions hit. The
  UV affine is ours, modelled on the position one.
- `meshopt_quantizeHalf` is unusable: half formats are banned.

## Where each piece lives

- **Format derivation**: the substitution table in the `Mesh_memory`
  constructor, beside the facet-id drop and the position substitution.
- **Soup path**: `Primitive_shape::make_buffer_mesh()` routes every
  non-position attribute through `erhe::dataformat::convert()`, so the plain
  re-encodings (color, lightmap UV) come for free. The quaternion, the UV
  affine and the implicit-sum weights each have an encode branch beside the
  `encode_position` one.
- **Geometry path**: `Build_context::take_optimizable_snapshot()` converts per
  attribute during the gather. A genuinely unhandled source / sink pair makes
  it decline - `return false`, no optimized variant at all - so an unhandled
  combination is a missing variant, never wrong bytes. This is also where the
  encodings pay twice: the geometry-path weld is a bitwise compare over the
  **encoded** bytes, so quantized attributes merge strictly more corners than
  float ones. The soup path welds on source floats before the sink conversion,
  so it gets the memory win but not the extra weld; its disk cache stores only
  the remap pair keyed on the source soup and is unaffected either way (the
  sink format is not part of the cache key, so an encoding change needs no
  format-version bump).
- **Shader side**: one `ERHE_VERTEX_<attribute>_ENCODING` axis per encoding,
  each emitted from the vertex format the way the position one is (never from
  `Shader_key::get_defines()`), each with its own `erhe_vertex_<attribute>.glsl`
  decode and its own `#ifndef` backstop. Each axis is binary (passthrough /
  encoded). They do not multiply the prewarm set: the axes are derived from
  the vertex format, and the prewarm walks already enumerate the base and the
  optimized format side by side (`Shadow_renderer::prewarm_pipelines()`
  explicitly, `Forward_renderer::prewarm_standard_variants()` by bucketing
  real meshes with `Mesh_variant::optimized`).
- **Import sanitization**: normals and tangents must arrive finite and within
  [-1, 1], and joint weights normalized. `convert()`'s `16_vec3_snorm`
  destination **asserts** on out-of-range input rather than clamping, and that
  assert stays - the fix for a bad asset belongs upstream, at import, not in a
  silent clamp in the encoder.

## Verification

Rendering identity is not the criterion: every encoding here is lossy by
design, so the expected result is a small non-zero difference, the same way
the position quantization epsilon is expected and is itself the proof that the
optimized variant is being rendered. **The acceptance criterion is user visual
inspection.**

`scripts/attr_encoding_ab.py` produces the image set, on **ABeautifulGame
only**. It builds on the harness in `doc/meshoptimizer_integration.md` ("How
to verify"): MCP port 3743, DDGI off, paused time, pinned layout, control pair
first. Four editor launches, into `logs/attr_encoding_ab/`:

| capture | `optimize_meshes` | `quantize_vertex_positions` |
|---|---|---|
| `off_a`, `off_b` | false | false |
| `on_qoff` | true | false |
| `on_qon` | true | true |

There is only one `off` side, because `quantize_vertex_positions` affects the
optimized variant alone - which is what makes the `on_qoff` / `on_qon` split
separate the attribute encodings from the position epsilon. Each comparison
gets a side-by-side PNG, a difference PNG amplified 8x so a 1-LSB difference
is visible, and a percentage over the cropped viewport.

Expected magnitudes (differing pixels of the cropped viewport, VS 2026 Vulkan
Debug):

| comparison | differing | worst | within 4 LSB |
|---|---|---|---|
| control `off_a` vs `off_b` | **0.000%** | 0 | -- |
| attributes `off_a` vs `on_qoff` | 1.085% | 17 LSB | 99.9% |
| all `off_a` vs `on_qon` | 1.492% | 107 LSB | 97.1% |
| positions `on_qoff` vs `on_qon` | 1.037% | 107 LSB | 95.9% |

So the attribute encodings together move about 1 % of viewport pixels, 99.9 %
of the differing channel samples by 4 LSB or less, on top of a control pair
that is exactly identical. Side by side the two renders are indistinguishable
by eye.

Two harness rules, both learned the hard way:

- **A non-zero control row invalidates the whole batch.** Two identical runs
  must render identically; a batch whose control row is non-zero (for example
  a shot taken before the scene had settled) is measuring that noise in every
  other row too. The script says so in its summary. Re-run.
- **A passing control pair is necessary and not sufficient** - it cannot tell
  you the optimized variant was built or selected at all. The script therefore
  also dumps the per-variant GPU strides and attribute formats, and that dump
  is what catches a substitution that silently did not apply.

Byte-level checks go through the MCP `get_mesh_buffer_info` /
`get_mesh_buffer_data` tools, which read the actual GPU bytes and name which
build they read: the base format stays at stride 104, the optimized one at
stride 36 with the per-attribute formats above.
`get_mesh_attribute_values` reads SOURCE geometry, not GPU bytes.

Headless interaction sanity, optimize on and off: `pick_at` probes hit the
same node / mesh / facet id, and glTF export round-trips to the same element
counts. Those read the original variant, so they must be **unchanged**, which
is the check that no encoding leaked out of the optimized variant.

### Unit tests

`src/erhe/primitive/test/test_attribute_encodings.cpp`, run via the
`erhe_primitive_tests` target. Each encoding is an encoder / decoder pair that
must agree exactly and whose mismatch renders subtly wrong rather than
failing, so the decoders in that file are **line-by-line C++ mirrors of the
GLSL**: a change made to one side and not the other fails there instead of
becoming a puzzling image.

Covered: the TBN quaternion round trip across orientations reaching all four
omitted-component cases (handedness compared exactly - it is a bit, and
mirrored UV shells depend on it), degenerate TBN input yielding a finite
orthonormal frame rather than NaN, the joint influence sort (descending,
stable, indices permuted along, normalizing and rejecting negative weights),
the implicit-sum weight round trip and its never-sum-past-one invariant, the
texcoord affine (identity without a range, round trip for tiling UVs), the
three encoding axes derived from the format including the texcoord axis being
keyed on channel 0, and the geometry path building the whole encoded variant
end to end - where a missing conversion surfaces as a null
`optimized_render_shape`.

Note that `ctest` at the `build_tests` root aborts on the
`erhe_graphics_gpu_tests` discovery include when that target has not been
built (it is not in the default target): build it explicitly, or run ctest per
test directory.

## Future work

- [Mesh optimization](plans/meshoptimizer.md) - Quest verification, further
  encoding candidates, the stream-2 fold.
