# ERHE_material

Stability: mostly stable

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
    "circular_brushed_metal_texgen_mode": "uv1",
    "use_aniso_control": true,
    "base_color_texgen_mode": "world_xz"
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
- `use_circular_brushed_metal`, `circular_brushed_metal_texgen_mode`,
  `use_aniso_control`: brushed-metal / anisotropy-control shading fields.
- `<slot>_texgen_mode` for slots `base_color`, `metallic_roughness`,
  `normal`, `occlusion`, `emissive`: the slot's texture coordinate source
  when it is not a UV set. One of `world_xy`, `world_xz`, `world_yz`
  (world position plane), `node_xy`, `node_xz`, `node_yz` (untransformed
  node-space position plane), `tangent` (world position projected onto
  the tangent frame's T/B plane). The UV modes `uv0`, `uv1`, `uv2` are
  never emitted here - they ride the core `texCoord` index (which stays 0
  when a non-UV mode is emitted). `circular_brushed_metal_texgen_mode`
  accepts the same names, UV modes included.
- `normalmap_encoding`: storage encoding of the normal texture (emitted
  when not the default `right_handed_three_channel`). One of
  `right_handed_three_channel`, `left_handed_three_channel` (RGB = XYZ),
  `right_handed_two_channel_ga`, `left_handed_two_channel_ga` (X in RGB /
  Y in A, KTX2 normal-mode), `right_handed_two_channel_rg`,
  `left_handed_two_channel_rg` (X in R / Y in G, BC5). Left handed =
  Direct3D Y convention (the shader flips Y). A KTX2 normal-mode texture
  overrides the channel layout at render time; the handedness is always
  honored.
- `style`: the name of the style item the material uses
  (`doc/style_library.md` D4), one of the scene's `ERHE_scene` `styles`;
  emitted only when the material has a style. Assigned on load once the
  styles exist; an unknown name is logged and assigns nothing.

## Schema

[schema/ERHE_material.schema.json](schema/ERHE_material.schema.json)
