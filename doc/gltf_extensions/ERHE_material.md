# ERHE_material

## Scope

**Material** extension. Optional (`extensionsUsed` only).

## Overview

Carries the erhe-specific `Material_data` fields that have no standard glTF
representation. Migrates the legacy material extras writer field-for-field;
the extras remain parsed for older files, this extension wins when both are
present. Fields at their round-trip defaults are omitted; a material with
no divergent fields gets no extension at all. `unlit` rides the standard
`KHR_materials_unlit`, and OPAQUE / BLEND / MASK blending rides the core
`alphaMode`.

## JSON layout

```json
{
    "roughness_y": 0.25,
    "bxdf_model": "anisotropic_brdf",
    "blending_mode": "multiply",
    "use_circular_brushed_metal": true,
    "circular_brushed_metal_tex_coord": 0,
    "use_aniso_control": true
}
```

- `roughness_y`: anisotropic roughness (emitted when != roughness x).
- `bxdf_model`: one of `unlit`, `isotropic_brdf`, `anisotropic_brdf`,
  `anisotropic_slope`, `anisotropic_engine_ready` (emitted for values not
  covered by `KHR_materials_unlit` / the default `isotropic_brdf`).
- `blending_mode`: one of `opaque`, `alpha_blend`, `alpha_test`,
  `multiply`, `add`, `subtract`, `screen_door` (emitted only for the modes
  `alphaMode` cannot represent; those export with `alphaMode` `BLEND` as a
  sensible core fallback).
- `use_circular_brushed_metal`, `circular_brushed_metal_tex_coord`,
  `use_aniso_control`: brushed-metal / anisotropy-control shading fields.

## Schema

[schema/ERHE_material.schema.json](schema/ERHE_material.schema.json)
