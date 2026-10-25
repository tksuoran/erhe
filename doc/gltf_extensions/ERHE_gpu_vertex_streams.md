# ERHE_gpu_vertex_streams

Stability: experimental

## Contributors / status

erhe project. Draft vendor extension, design only: nothing reads or writes
it yet. The plan that introduces it is
[`../plans/gltf_fast_load.md`](../plans/gltf_fast_load.md) ("Item 9:
streaming load"); this page is completed (schema, exact signature grammar,
encoder references) in that plan's build order step 7.

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

- `base`: the full-precision layout. Its non-zero streams are the core
  primitive's own bufferViews (interleaved, `byteStride` set), and the core
  accessors over them use only glTF 2.0 core types, so stock viewers render
  the primitive unchanged.
- `optimized`: the compact layout (quantized position, tangent frame
  quaternion, unorm16 texcoords, unorm8 color, implicit-sum weights). Its
  accessors are referenced only from this extension.

Unlike `KHR_mesh_quantization`, which bakes the dequantization into node
transforms, the per-attribute scale and offset are stored explicitly here and
reach the shader through erhe's per-draw primitive record.

## Payload

- The core `indices` accessor is SCALAR UNSIGNED_INT and serves both
  variants (the vertex sets are identical: element mappings are the
  identity, see `ERHE_mesh_optimized`).
- Each variant lists its streams in binding order. A stream is either a
  bufferView (`byteLength` = vertex count * `byteStride`) or zero-filled
  (`"fill": "zero"` with a `byteStride`, no bytes in the file).
- Custom attribute names start with an underscore: `_TBN_QUATERNION`
  (VEC4 SHORT, erhe's largest-component-dropped quaternion with the omitted
  component index and handedness in the w lane), `_WEIGHTS_IMPLICIT`
  (VEC3 UNSIGNED_SHORT normalized; fourth weight = 1 - sum, joints permuted
  smallest weight last).
- The attribute-by-attribute table is in the plan section "Quest file: GPU
  ready vertex streams".

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
            "signature": "s0=8:position.0=16_vec3_snorm@0|s1=24:...|s2=4:...",
            "streams": [{"bufferView": 42}, {"bufferView": 43}, {"fill": "zero", "byteStride": 4}],
            "attributes": {
                "POSITION":        {"accessor": 90, "scale": [4.1, 2.0, 7.3], "offset": [0.5, 2.0, -1.0]},
                "_TBN_QUATERNION": {"accessor": 91},
                "TEXCOORD_0":      {"accessor": 92, "scale": [3.0, 3.0], "offset": [-1.0, 0.0]},
                "COLOR_0":         {"accessor": 94}
            }
        }
    ]
}
```

- `version`: integer, `1`. A loader rejects any other value.
- `encoding_version`: integer; the version of erhe's attribute encoders
  (bumped when an encoder changes the bits it writes for the same layout).
- `signature`: canonical description of the variant's vertex layout (per
  stream the stride, per attribute usage, index, format and offset). A loader
  uses the stored streams only when it equals the running layout's
  signature.
- `scale` / `offset`: decoded value = stored (normalized) value * `scale` +
  `offset`, per component; written with float round-trip precision.

## Validation / fallback

Per variant: unknown `version`, a different `signature` or
`encoding_version`, a stream length that is not count * stride, or a
zero-fill stride different from the running one makes the loader build that
variant from the core accessors instead. For every primitive the loader
checks that all indices are below the vertex count and the
`ERHE_mesh_optimized` content hash. Stream bytes are otherwise trusted.

## Schema

To be written (plan build order step 7).
