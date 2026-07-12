# ERHE_brushes

## Scope

**Asset-root** extension (the top-level `extensions` object). Optional
(`extensionsUsed` only).

## Overview

Persists the scene's brush library. A brush is an editor-only stamping
asset (geometry + material + physical density + normal style); its
geometry exports as a glTF mesh that **no node references**, carrying
`ERHE_geometry` for a bit-exact reload. Foreign viewers simply see (and
may ignore) the unreferenced meshes. The brush collision shape is not
persisted - it is rebuilt from the geometry at first instantiation, as at
runtime.

## JSON layout

```json
{
    "brushes": [
        {
            "name": "Cube",
            "folder_path": "Platonic Solids",
            "mesh": 17,
            "material": 3,
            "density": 1,
            "normal_style": "polygon_normals"
        }
    ]
}
```

- `name`: brush name.
- `folder_path` (optional): slash-separated content-library folder path
  relative to the Brushes root; omitted for top-level brushes. Preserves
  the library folder hierarchy.
- `mesh`: glTF mesh index of the (node-unreferenced) geometry carrier.
- `material` (optional): glTF material index of the brush material.
- `density`: physical density used for mass computation.
- `normal_style`: `none` | `corner_normals` | `polygon_normals` |
  `point_normals`.

## Load semantics

Rebuilds each brush into the content library (re-creating folders as
needed); geometry comes from the referenced mesh's `ERHE_geometry`
primitive.

## Schema

[schema/ERHE_brushes.schema.json](schema/ERHE_brushes.schema.json)
