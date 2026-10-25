# ERHE_texture_ktx2

Stability: experimental

## Contributors / status

erhe project. Draft vendor extension. Introduced by "Save for Quest"
([`../plans/gltf_fast_load.md`](../plans/gltf_fast_load.md) follow-up
item 4).

## Dependencies

Written against glTF 2.0 and KTX 2.0. Listed in `extensionsUsed` only (see
"Validation / fallback" for why not `extensionsRequired`).

## Scope

**Asset root** extension, naming per-texture images.

## Overview

Gives textures a KTX2 image that holds **raw GPU blocks** (a non-zero
`vkFormat`), which `KHR_texture_basisu` cannot carry: that extension is only
for Basis Universal payloads (`vkFormat` 0, ETC1S / UASTC). erhe writes such
images with File > Save glTF for Quest / MCP `save_scene_for_quest`: every
Basis Universal texture of the scene is transcoded at save time to ASTC 4x4,
so a device that samples ASTC (Quest 3) uploads the stored blocks with no
transcode and no decode.

A texture listed here names no core `source` and carries no texture
extension; the entry below supplies its image. (glTF allows a texture
without `source` when an extension supplies the image.) The extension sits
on the asset root because erhe's glTF library (fastgltf) passes generic
extensions through for the asset, scenes, nodes, cameras, materials, meshes
and primitives, not for textures.

## Payload

Each referenced image is a glTF image with a bufferView and `mimeType`
`image/ktx2`, holding a KTX 2.0 file with:

- `vkFormat` `VK_FORMAT_ASTC_4x4_SRGB_BLOCK` (158) or
  `VK_FORMAT_ASTC_4x4_UNORM_BLOCK` (157): SRGB for color data (base color,
  emissive), UNORM for data textures (normal, metallic-roughness,
  occlusion). The writer takes the color space from the loaded texture's
  format, which the loader picked from the first material slot that uses
  the image;
- `typeSize` 1, `pixelDepth` 0, `layerCount` 0, `faceCount` 1, the full mip
  chain of the source (`levelCount` > 0);
- `supercompressionScheme` 0 (none; levels aligned to 16 bytes) or 2
  (Zstandard; each level compressed on its own, not aligned). erhe writes
  Zstandard: the Bistro ASTC data compresses to about 22 % of its size;
- a Khronos basic data format descriptor: color model `KHR_DF_MODEL_ASTC`
  (162), BT.709 primaries, transfer function sRGB (2) or linear (1), texel
  block 4x4x1, `bytesPlane0` 16 (0 when supercompressed), one sample of 128
  bits with channel `KHR_DF_CHANNEL_ASTC_DATA`;
- key/value data `KTXwriter` (`erhe`) and, for a two component normal map,
  `KTXwriterScParams` containing `--normal-mode` (the marker
  `ktx encode --normal-mode` writes).

A **two component normal map** (X+Y, Z reconstructed in the shader) keeps
the layout the Basis transcoder produces for ASTC: X in RGB, Y in A (L+A
endpoint blocks). erhe's shader decodes it with the `.ga` path for any RGBA
format (`shader_key.cpp`, `NORMAL_TEXTURE_ENCODING`), whether the blocks are
sampled as ASTC or decoded to RGBA8; BC5's X+Y in R+G layout is not used.

## JSON layout

```json
"extensions": {
  "ERHE_texture_ktx2": {
    "version": 1,
    "textures": [
      {"texture": 0, "source": 0},
      {"texture": 1, "source": 1}
    ]
  }
}
```

| Field | Type | Meaning |
|---|---|---|
| `version` | integer | 1 |
| `textures` | array | one entry per texture whose image is a raw block KTX2 |
| `textures[].texture` | integer | index into `textures` |
| `textures[].source` | integer | index into `images` (the KTX2 image) |

## Loading (erhe)

`parse_gltf` applies each entry as the texture's image before resolving
material slots. `Gltf_image_residency` opens the image on an executor worker
(`Image_loader_ktx2`), inside the decoded-bytes budget:

- the device samples ASTC 4x4 (`query_gltf_device_options` checks both color
  spaces; `Astc_support::native`): the stored levels (inflated when
  Zstandard) are uploaded as they are;
- otherwise mip level 0 is decoded to RGBA8 on the CPU with the ARM
  astc-encoder decoder (SRGB / UNORM kept) and the GPU mipmap generation
  path rebuilds the chain. RGBA8 needs four times the GPU memory of ASTC.

Each load logs one summary line per file
(`async load '<file>' images: N Basis KTX2 transcoded, N raw ASTC KTX2
uploaded as stored, N raw ASTC KTX2 decoded to RGBA8 ...`).

## Validation / fallback

- An entry whose indices are out of range is skipped (and counted in the
  `ERHE_texture_ktx2:` log line); a `version` other than 1 makes the loader
  ignore the extension.
- A KTX2 file with an unsupported `vkFormat` or supercompression scheme, a
  depth, array or cube image, or a level whose size does not match its
  dimensions fails to decode; the texture slot stays empty.
- The extension is listed in `extensionsUsed` only. A loader without it sees
  textures without a source and renders the materials untextured. It is not
  listed in `extensionsRequired` because fastgltf, which erhe's own loader is
  built on, refuses files that require an extension it does not know.
- PNG / JPEG images in the same file keep their core `source`.

## Schema

[`schema/ERHE_texture_ktx2.schema.json`](schema/ERHE_texture_ktx2.schema.json)
