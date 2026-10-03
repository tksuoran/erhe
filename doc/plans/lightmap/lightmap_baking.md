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
a pastel diffuse surface. The options, in the order they cost effort:

a) leave it as it is;
b) add the `(1 - metallic)` weight and accept near-black metals until something
   provides specular;
c) re-enable analytic specular (only) for lightmapped draws;
d) bake a specular approximation.

This is a user decision about appearance, not a defect to fix in passing.

## Adaptive gather budget

The per-frame gather budget is a fixed texel band (`c_texels_per_tick`), not
adaptive to measured dispatch time. Drive the band size from the measured
gather time toward a millisecond target, using the existing GPU timing
infrastructure (`Gpu_timer`).

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
the environment this needs; the core observes the rules in section 6 of the
current document, and the bake-to-disk path (`start_offline_bake` /
`offline_tick`, section 9) already bakes every tile one per frame. What remains
is argument parsing (for example
`--bake-lightmaps <in.glb> [-o out.glb] [--target-spp N]`), progress logging, a
saturating dispatch loop and the exit status.

## Out of scope for now

Directional lightmaps (SH L1, designed for but not built), light probes for
dynamic objects (`DDGI` covers the runtime case;
[../ddgi.md](../ddgi.md)), a ray tracing pipeline, OIDN in process, KTX2 or EXR
interchange export, lightmap block compression (BC6H, ASTC-HDR), skinned and
dynamic geometry, and a CPU (embree) bake fallback.
