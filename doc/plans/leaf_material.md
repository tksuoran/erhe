# Leaf material: remaining work

Status: proposed

Extends [../erhe/leaf_material.md](../erhe/leaf_material.md), which describes
the texture-free leaf BxDF (`Bxdf_model::leaf`): per-face reflection and
diffuse transmission, double-sided drawing and shadows, glTF
`KHR_materials_diffuse_transmission`, the content library presets.

## Textures

- Per-face albedo and normal maps (the back face has neither today), the
  `KHR_materials_diffuse_transmission` transmission and transmission color
  textures, and a thickness map.
- Habel's baked translucency (thesis chapter 3, sections 3.3.5 - 3.3.9): the
  multi-dipole transmittance kernel convolved over the thickness, albedo and
  normal maps, stored per texel in the Half-Life 2 basis and evaluated with
  the light direction. It replaces the constant transmission term where a
  leaf has the maps.

## Other renderers

- The ray query path (`erhe_ray_hit.glsl` `evaluate_bxdf()`): a
  `bxdf_model == 5u` case taking the face from the hit's front-face flag, so
  DDGI bounces and ray traced views see the transmission.
- The lightmap baker: a texel stores one side's irradiance; a leaf needs both
  sides before lightmapped leaves transmit baked light.

## Import and export

- USD: OpenPBR `geometry_thin_walled` with `subsurface_weight` /
  `transmission_weight` mapped to the leaf model, and the reverse on export.
- glTF: the front `reflectance` has no glTF carrier (true of every material);
  a leaf's front reflectance is lost on export until one exists
  (`KHR_materials_specular` or an `ERHE_material` field).
