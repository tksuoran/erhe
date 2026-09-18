# Lightmap baking follow-ups

Status: in progress

Extends [../../lightmap_baking.md](../../editor/lightmap_baking.md), which describes
the interactive baker, its artifact defenses, the tile grid and the world-space
partition as they work today.

## Lightmapped metals need a decision

The bake stores diffuse irradiance and the G-buffer albedo is
`base_color * (1 - metallic)`, so a metal bounces almost nothing, which is
physically right. But `standard.frag`'s runtime term is
`lightmap * base_color` **without** the `(1 - metallic)` weight, and the
lightmap gate also disables analytic specular, so a lightmapped metal renders as
a pastel diffuse surface. Every material in the default scene has
`metallic = 1`, so this is immediately visible there. The options, in the order
they cost effort:

a) leave it as it is;
b) add the `(1 - metallic)` weight and accept near-black metals until something
   provides specular;
c) re-enable analytic specular (only) for lightmapped draws;
d) bake a specular approximation.

This is a user decision about appearance, not a defect to fix in passing.

## Sky lighting in the bake

The gather handles punctual lights only - directional, point and spot shadow
rays plus one diffuse bounce - so a ray that escapes to the sky contributes
nothing. Feed the procedural sky
([../../procedural_sky.md](../../erhe/procedural_sky.md)) into the gather's miss
path, through the `sky_sample_*` helpers. Do this before tuning any threshold
that is measured against baked luminance, such as the seam classifier in
[seam_driven_unwrap.md](seam_driven_unwrap.md).

## Adaptive budget and bounce-origin bias

The per-frame gather budget is a fixed texel band, not adaptive to measured
dispatch time; the GPU timing infrastructure needed for a millisecond-targeted
budget already exists. The single bounce spawns only from G-buffer texels, where
the fixed bias applies, so there is no adaptive bounce-origin bias.

## Dynamic occluder motion resets accumulation every frame

An occluder that moves continuously (physics jitter, for example) resets the
accumulation every frame, so the bake never converges. Decide a policy - a
motion threshold, or excluding dynamic bodies from the occluder hash - if this
is hit in practice.

## G-buffer re-raster on invalidation is a blocking submit

`bake_gbuffer` on invalidation is a standalone wait-idle submit, which hitches
on a lightmapped-mesh transform edit. Record it into the frame command buffer
like the gather slice.

## GLB persistence and encoding

Baked tiles persist as `.lmt` payloads plus a manifest beside the scene. A
scene-embedded form would carry the bake with the file: an `ERHE_lightmap` glTF
extension with the atlas payload (RGB9E5 or RGBA16F buffer view plus dimensions
and encoding), the per-instance `uv_scale_offset` and a per-mesh record that
channel-2 UVs are baked, plus a hash of (geometry ids, transforms, lights,
density) so a stale bake still loads but is flagged. RGB9E5 is the shipping
encoding: 4 bytes per texel, filterable, supported everywhere including Quest.

## Non-interactive command-line bake

An unattended bake: load a scene, unwrap and partition as needed, run the gather
to a target quality (minimum samples per texel and/or a variance threshold) at
full GPU throughput, denoise / dilate / seam-fix, write the baked scene, and
exit non-zero on failure (no ray-query device, an unpackable atlas, ...). The
headless build already runs the engine without a window or swapchain, which is
the environment this needs, and the core observes the rules in section 6 of the
current document, so this is mostly argument parsing (for example
`--bake-lightmaps <in.glb> [-o out.glb] [--target-spp N]`), progress logging and
a saturating dispatch loop.

## Out of scope for now

Directional lightmaps (SH L1, designed for but not built), light probes for
dynamic objects (`DDGI` covers the runtime case;
[../ddgi.md](../ddgi.md)), a ray tracing pipeline, OIDN in process, KTX2 or EXR
interchange export, lightmap block compression (BC6H, ASTC-HDR), skinned and
dynamic geometry, and a CPU (embree) bake fallback.
