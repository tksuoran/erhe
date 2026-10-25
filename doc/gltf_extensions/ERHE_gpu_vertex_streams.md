# ERHE_gpu_vertex_streams

Stability: experimental

## Contributors / status

erhe project. Draft vendor extension. The plan that introduces it is
[`../plans/gltf_fast_load.md`](../plans/gltf_fast_load.md) ("Item 9:
streaming load"): "Save for Quest" writes it (step 8) and the loader uses it
(step 9).

## Dependencies

Written against glTF 2.0. Optional (`extensionsUsed` only, never
`extensionsRequired`). Requires [`ERHE_mesh_optimized`](ERHE_mesh_optimized.md)
on the same primitive; without it, or when it is rejected, this extension is
ignored.

## Scope

Mesh **primitive** extension.

## Overview

Stores a primitive's vertex streams in erhe's GPU vertex layout, so a loader
whose running layout matches uploads them with at most a memcpy and no
conversion. Two variants are described:

- `base`: the full-precision layout. Its stored streams are the core
  primitive's own bufferViews (interleaved, `byteStride` set), and the core
  accessors over them use only glTF 2.0 core types, so stock viewers render
  the primitive unchanged.
- `optimized`: the compact layout (quantized position, tangent frame
  quaternion, unorm16 texcoords, unorm8 color, implicit-sum joint weights).
  Its accessors are referenced only from this extension.

Unlike `KHR_mesh_quantization`, which bakes the position dequantization into
node transforms, the per-attribute scale and offset are stored explicitly
here; erhe hands them to the shaders through its per-draw primitive record
(`Buffer_mesh::position_quantization` / `texcoord_quantization`,
`doc/erhe/vertex_position_quantization.md` section 3).

## Core primitive

A primitive carrying the extension satisfies, in addition to the
`ERHE_mesh_optimized` core contract:

1. `indices` is a SCALAR UNSIGNED_INT accessor. Both variants draw with it
   (the vertex sets are identical: the element mappings are the identity).
2. The `base` variant's stored streams are the bufferViews the core
   attribute accessors read; every core attribute accessor lies in one of
   them, at the offset the signature names for its attribute.
3. Attributes of the base layout the primitive does not carry (for example
   TEXCOORD_1 of a primitive with one UV set) are bytes inside the stride
   that no core accessor names; they hold what erhe's soup build writes for
   a missing attribute (zero, white for COLOR_0).
4. The core `WEIGHTS_0`, when present, is UNSIGNED_BYTE normalized (the base
   layout stores unorm8 weights); this amends `ERHE_mesh_optimized` contract
   item 2 for primitives carrying this extension.

## Payload

- Each variant lists its streams in binding order. A stream is either
  `{"bufferView": n}` - the view holds vertex count * stride bytes, its
  `byteStride` is the stride - or `{"fill": "zero", "byteStride": s}`: a
  stream every vertex of which is zero, stored nowhere (erhe's soup build
  writes zeros for the smooth normal, facet id, valency and anisotropy
  attributes).
- The `optimized` variant names an accessor for each attribute it stores, in
  `attributes`, keyed by attribute name (below), with `scale` / `offset` for
  the affine-encoded ones. These accessors are valid glTF accessors over the
  variant's bufferViews but are not core attributes of any primitive.

### Attribute names and encodings

| Name | Variant | Accessor | Meaning |
|---|---|---|---|
| `POSITION` | optimized | VEC3 SHORT normalized | decoded = stored * `scale` + `offset`; `scale` = max(0.5 * (max - min), 1e-6) and `offset` = 0.5 * (max + min) of the POSITION bounds, per axis |
| `_TBN_QUATERNION` | optimized | VEC4 SHORT (not normalized) | normal + tangent + handedness as one quaternion: lanes 0..2 the non-largest components times sqrt(2) as snorm16, lane 3 bits 0..1 the omitted component, bit 2 the handedness (`erhe::primitive::encode_tbn_quaternion()`, decoded by `res/shaders/erhe_vertex_tbn.glsl`) |
| `TEXCOORD_0`, `TEXCOORD_1` | optimized | VEC2 UNSIGNED_SHORT normalized | decoded = stored * `scale` + `offset`; `scale` = max(max - min, 1e-6) and `offset` = min of the channel's bounds |
| `TEXCOORD_2` | optimized | VEC2 UNSIGNED_SHORT normalized | lightmap UVs in [0, 1], no affine |
| `COLOR_0` | optimized | VEC4 UNSIGNED_BYTE normalized | as glTF |
| `JOINTS_0` | optimized (skinned) | VEC4 UNSIGNED_BYTE | permuted so that the smallest weight is last |
| `_WEIGHTS_IMPLICIT` | optimized (skinned) | VEC3 UNSIGNED_SHORT normalized | the three largest weights; the fourth is max(0, 1 - sum) |

The `base` variant names no attributes: its attributes are the core ones.
`scale` and `offset` hold float values written with float round-trip
precision (9 significant digits) and are read back as the same float.

### Signature

`signature` is the canonical text of the variant's vertex layout
(`erhe::dataformat::get_vertex_format_signature()`): per stream in binding
order `s<i>=<stride>:` followed by its attributes in declaration order,
`<usage>.<index>=<format>@<offset>` separated by `,`; streams separated by
`|`. `<usage>` is the erhe attribute usage name (`position`, `normal`,
`tangent`, `tex_coord`, `color`, `joint_indices`, `joint_weights`, `custom`),
`<format>` the erhe format name without its `format_` prefix. The layouts
erhe derives today (logged at startup as `Vertex format signatures`):

- base: `s0=12:position.0=32_vec3_float@0|s1=68:normal.0=32_vec3_float@0,tangent.0=32_vec4_float@12,tex_coord.0=32_vec2_float@28,tex_coord.1=32_vec2_float@36,tex_coord.2=32_vec2_float@44,color.0=32_vec4_float@52|s2=24:normal.1=32_vec3_float@0,custom.1=8_vec2_unorm@12,custom.2=16_vec2_uint@14,custom.0=8_vec4_unorm@18`
- base, skinned: stream 0 is `s0=20:position.0=32_vec3_float@0,joint_indices.0=8_vec4_uint@12,joint_weights.0=8_vec4_unorm@16`
- optimized: `s0=8:position.0=16_vec3_snorm@0|s1=24:tangent.0=16_vec4_sint@0,tex_coord.0=16_vec2_unorm@8,tex_coord.1=16_vec2_unorm@12,tex_coord.2=16_vec2_unorm@16,color.0=8_vec4_unorm@20|s2=4:custom.1=8_vec2_unorm@0`
- optimized, skinned: stream 0 is `s0=16:position.0=16_vec3_snorm@0,joint_indices.0=8_vec4_uint@6,joint_weights.0=16_vec3_unorm@10`

The layout depends on the device (position quantization off, no unorm16x3
vertex input, attribute alignment); the signature captures that as layout.

## JSON layout

```json
{
    "version": 1,
    "encoding_version": 1,
    "variants": [
        {
            "role": "base",
            "signature": "s0=12:position.0=32_vec3_float@0|s1=68:...|s2=24:...",
            "streams": [{"bufferView": 40}, {"bufferView": 41}, {"fill": "zero", "byteStride": 24}]
        },
        {
            "role": "optimized",
            "signature": "s0=8:position.0=16_vec3_snorm@0|s1=24:...|s2=4:custom.1=8_vec2_unorm@0",
            "streams": [{"bufferView": 42}, {"bufferView": 43}, {"fill": "zero", "byteStride": 4}],
            "attributes": {
                "POSITION":        {"accessor": 90, "scale": [4.1, 2.0, 7.3], "offset": [0.5, 2.0, -1.0]},
                "_TBN_QUATERNION": {"accessor": 91},
                "TEXCOORD_0":      {"accessor": 92, "scale": [3.0, 3.0], "offset": [-1.0, 0.0]},
                "TEXCOORD_1":      {"accessor": 93, "scale": [1.0, 1.0], "offset": [0.0, 0.0]},
                "TEXCOORD_2":      {"accessor": 94},
                "COLOR_0":         {"accessor": 95}
            }
        }
    ]
}
```

- `version`: integer, `1`. A loader rejects any other value.
- `encoding_version`: integer; the version of erhe's attribute encoders
  (`erhe::primitive::vertex_encoding_version`), bumped when an encoder writes
  different bits for the same layout.
- `variants`: one `base` and at most one `optimized` entry.
- `signature`: see "Signature".

## Validation / fallback

Per variant, the stored streams are used only when all hold: `version` is 1;
`signature` equals the running layout's signature for that variant;
`encoding_version` equals the running encoders'; every stored stream's
bufferView is `byteLength` = vertex count * stride with `byteStride` = stride;
every zero-filled stream's `byteStride` equals the running stride. Otherwise
the loader builds that variant from the core accessors, as for a primitive
without the extension. For every primitive the loader checks that all
indices are below the vertex count (GPU safety) and the `ERHE_mesh_optimized`
content hash; stream bytes are otherwise trusted.

## Schema

[`schema/ERHE_gpu_vertex_streams.schema.json`](schema/ERHE_gpu_vertex_streams.schema.json)
