# ERHE_mesh_lod

Stability: experimental

## Contributors / status

erhe project. Draft vendor extension. The plan that introduces it is
[`../plans/lod.md`](../plans/lod.md) (step 6). The writer is
`erhe::gltf::export_gltf()`, the reader `erhe::gltf::parse_gltf()`
(`../erhe/gltf.md` "Authoring data").

## Dependencies

Written against glTF 2.0. Optional (`extensionsUsed` only, never
`extensionsRequired`). No other extension is required.

## Scope

Mesh **primitive** extension.

## Overview

The primitive's level of detail request: the `erhe::primitive::Lod_settings`
with which erhe generates the primitive's LOD chain (index lists that draw it
with fewer triangles over its own vertices; `../erhe/meshoptimizer_integration.md`
"Level of detail"). The request is authoring data of the primitive: erhe's
Save Scene, which writes raw soups (`../editor/scene_serialization.md` "File
anatomy"), writes it for a primitive whose chain was requested in the editor;
imports and primitives without a request carry none. The chain itself is
derived data, held in the editor asset's Local-space mesh item and the
prepared asset (`../editor/prepared_assets.md`, `../erhe/gltf.md` "LOD
chains"); a glTF file carries none.

## JSON layout

```json
{"version": 1, "settings": {"level_count": 6, "ratio": "0x1p-1", "attributes": "normals_and_colors",
 "normal_weight": "0x1p+0", "color_weight": "0x1p+0", "border": "locked",
 "seams": "keep", "fallback": "none", "normal_merge_angle": "0x1.4p+4"}}
```

- `version`: integer, `1`. A loader rejects any other value.
- `settings`: `level_count` (levels including level 0, 2..8), `ratio`,
  `normal_weight`, `color_weight`, `normal_merge_angle` (degrees) as C99
  hexadecimal float strings (`%a`, exact bits), and `attributes`
  (`positions_only` | `normals_and_colors`), `border` (`free` | `locked`),
  `seams` (`keep` | `permissive`), `fallback` (`none` | `sloppy`).

## Validation / fallback

The loader reads the `settings` as the request
(`erhe::primitive::Primitive::lod_settings`) when `version` is 1 and the
settings parse (every member present, known enum names, hexadecimal floats
that are exact floats) and lie in the ranges an on-request generation
accepts: `level_count` 2..8, `ratio` in (0, 1), `normal_weight` and
`color_weight` finite and >= 0, `normal_merge_angle` in [0, 180]. Otherwise
the request is ignored with a warning and the primitive loads without one.
Members other than `version` and `settings` (the `indices` and `levels` of
the chain files of earlier builds carried) are not read.

## Schema

[`schema/ERHE_mesh_lod.schema.json`](schema/ERHE_mesh_lod.schema.json)
