# ERHE_texture_properties

Stability: experimental

## Contributors / status

erhe project. Draft vendor extension. Introduced by the texture properties
of source textures (`doc/plans/prepared_assets.md` R4.2, R4.3; D8 step 6a
of `doc/plans/prepared_assets_design.md`).

## Dependencies

Written against glTF 2.0. Listed in `extensionsUsed` only.

## Scope

**Asset root** extension, naming per-image properties. It sits on the asset
root for the reason `ERHE_texture_ktx2` does: fastgltf passes generic
extensions through for the asset, scenes, nodes, cameras, materials, meshes
and primitives, not for images.

## Overview

The authoring properties of a texture that its conversion reads: how its
texels are encoded, whether it is a normal map and how many components it
stores, and whether mip levels are made for it. erhe writes an entry for
every image a file holds. A loader that reads an entry uses it in place of
deriving the properties from the material slots that use the image, so an
edit of a property survives a save; an image without an entry (any foreign
glTF) takes its properties, once, from the first material slot that uses it
(`doc/erhe/gltf.md` "Texture properties").

## JSON layout

```json
"extensions": {
  "ERHE_texture_properties": {
    "version": 1,
    "images": [
      {"image": 0, "transfer_function": "srgb", "normal_encoding": "none",
       "generate_mipmaps": true, "wrap_u": "repeat", "wrap_v": "clamp_to_edge"}
    ]
  }
}
```

| Field | Type | Meaning |
|---|---|---|
| `version` | integer | 1 |
| `images` | array | one entry per image |
| `images[].image` | integer | index into `images` |
| `images[].transfer_function` | string | `srgb` (color data) or `linear` (data textures); the decode and the transcode read the image in this color space |
| `images[].normal_encoding` | string | `none` (not a normal map), `three_component`, `two_component` (X and Y stored, Z reconstructed; BC5 / the ASTC X+Y layout when transcoded) |
| `images[].generate_mipmaps` | boolean | true: the mip levels the image does not carry are made at load |
| `images[].wrap_u`, `images[].wrap_v` | string | `repeat`, `clamp_to_edge` or `mirrored_repeat`: the wrap mode mipmap generation filters with at the image's edges |

## Loading (erhe)

`parse_gltf` reads the entries before the images are prepared; an entry
whose fields do not parse or whose index is out of range is skipped (the
image then derives its properties) and counted in a warning; a `version`
other than 1 makes the loader ignore the extension. The properties become
the loaded `erhe::graphics::Texture`'s texture properties.

## Schema

[`schema/ERHE_texture_properties.schema.json`](schema/ERHE_texture_properties.schema.json)
