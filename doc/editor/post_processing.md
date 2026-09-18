# Post-Processing Pipeline

Stability: mostly stable

> This document was mostly written by Claude and may contain inaccuracies.

The post-processing system implements pyramidal bloom with progressive
downsample and upsample passes, operating on mip chains of two internal
textures.

Source files:
- `src/editor/rendergraph/post_processing.cpp`
- `src/editor/rendergraph/post_processing.hpp`

## Textures

| Texture | Format | Usage flags | Notes |
| :--- | :--- | :--- | :--- |
| `input_texture` | varies | sampled | Output of the upstream Viewport_scene_view |
| `downsample_texture` | RGBA16F | color_attachment, sampled | Mip pyramid, written during downsample |
| `upsample_texture` | RGBA16F | color_attachment, sampled | Mip pyramid, written during upsample |

Both `downsample_texture` and `upsample_texture` are created with
`use_mipmaps = true` and all mip levels are pre-transitioned to
`SHADER_READ_ONLY_OPTIMAL` in `update_size()`.

Per-mip texture views are created when `device.use_texture_view` is true,
so that sampling from the source mip (in `SHADER_READ_ONLY_OPTIMAL`) does
not conflict with the destination mip being used as a color attachment
(in `COLOR_ATTACHMENT_OPTIMAL`).

## Mip level organization

Given a level-0 size of WxH, mip levels are generated until both
dimensions reach 1. Example for 512x512: levels 0..9 with sizes
512, 256, 128, 64, 32, 16, 8, 4, 2, 1.

- `downsample_source_levels`: [0, 1, 2, ..., level_count - 2]
- `upsample_source_levels`: [level_count - 1, level_count - 2, ..., 1]

## Downsample chain

Each downsample pass reads from source_level and writes to
source_level + 1. Data flow is strictly forward: a pass only
samples from earlier passes, never from later ones.

```
 input_texture  downsample_texture
 (all mips)     mip 0   mip 1   mip 2   mip 3   mip 4
                  |       |       |       |       |
  D[0]: read ----.----> write     |       |       |
                          |       |       |       |
  D[1]:           read ---.----> write    |       |
                                  |       |       |
  D[2]:                   read ---.----> write    |
                                          |       |
  D[3]:                           read ---.----> write
```

| Pass | Destination mip | Sampled textures | Pipeline shader |
| :--- | :--- | :--- | :--- |
| D[0] | downsample mip 1 | input_texture | downsample_with_lowpass_input |
| D[1..lowpass-1] | downsample mip i+1 | downsample mip i (per-mip view) | downsample_with_lowpass |
| D[lowpass..end] | downsample mip i+1 | downsample mip i (per-mip view) | downsample |

Downsample passes do NOT sample from `upsample_texture`. The upsample
texture descriptor is bound but not read by the downsample shaders.

### Downsample filter

The lowpass downsample uses a custom 36-texel kernel (13 bilinear
fetches) that is a weighted average of one-pixel-shifted 2x2 boxes.
This eliminates the pulsating artifacts and temporal stability issues
common in naive pyramid approaches. Once the mip chain is below
`lowpass_count` levels, a simple 2x2 downsample is used instead.

## Upsample chain

Each upsample pass reads from source_level and writes to
source_level - 1. The upsample chain starts where the downsample
chain ended (at the smallest mip) and works back up to mip 0.

```
 downsample_texture  upsample_texture
 (full texture)      mip 4   mip 3   mip 2   mip 1   mip 0
                       |       |       |       |       |
  U[4]: read ---------.----- write    |       |       |
                               |       |       |       |
  U[3]: read ---------.-- read-+---- write    |       |
                                       |       |       |
  U[2]: read ---------.------- read ---.---- write    |
                                                |       |
  U[1]: read ---------.--------------- read ---.---- write
```

| Pass | Destination mip | Sampled textures | Pipeline shader |
| :--- | :--- | :--- | :--- |
| U[top] | upsample mip top-1 | downsample (full, at destination lod) | upsample_first |
| U[mid] | upsample mip i-1 | downsample (full, at destination lod), upsample mip i (per-mip view) | upsample |
| U[1] | upsample mip 0 | input_texture, downsample (full, at dest lod), upsample mip 1 (view) | upsample_last |

### Upsample filter

A 3x3 tent filter scaled by `upsample_radius`:

```
  1/16 * [ 1 2 1 ]
         [ 2 4 2 ]
         [ 1 2 1 ]
```

The progressive upsample accumulates the chain:

```
  u4 = d4
  u3 = d3 + blur(u4)
  u2 = d2 + blur(u3)
  u1 = d1 + blur(u2)
  u0 = d0 + blur(u1)

  Expanded: u0 = d0 + blur(d1 + blur(d2 + blur(d3 + blur(d4))))
```

Repeated convolutions converge to a Gaussian (Kraus 2007).

## Render pass synchronization

### Barriers around each render pass

Each render pass emits a global `VkMemoryBarrier2` before
`vkCmdBeginRenderPass` (`compute_inter_pass_barrier()` in
`vulkan_render_pass.cpp`). Its source side is derived from the attachment's
`usage_before`; its destination side always includes
`FRAGMENT_SHADER | SHADER_READ` plus the stage and access of whatever attachment
types are present (`COLOR_ATTACHMENT_OUTPUT | COLOR_ATTACHMENT_WRITE` for
color).

After `vkCmdEndRenderPass`, the framework emits a second `VkMemoryBarrier2`
derived from the attachment types (source) and `usage_after` (destination)
(`compute_post_render_pass_barrier()`):

```
srcStage  = COLOR_ATTACHMENT_OUTPUT  (from attachment type)
srcAccess = COLOR_ATTACHMENT_WRITE
dstStage  = FRAGMENT_SHADER          (from usage_after = sampled)
dstAccess = SHADER_READ
```

This makes each writing pass's output visible for its declared next use, with
no action needed in callers.

The VkRenderPass itself uses canonical subpass dependencies
(`make_canonical_subpass_dependencies2()` in `vulkan_helpers.cpp`):

```
EXTERNAL -> subpass 0:
  srcStage  = COLOR_ATTACHMENT_OUTPUT
  srcAccess = 0
  dstStage  = COLOR_ATTACHMENT_OUTPUT
  dstAccess = COLOR_ATTACHMENT_WRITE

subpass 0 -> EXTERNAL:
  srcStage  = COLOR_ATTACHMENT_OUTPUT
  srcAccess = COLOR_ATTACHMENT_WRITE
  dstStage  = COLOR_ATTACHMENT_OUTPUT
  dstAccess = 0
```

The render pass's `initialLayout` and `finalLayout` are set from
`layout_before` and `layout_after` on the attachment descriptor.

### Per-mip texture views

To avoid layout conflicts when a texture is simultaneously a render
target (destination mip, in `COLOR_ATTACHMENT_OPTIMAL`) and a sampled
source (source mip, in `SHADER_READ_ONLY_OPTIMAL`), per-mip texture
views are used. Each view covers a single mip level.

### Attachment descriptors

All downsample and upsample render passes use:

```
usage_before  = sampled
usage_after   = sampled
layout_before = shader_read_only_optimal
layout_after  = shader_read_only_optimal
```

The cross-pass write-to-read dependency (color attachment write in pass N,
fragment shader read in pass N+1) is carried entirely by the end-of-pass
barrier that `usage_after = sampled` produces.

### Why the end-of-pass barrier carries the dependency

The other three mechanisms cannot, and a downsample chain is the case that
proves it. When downsample pass D[n] writes `downsample_texture` mip N+1 as a
color attachment, pass D[n+1] samples that mip in its fragment shader while its
own attachment is mip N+2 - a different subresource:

1. D[n]'s subpass-to-EXTERNAL dependency makes `COLOR_ATTACHMENT_WRITE`
   available in `COLOR_ATTACHMENT_OUTPUT` (dstAccess = 0), and transitions mip
   N+1 back to `SHADER_READ_ONLY_OPTIMAL`.
2. D[n+1]'s inter-pass barrier has `srcStage = FRAGMENT_SHADER,
   srcAccess = SHADER_READ` (from `usage_before = sampled` on the attachment at
   mip N+2), which does not cover `COLOR_ATTACHMENT_OUTPUT` where D[n]'s writes
   became available.
3. D[n+1]'s EXTERNAL-to-subpass dependency does chain with step 1, but its
   `dstStage = COLOR_ATTACHMENT_OUTPUT` makes the data visible in the color
   attachment output stage only, not in `FRAGMENT_SHADER` where the sampling
   happens.

The end-of-pass barrier is what closes that gap, which is why every pass in both
chains declares `usage_after = sampled` and none of them opts out.

### Explicit barriers via Device::cmd_texture_barrier()

When `usage_after` includes the `user_synchronized` bit, the automatic
end-of-pass barrier is suppressed and the caller inserts an explicit barrier
with `Device::cmd_texture_barrier(usage_before, usage_after)` between render
passes, on the frame's shared command buffer. The `user_synchronized` bit is a
modifier in `Image_usage_flag_bit_mask` that does not affect layout computation
or stage / access mapping - it only controls whether the automatic barrier
fires.

## Parameter buffer layout

Per-mip-level uniform data, aligned to device buffer alignment:

| Field | Type | Description |
| :--- | :--- | :--- |
| input_texture | uvec2 | Bindless handle for input texture |
| downsample_texture | uvec2 | Bindless handle for downsample texture |
| upsample_texture | uvec2 | Bindless handle for upsample texture |
| texel_scale | vec2 | 1/width, 1/height of source mip level |
| source_lod | float | Source mip level index |
| level_count | float | Total number of mip levels |
| upsample_radius | float | Tent filter radius scale |
| mix_weight | float | Blend weight for this level |
| tonemap_luminance_max | float | HDR tone mapping max luminance |
| tonemap_alpha | float | HDR tone mapping alpha |

## Samplers

| Sampler | Filter | Mipmap mode | Used for |
| :--- | :--- | :--- | :--- |
| sampler_linear | linear/linear | not mipmapped | input_texture |
| sampler_linear_mipmap_nearest | linear/linear | nearest | downsample_texture, upsample_texture |

## Pipeline configuration

All passes use a fullscreen triangle (3 vertices, no vertex buffers):
- Cull mode: none
- Depth/stencil: disabled
- Color blend: disabled
- Topology: triangle

## Future work

- [plans/post_processing.md](../plans/post_processing.md) - debug per-mip
  barrier tracker.
