# Quest fragment shader: the mobile shading variant

Status: in progress

Extends `doc/erhe/shader_variants.md` (the `Shader_key` axes) and
`doc/erhe/shadows.md` (the shadow lookup); continues
`doc/plans/shader_fp16.md` (the fp16 paths and their boundaries) for the
headset, and feeds `doc/plans/quest_performance.md` (the frame budget the
work is measured against). The user ruling this plan rests on: the mobile
variant may render differently from the desktop path, as long as nothing
looks broken (no acne fields, detached contact shadows wider than a few
texels, black or saturated patches, NaN speckle).

## Where the headset fragment cost is

Static cost on the Quest 3 GPU, measured offline on recorded `standard.frag`
variants rebuilt with the OpenXR preset's axes (hard filter, 16-bit depth,
receiver-plane bias; the offline compiler setup is per machine,
`memory-bank/local/context.md`). The base variant is the isotropic BRDF,
vertex colors, no textures, one shadow-mapped directional light; "textured"
adds a base color and a normal map with the tangent frame.

| Variant | Instructions | ALU 32 / 16 | Complex | Texture | Memory | Registers | Occupancy |
|---|---|---|---|---|---|---|---|
| base, as built | 642 | 342 / 0 | 27 | 6 | 16 | 17 | 62 % |
| base, light unshadowed | 258 | 137 / 0 | 8 | 0 | 7 | 6 | 200 % |
| base, no light (ambient only) | 72 | 27 / 0 | 0 | 0 | 3 | 3 | 200 % |
| base, one plain compare tap instead of the lookup | 311 | 163 / 0 | 9 | 1 | 13 | 9 | 125 % |
| base, fp16 paths 31 | 655 | 281 / 70 | 28 | 6 | 16 | 16 (+23 half) | 75 % |
| base, fp16 path 1 (material) alone | 645 | 338 / 4 | 27 | 6 | 16 | 16 (+23 half) | 75 % |
| base, slides' BRDF (GGX D, Hable visibility, SG Schlick) | 632 | 321 / 0 | 25 | 6 | 16 | 17 | 62 % |
| base, plain tap + slides' BRDF | 287 | 142 / 0 | 7 | 1 | 13 | 9 | 125 % |
| base, plain tap + slides' BRDF + fp16 paths 31 | 291 | 87 / 55 | 6 | 1 | 13 | 9 (+14 half) | 125 % |
| textured, as built | 772 | 408 / 0 | 31 | 8 | 25 | 24 | 50 % |
| textured, light unshadowed | 370 | 203 / 0 | 12 | 2 | 16 | 13 | 87 % |
| textured, identity texture transform | 760 | 396 / 0 | 31 | 8 | 21 | 22 | 50 % |
| textured, plain tap + slides' BRDF + identity transform | 393 | 196 / 0 | 11 | 3 | 18 | 15 | 75 % |
| base, the light a shadowed spot | 694 | 376 / 0 | 31 | 6 | 17 | 17 | 62 % |
| base, shadowed spot with a plain compare tap | 396 | 197 / 0 | 13 | 1 | 14 | 10 | 100 % |
| base, the light a shadowed point (cube lookup) | 748 | 378 / 3 | 34 | 6 | 9 | 14 (+24 half) | 75 % |
| base, the light an unshadowed point | 283 | 150 / 0 | 10 | 0 | 7 | 7 | 150 % |
| shadowed directional + spot + point | 1617 | 860 / 3 | 86 | 10 | 32 | 19 (+12 half) | 62 % |
| the preset's maxima (1 + 4 directional, 2 + 4 spot, 1 + 4 point) | 2384 | 1262 / 3 | 127 | 12 | 48 | 20 (+17 half) | 50 % |

Reading:

- **The shadow lookup is the shader.** The derived bias of
  `doc/erhe/shadows.md` (receiver plane, D vectors, minimum bias terms,
  rounding bounds, texel selection) costs 384 of the 642 instructions of
  the one-light variant and 11 of its 17 registers; a plain compare tap
  costs 53. The spot lookup costs the same, the point cube lookup 465. The
  bias mode axis changes nothing with the hard filter (slope_scaled equals
  receiver_plane there): the cost is the bound machinery, not the filter.
- **The BRDF is cheap.** The slides' reformulation (GGX distribution,
  Hable's visibility in L.H, spherical-gaussian Schlick, one division)
  saves 10 instructions. The lit path without shadow is 186 instructions
  over ambient, most of them the light record fetch, the frame normalization
  and the view vector, not the lobe.
- **The fp16 paths as built do not shorten the shader.** Every value is
  converted at its path boundary and the vectors stay fp32, so the counts
  stay level (655 against 642) while the half register footprint grows.
  What fp16 buys on Adreno is the double-rate 16-bit ALU, which the static
  count does not show; only the device bench can rate it (build order
  step 0). Path 1 alone raises the occupancy (62 to 75 %) by shrinking the
  fp32 footprint by one register: a register effect, not an instruction
  effect.
- **Texture transforms** cost 12 instructions, 4 record reads and 2
  registers per textured material (glTF materials nearly always carry the
  identity).
- **The material record** is 3 of the 16 memory reads of the untextured
  variant and about 12 of the 25 of the textured one (five handles, five
  rotation / scale vec4, five offsets, two decode vec4, the channel
  selectors).

The device side of the same picture (`doc/plans/quest_performance.md`):
the Bistro eye pass shades about 30 M fragments per frame for 5.9 M eye
pixels at 80 - 97 ALU per fragment, texture fetch stall 4 %, so bin
rendering is shader time times fragment count. This plan halves the shader
time of the lit variants; the fragment count (overshading, the discarding
draws) is the other factor and stays with the performance plan.

## What the slides give, and what does not apply

Renaldas Zioma's "Optimizing PBR" (SIGGRAPH 2015 Moving Mobile Graphics
course) is written for 2012 - 2015 GPUs; what carries over to the
Adreno 740 and what does not:

- **Precision**: "check your epsilons (1e-4 is OK, 1e-5 is not)" and "add
  clamping for fp16 overflow". The standard shader's floors are 1e-7 (the
  GGX denominator), 1e-8 (the visibility denominator) and 1e-4 (roughness);
  the fp16 plan's floors (6.1e-5 and 0.09) are the answer and the mobile
  variant takes them (M3).
- **Faster BRDF**: GGX with one complex op, Hable's visibility in L.H,
  the spherical-gaussian Fresnel, Fresnel post-multiplied for scalar
  pipelines. Adreno is scalar and the form fits, but the lobe is 10
  instructions of 642 here; M6 keeps it in reserve.
- **Per-vertex diffuse and ambient** (mid tier) is rejected: the headset
  frame is geometry bound (binning 10.6 ms, vertex fetch stall 53 %, 4 M
  shaded vertices per frame), so work moved to the vertex stage lands on
  the bound side and adds varyings to the binning pass.
- **LUT specular** (low tier, low ALU / TEX ratio) is rejected: the
  Adreno 740 has the ALU for the lobe, the texture fetch stall is 4 %, and a
  dependent fetch per light would cost more than the 20 ALU it replaces.
- **Gamma hack**: not applicable; the pipeline is linear into an fp16
  target the compositor consumes, and sRGB textures decode in the sampler.
- **Environment BRDF and texCUBElod**: not applicable today (the headset
  path has a flat ambient term, no image-based lighting); when a sky IBL
  arrives, the analytic environment BRDF fit is the form to take.

## What Filament does on mobile

Google Filament (`shaders/src/surface_brdf.fs`, `surface_shadowing.glsl`,
`surface_light_punctual.fs`, `ShadowMapManager.cpp` of a local clone) is
the reference for a shipped mobile PBR path; the items this plan takes
from it:

- **Shadow bias is per light, applied before the fragment shader.** The
  normal offset `n * sin(theta) * normal_bias` is applied to the receiver
  point with `normal_bias = option * texel size at one meter` computed per
  light on the CPU; the constant bias is a translation along the light
  direction folded into the light's matrix; the caster side uses the
  rasterizer's polygon offset (slope 2, constant 0.5). The fragment side is
  one perspective divide and one compare fetch. M1 takes the per-light
  constants and the fold; whether the offset and projection run in the
  vertex shader (Filament: one vec4 varying per shadow light) or in the
  fragment shader is an Open item, because the headset is binning bound.
- **Hardware compare plus a 3x3 gaussian** (`ShadowSample_PCF_Low`): four
  bilinear compare fetches at computed offsets give a 3x3 gaussian
  footprint, the step between the hard tap and a real PCF. M1 offers it as
  the mobile filter's second value.
- **Everything is mediump except positions, matrices, shadow coordinates
  and emissive**, with fp16-safe forms: the GGX distribution computes
  `1 - N.H^2` as `|N x H|^2` (the fp16 plan's P2 does the same) and clamps
  `k` to 453.5 against fp16 overflow instead of normalizing by alpha^2;
  the visibility term is Hammon's `0.5 / mix(2 N.L N.V, N.L + N.V,
  roughness)` with no square roots (`V_SmithGGXCorrelated_Fast`); every
  division has an fp16 floor (`PREVENT_DIV0`, 7.7e-6 and the like). M3
  and M6 take these forms.
- **Per-light constants replace per-fragment trigonometry**: the spot
  cone is `saturate(cos * scale + offset)^2` with `scale` and `offset`
  precomputed per light (erhe normalizes two vectors and runs a smoothstep
  per fragment per spot light); the falloff window is
  `(1 - (d^2 / r^2)^2)^2 / max(d^2, 1e-4)`; the light intensity is stored
  pre-exposed, which is P3's exposure fold at no shader cost. M1 adds the
  spot constants to the record; the pre-exposed radiance is a
  `Light_buffer` change that the mobile accumulation uses.
- **The DFG term is a LUT** only for image-based lighting, which the
  headset path does not have; the analytic fit stays the fallback named
  above.

## Design

Every item is a `Shader_key` axis or a device capability, never a Quest
`#ifdef`: the desktop editor renders the mobile variant on request, so its
gates run headless on the desktop against the full path, and the OpenXR
graphics preset selects the mobile values (`doc/erhe/shader_variants.md`
"Variant key dimensions").

### M1 Mobile shadow lookup: directional and spot lights

Built (`doc/erhe/shadows.md` "Normal offset bias"): the third
`Shadow_bias_mode`, `normal_offset`, selected by the preset's `shadow_bias`
field (compile-time axis `ERHE_SHADOW_BIAS`), the OpenXR preset's value.
Measured on the Adreno 740 (hard filter, 16-bit depth, one shadowed
directional light, isotropic, untextured): 637 to 367 instructions, 17 to
12 registers, 62 % to 100 % occupancy; the shadowed spot 430 and point 358
(were 694 and 748); the base-color textured variant 664 to 392, 17 to 13
registers, 50 % to 75 % occupancy; directional + spot + point 1617 to 893.
Desktop gates at 16-bit depth with the offsets 1.0 / 1.0: no acne on any
station or light type (G1), contact attached within 0.08 - 1.25 texels
(G3), thin walls pass (G4), edges within a texel (G5; the spot up to 2.5),
and the hardware filter's partial values just inside shadow edges (G2,
0.01 - 1.2 % of the pixels), the mode's softening. Smaller offsets were
measured too: 1.0 / 0.25 shows 0.4 - 0.6 % acne on the head-on floors
and 0.5 / 0 shows 58 - 66 %, so the constant offset is what the head-on
receiver needs (its sine term is zero there). The design:

1. the receiver point moved along the interpolated shading normal N by
   `k_offset * texel_world * (1 - N.L)` (normal offset shadows), where
   `texel_world` is the world size of one shadow texel at the receiver: a
   per-light constant for a directional light, `texel_unit * distance to
   the light` for a spot light; `texel_unit` is written into the hot record
   (`Light_shadowed`, a new vec4 `mobile_bias`: texel size at unit depth,
   constant depth bias, slope depth bias, unused), computed once per light
   by `Light_buffer` from the light's projection;
2. the texture-space position from `texture_from_view_relative` (as now),
   the reference depth moved toward the light by `constant + slope *
   min(tan(theta), tan_max)` in depth units of the map, `theta` the angle
   between N and L (clamped with `tan_max` 8 so grazing receivers do not
   lift the whole map);
3. one `texture(s_shadow_compare, vec4(uv, layer, reference))`: with a
   linear compare sampler this is the hardware's 2x2 percentage-closer
   filter in one texture instruction (smoother than the hard filter at a
   lower cost); with nearest it is the hard filter; the preset's
   `shadow_filter` `pcf_2x2` value on this path selects Filament's four-tap
   3x3 gaussian over the hardware filter. The sampler state of
   `s_shadow_compare` on the mobile path is a preset decision (Open).

The spot light's cone test takes Filament's form, `saturate(cos * scale +
offset)^2` with `scale` and `offset` per light in the record
(`direction_and_outer_spot_cos.w` and `position_and_inner_spot_cos.w`
become the pair), so no vector is normalized per fragment for the cone.
The hot record's radiance is written pre-exposed for the mobile
accumulation.

Nothing of the receiver plane, the D vectors, the rounding bounds or the
texel selection is compiled: `v_position_rounding` and the receiver plane
derivatives are not read, so the vertex shader drops that varying on the
mobile key too. Expected cost: about 340 instructions for the one-light
base variant (the plain tap row plus the offset and bias), half of today.

Boundary: a receiver facing the light within `tan_max` renders without
acne; contact shadows detach by up to `k_offset` texels; a wall thinner than
the normal offset at its distance can leak. The constants start at the
values the shadow stations pass with (`k_offset` 1.5 texels, constant 2
depth ulps, slope 1 texel of depth) and are preset fields, so the user can
tune them in the Settings window.

### M2 Mobile shadow lookup: point lights

`sample_point_light_visibility()` gets the same mode: the normal-offset
receiver point, the stored-value bias from the cube's texel size at the
receiver's distance (`texel_unit * distance`), and a single compare fetch
of the cube layer instead of the lookup's tap geometry. The cube stores
what the compare needs (depth or distance) as it does today; the step
checks which and sizes the bias in that unit. Expected: the point row from
748 to about 330.

### M3 The fp16 chain of the mobile variant

Step 0 measured the existing paths P1 - P5 on the device: slower on the
default scene, level on Bistro street, so the 16-bit ALU rate does not pay
for the boundary conversions on this GPU, and a whole fp16 chain has no
measured case for it. What fp16 does buy is full-precision registers: on
the mobile textured variant the material path alone (mask 1) takes 13
full registers to 11 (+22 instructions) and the occupancy from 75 % to
100 %, where M5 could not (the shadow lookup is already one fetch, and
the remaining registers hold the frame, the view vector and the
accumulated color). The step therefore is: `shader_fp16_paths` 1 in the
OpenXR preset once the device bench shows it level or better; its
rendering difference is the fp16 plan's P1 boundary (roughness floor
0.09, 8-bit inputs exact). The whole-chain design below stays as the
fallback if a later device measurement shows the fragment ALU bound:

- the vectors N, V, L and H in fp16 after their fp32 normalization (the
  2^-11 angular error that P2 excluded on the desktop; the roughness floor
  0.09 of P1 bounds the highlight flank error to the measured 13 - 76 % on
  roughness below 0.09 only, which the floor removes);
- the shadow lookup's visibility, the attenuation and the accumulation as
  P3 - P5 already do, with the exposure folded into the accumulation;
- the texture transform and the normal map decode in fp16 (texcoords up to
  the 2048-texel bound of the fp16 plan's P8 statement; the OpenXR preset
  is inside it);
- the output clamp at 1.0 (`c_output_max` under multiview) stays as it is.

The gate is the fp16 plan's station matrix with the mobile tolerance
(below), and the device bench. The desktop paths and their boundaries do
not change.

### M4 fp16 material record storage

The Quest reports `storageBuffer16BitAccess` and not
`uniformAndStorageBuffer16BitAccess`, so 16-bit members are legal in the
storage blocks (the material block, the shadowed light array) and not in
the uniform blocks (lights, camera). `Shader_resource` gains the member
types `float16_t`, `f16vec2` and `f16vec4` (std430 alignment 2, 4 and 8
bytes) behind `Device_info::storage_buffer_16bit_access`, and
`Material_interface` builds one of two layouts at startup from that flag
(one layout per device, no runtime branch): the 16-bit `Material` packs
roughness.xy, metallic and reflectance into one `f16vec4`; base color,
emissive (HDR, below 65504), the four scalar inputs (opacity, normal scale,
alpha cutoff, occlusion strength), ior and transmission, the five rotation /
scale vec4 and the two decode vec4 as halves, the five offsets as `f16vec2`;
the handles, the channel selectors, the input sources and the BxDF model
stay 32-bit. The record goes from 304 to about 176 bytes and the untextured
variant's record reads from 3 to 2, the textured one's from about 12 to 7.
The writer packs with `glm::packHalf1x16`; every reader of
`material.materials[]` (`standard.frag`, `brdf_slice.frag`, the ray trace
compute shaders, `textured.vert`) reads through the `mat_*` types so the
16-bit layout needs no shader change beyond the struct. Boundary: texture
offsets and scales round to 2^-11 of their magnitude, within the P8 texcoord
bound; the desktop keeps the fp32 layout.

This is the smallest item of the plan by the static count (about 5
memory instructions of 772 on the textured variant) and the device bench
decides whether it stays; it is kept in the order because the textured
record's transform fields are what it shrinks, and Bistro's materials all
carry textures.

### M5 Identity texture transform axis

Built: the material sub-key boolean `IDENTITY_TEXTURE_TRANSFORM`
(`doc/erhe/shader_variants.md` "Material sub-key"), set when every bound
sampler has rotation 0, scale 1 and offset 0 (glTF without
`KHR_texture_transform`); `ERHE_SAMPLE_MATERIAL_TEXTURE` in `standard.frag`
then calls the untransformed `sample_texture()` overload and the record's
transform fields are not read. Exact on every platform. Measured on the
base-color textured mobile variant: 4 instructions and 2 record reads, no
register (13 stays 13; the receiver-plane estimate of 12 instructions, 4
reads and 2 registers came from a normal-mapped variant with two textured
slots, where the saving is per slot).

### M6 The slides' BRDF

`isotropic_brdf()` under the mobile bit: GGX distribution with the
`|N x H|^2` form and Filament's 453.5 clamp, Hammon's visibility
(`V_SmithGGXCorrelated_Fast`, no square roots) or Hable's
`1 / (4 (L.H)^2 (alpha + 0.5))`, Filament's `f + f0 (1 - f)` Schlick with
`pow5`, the lobe as one division; the diffuse keeps its `1 - F` factor. Measured 10 instructions and 2 complex
ops; taken only if the device bench after M3 still shows the eye pass
ALU-bound.

## Gates

- **G-cost** (offline, every step): the offline compiler statistics
  of the five preset variants of the table (base, textured, spot, point,
  the maxima) report instructions, registers and occupancy; a step lands
  only when no row rises and the rows it targets fall by what the design
  states.
- **G-visual** (desktop headless, every step): the mobile axes render on
  the desktop editor, so `scripts/shader_fp16_verify.py` runs its station
  matrix with the mobile mask against the full path, and
  `scripts/shadow_verify.py` runs the shadow stations with
  `shadow_bias=normal_offset`. The mobile tolerance is: G-finite as it is;
  G-display at 16/255 for 99 % of the pixels and no connected region of
  more than 64 pixels above it; G-dark as it is; on the shadow stations
  zero acne pixels on the lit planes of `contact_blocks` and `cornell`,
  contact detachment of at most `k_offset` texels, leaks only through
  `thin_walls` thinner than the normal offset at their distance.
- **G-device** (the Quest, each step that changes the headset variant):
  `scripts/quest_bench.py` on the default scene and Bistro street, app GPU
  time and fps before and after at the same build and `input_hash`
  (`doc/agents/quest.md` "Bench configurations and the run record"), then
  the user's worn check of the step's scene.

## Build order

0. **Device baseline of the existing fp16 paths** (done; the M1 device
   bench waits for the APK reinstall, the installed APK carries another
   machine's signing key):
   `shader_fp16_paths` 0 against 31 with the release APK of 2026-11-07,
   off head, 120 Hz. Default scene (`benchmark_orbit`, two runs each): mask
   31 is slower, the XR multiview pass 7.96 ms against 7.28 ms (medians;
   the mask 0 pair spread 12 %, the mask 31 pair 0.2 %), app GPU frame 6.27
   against 5.65 ms, 106.5 against 115 fps. Bistro street (single
   directional light, GPU occlusion culling, one run each, in
   `doc/reference/quest_bench_runs.md`): level, 58.4 against 58.9 fps, app
   GPU 12.1 against 12.3 ms. So P1 - P5 stay out of the OpenXR preset: a chain that converts at
   every path boundary costs more on Adreno than its 16-bit rate returns,
   and M3's chain has to be whole (vectors, lookup and accumulation in
   fp16 with one conversion at the texture fetches and one at the output).
1. **M1** (done but for the device bench): the axis value, the record
   field, the lookup, the preset fields, the desktop gates.
2. **M2** (done with M1) point lights, the same gates.
3. **M5** (done) identity texture transform (exact, every platform).
4. **M3** as reduced above: P1 in the OpenXR preset after its device
   bench.
5. **M4** the 16-bit material record, the device bench decides.
6. **M6** if the eye pass is still ALU-bound.
7. **Preset**: `graphics_presets_openxr.json` carries `shadow_bias`
   `normal_offset` (done) and takes `shader_fp16_paths` 1 after its
   bench; the desktop presets are unchanged.

## Open

- Where the normal offset and the light-space projection run: in the
  fragment shader (this plan's default: no varyings added to the binning
  pass) or in the vertex shader as Filament does (one vec4 varying per
  shadow light, about 20 fragment instructions per light saved); the device
  bench of M1 measures both on Bistro.
- The compare sampler of the mobile path: linear (hardware 2x2 filter, one
  instruction, soft edges) or nearest (the preset's `hard` look). Linear
  is proposed.
- The acceptance constants of the shadow stations in mobile mode
  (`k_offset`, the depth bias pair) and whether they are per preset or per
  light.
- Whether the mobile shadow path also serves the desktop "low" preset, or
  stays a headset setting.
- The remaining desktop fp16 items (`doc/plans/shader_fp16.md` R2, R3,
  P6 - P8) are not on this plan's path: with M1 the headset variant has no
  receiver plane to budget registers for, so R2's per-light orthographic
  constants are a desktop-only item.
