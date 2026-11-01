# ERHE_mesh_lod

Stability: experimental

## Contributors / status

erhe project. Draft vendor extension. The plan that introduces it is
[`../plans/lod.md`](../plans/lod.md) (step 6). The writer is
`erhe::gltf::export_gltf()` with prepared mesh data, the reader the
prepared mesh data validation of `erhe::gltf::parse_gltf()`
(`doc/erhe/gltf.md`, `doc/erhe/meshoptimizer_integration.md` "Level of
detail").

## Dependencies

Written against glTF 2.0. Optional (`extensionsUsed` only, never
`extensionsRequired`). Requires [`ERHE_mesh_optimized`](ERHE_mesh_optimized.md)
on the same primitive; without it, or when it is rejected, this extension is
ignored.

## Scope

Mesh **primitive** extension.

## Overview

Stores a level of detail chain of the primitive: index lists that draw the
primitive with fewer triangles, over the core vertices. Level 0 is the core
primitive itself (its `indices` accessor); the extension carries the levels
after it. A level never has vertices of its own, so a loader that keeps the
core vertices in one buffer stores the levels as more index ranges beside
level 0 and switches level by changing only the drawn index range.

erhe writes the extension only for a primitive whose chain was requested in
the editor (a `Lod_settings` request on the primitive); imports and
primitives without a request carry none. Each level is the
`meshopt_simplifyWithAttributes()` result of the level before it (or, with
`fallback` `sloppy`, a `meshopt_simplifySloppy()` result where the regular
step misses its target), made with the stored settings; a loader uses the
levels as they are and runs no simplification.

## Payload

- `indices`: the index of a raw bufferView (no `byteStride`, no `target`)
  holding the levels after level 0 back to back, each index an unsigned
  32-bit little-endian integer into the core vertices (the vertices of the
  core `POSITION` accessor). Its `byteLength` is 4 times the sum of the
  levels' `index_count`.
- `levels`: one entry per level after level 0, in order, at most 7 (erhe's
  `max_lod_levels` is 8, level 0 included):
  - `index_count`: the level's index count, a positive multiple of 3. Level
    `i` (1-based in `levels`) starts where level `i - 1` ends.
  - `error`: the level's object-space error (meshoptimizer's relative error
    times `meshopt_simplifyScale()`), a C99 hexadecimal floating-point literal
    (`%a`), so the loader reads back the exact bits. Strictly increasing over
    the levels and greater than 0 (level 0's error is 0).
- `settings`: the `erhe::primitive::Lod_settings` the chain was made with,
  so a later request with the same settings reuses the stored chain:
  `level_count` (levels including level 0, 2..8), `ratio`, `normal_weight`,
  `color_weight`, `normal_merge_angle` (degrees) as hexadecimal float
  strings, and `attributes` (`positions_only` | `normals_and_colors`),
  `border` (`free` | `locked`), `seams` (`keep` | `permissive`), `fallback`
  (`none` | `sloppy`).

## JSON layout

```json
{
    "version": 1,
    "indices": 57,
    "levels": [
        {"index_count": 6144, "error": "0x1.295a6p-7"},
        {"index_count": 3072, "error": "0x1.fdd5aep-7"},
        {"index_count": 1536, "error": "0x1.1e8ecp-5"}
    ],
    "settings": {
        "level_count": 6, "ratio": "0x1p-1", "attributes": "normals_and_colors",
        "normal_weight": "0x1p+0", "color_weight": "0x1p+0", "border": "locked",
        "seams": "keep", "fallback": "none", "normal_merge_angle": "0x1.4p+4"
    }
}
```

- `version`: integer, `1`. A loader rejects any other value.

## Validation / fallback

The loader rejects the extension, and loads the primitive without a chain
(the rest of its prepared mesh data stays in use), when any of these fails:

- `version` is 1; `settings` parses (every member present, known enum
  names, hexadecimal floats that are exact floats); `level_count` is 2..8,
  `ratio` in (0, 1), `normal_weight` and `color_weight` finite and >= 0,
  `normal_merge_angle` in [0, 180] (the ranges an on-request generation
  accepts);
- `levels` has 1..7 entries and fewer than `level_count`; every
  `index_count` is a positive multiple of 3; every `error` parses, is
  finite, greater than 0 and greater than the previous level's;
- the `indices` bufferView exists, has no `byteStride` and holds exactly
  the levels' indices;
- every index is below the core vertex count.

A rejected chain never rejects the rest of the prepared mesh data: on the
stored vertex streams path (`ERHE_gpu_vertex_streams`) the streams load
without the chain as well. The loader counts the accepted extensions (`LOD chains N` in the
`prepared mesh data` log line of a load).

## Schema

[`schema/ERHE_mesh_lod.schema.json`](schema/ERHE_mesh_lod.schema.json)
