# Quest 3 GPU profiling, 2026-05-01

## Setup

- **Headset**: Quest 3 (Adreno 740), serial `2G0YC1ZF7X10LS`
- **APK**: `org.libsdl.app.quest`, **release** flavor, built 2026-05-01 19:06
  - Built via `scripts\build_android.bat quest assembleQuestRelease`
  - Installed via `scripts\install_android.bat quest release` (no auto-launch)
  - Launched via the immersive VR intent
    (`am start -W -a MAIN -c LAUNCHER -c com.oculus.intent.category.VR -n org.libsdl.app.quest/org.libsdl.app.ErheActivity`)
- **Tooling**: `ovrgpuprofiler` real-time metrics, no detailed mode (avoids the
  documented ~10 % overhead). Two 30 s captures, first 2 samples dropped
  per-metric, n=27 per metric.
  - Iteration 1: broad bottleneck screen, 26 metrics.
    `D:\erhe\profiling\20260501-191042-iter1-broad.txt` and
    `20260501-191042-iter1-analysis.md`
  - Iteration 2: focused on filter mix and mip levels.
    `D:\erhe\profiling\20260501-191448-iter2-textures.txt`
- **Workload**: editor running release APK, user-chosen heaviest view.

## Hypothesis going in

Fragment-shader ALU bound. Justification for evaluating: the recently-landed
`shaderFloat16` / `GL_EXT_shader_16bit_storage` / `erhe_half.glsl` work would
be the natural follow-up if confirmed.

## Result

**Hypothesis refuted.** The bottleneck is **texture-fetch stall, driven by
mips never being sampled.** Confidence: high (across two independent
captures, with very tight intra-capture variance).

## Headline numbers

| Metric | Iter 1 mean | Iter 2 mean |
|---|---:|---:|
| `% Time Shading Fragments` | 99.79 % | 99.79 % |
| `% Texture Fetch Stall` | **70.56 %** | **70.46 %** |
| `% Texture Pipes Busy` | 88.70 % | 89.05 % |
| `% Shader ALU Capacity Utilized` | **39.40 %** | not captured |
| `% Time ALUs Working` | 41.23 % | not captured |
| `Textures / Fragment` | 5.20 | 5.42 |
| `% Texture L1 Miss` | 21.23 % | 21.95 % |
| `% Texture L2 Miss` | 0.10 % | 0.73 % |
| `% Non-Base Level Textures` | not captured | **0.00 %** (zero variance) |
| `% Nearest Filtered` | not captured | **87.98 %** |
| `% Linear Filtered` | not captured | 12.01 % |
| `% Anisotropic Filtered` | not captured | 0.00 % |
| `% Time Shading Vertices` | 0.16 % | not captured |
| `% LRZ Busy` / `% LRZ Pixels Killed` | 94.70 / 6.18 % | not captured |
| `Frag ALU Inst/s (Half)` / `(Full)` | 36.4 G / 280.5 G | not captured |

## Diagnosis

The fragment stage owns 99.8 % of GPU time (vertex stage is essentially free
at 0.16 %). Inside fragment, the cores are stalled on texture fetches 70 % of
the time, and the ALUs are only running at ~40 % of capacity. If we were ALU
bound, ALU capacity would be in the 70-90 % range and the texture-fetch-stall
percent would be small.

The two captures together attribute the texture pressure to a specific cause:
**`% Non-Base Level Textures = 0 %` with zero variance across 27 samples.**
Every fragment texture sample reads from the base-level mip. Two ways this
can happen on Vulkan:

1. Textures are created with `mip_level_count = 1` (no mip pyramid in the
   image), or mip levels exist but are not uploaded.
2. Samplers force LOD selection to base — `min_lod = max_lod = 0`,
   `mip_filter = nearest` and the LOD calculation gets clamped, or a hard
   `lod_bias` of -16+ that always rounds to 0.

When base-level is always sampled, fragments that should be reading 1 / 4 / 16
texels per screen pixel (mip 1 / 2 / 4) are instead reading 1 / 16 / 256
texels per screen pixel. This has two cascading effects:
- Cache locality collapses: adjacent screen-space fragments touch
  far-apart texel addresses on distant geometry. → 21 % L1 miss.
- Bandwidth and texture pipe pressure go up: → 89 % `% Texture Pipes Busy`,
  70 % `% Texture Fetch Stall`.

The 88 % nearest-filter dominance compounds the visual cost (aliasing on
distant content) and is a cache-locality multiplier in its own right, but the
mip issue is the more impactful of the two — linear filtering on a properly
mipped sample reads 4 texels from a small neighborhood (cache-friendly);
nearest filtering on the *base* level reads 1 texel that is potentially far
from the neighbors.

## Implications for the just-landed fp16 / 16-bit-storage work

Hold the rollout. The `ERHE_HAS_SHADER_FLOAT16` and `GL_EXT_shader_16bit_storage`
preamble is correct and the device-feature plumbing is sound — but applying it
to fragment shaders in this workload would shave the small unstall-led ALU
slice while the texture-fetch-stall wall stays put. Estimated frame-time
reduction is in the low single digits at best.

The infrastructure is still worth keeping in place — once the texture-stall
floor drops, we may shift back into ALU-bound territory and the rollout will
matter.

## Recommended next investigations, in priority order

### 1. Find out why no mips are being sampled (highest impact)

Two-track investigation. Whichever finishes first answers the question.

**Track A — texture creation.** Audit Vulkan texture creation in
`src\erhe\graphics\erhe_graphics\vulkan\vulkan_texture.cpp`. For a 2D image
loaded for sampling, `VkImageCreateInfo::mipLevels` should be
`floor(log2(max(width, height))) + 1`. Check:
- Are textures being created with `mipLevels = 1`?
- If `mipLevels > 1`, are the levels actually populated (uploaded via
  `vkCmdCopyBufferToImage` per level, or generated via `vkCmdBlitImage`)?
- Is `imageView` created with `subresourceRange.levelCount` covering the
  whole pyramid?

**Track B — sampler config.** Audit Vulkan sampler creation in
`src\erhe\graphics\erhe_graphics\vulkan\vulkan_sampler.cpp`. For mip-aware
sampling, `VkSamplerCreateInfo` needs:
- `mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR` (or NEAREST), not absent
- `minLod = 0`, `maxLod = VK_LOD_CLAMP_NONE` (or a large finite value)
- `mipLodBias = 0`
- The default `Sampler` constructor in the abstraction should not be
  overriding these to 0.

A quick way to discriminate: capture again after editing one specific
sampler config to force linear mip selection — if `% Non-Base Level Textures`
moves off 0 %, the sampler path is the culprit; if it stays at 0 %, the
texture path is the culprit.

Reference: existing GPU timer infrastructure in
`src\erhe\graphics\erhe_graphics\gpu_timer.hpp` (+ Vulkan backend) gives
per-render-pass cost; once the fix lands, compare per-pass GPU times before
and after to validate.

### 2. Audit nearest-vs-linear filtering distribution

88 % nearest is suspicious for a 3D editor. Likely contributors, in
descending probability:
- ImGui font / panel sampling (nearest is correct for pixel-perfect UI but
  should NOT dominate in a 3D viewport mode). Check ImGui sampler setup in
  `src\erhe\imgui\erhe_imgui\imgui_renderer.cpp`-equivalent.
- Default sampler in the editor's content library set to nearest.
- Specific debug textures (ID-buffer picking, debug overlays).

Even if mip pyramids are restored, every additional texel a nearest sample
fetches still costs an L1 lookup; switching to linear+mip everywhere a
real-world surface is sampled is the right shape long term.

### 3. FFR (Fixed Foveated Rendering) bump

With fragment-stage at 99.8 %, FFR is the most direct lever to reduce
per-eye fragment shading work without touching content. Try
`XR_FB_foveation_configuration` (or whatever erhe currently uses) at one step
above current. Cheap test, immediate frame-time delta.

### 4. Texture compression audit

Verify all sampled textures are BC7 / ASTC and not uncompressed RGBA. With
no mips, base-level reads are 4-8x more bandwidth than they should be;
without compression on top, that is another 2-4x. Once mips are restored,
compression becomes a smaller win, but it is still worth verifying.

### 5. (Deferred) Revisit fp16 / 16-bit-storage rollout

After the mip / sampler fix lands, recapture iter-1 metrics. If
`% Texture Fetch Stall` drops below ~30 % and `% Shader ALU Capacity
Utilized` rises above ~60 %, the workload has shifted to ALU-bound and
fragment-shader fp16 becomes the right next move. Until then, hold.

## Reproducing

```cmd
:: build
scripts\build_android.bat quest assembleQuestRelease

:: install (no auto-launch)
scripts\install_android.bat quest release

:: launch (immersive VR)
adb shell am start -W -a android.intent.action.MAIN ^
  -c android.intent.category.LAUNCHER -c com.oculus.intent.category.VR ^
  -n org.libsdl.app.quest/org.libsdl.app.ErheActivity

:: iteration 1: broad screen (26 metrics, 30 s)
adb shell timeout 30 ovrgpuprofiler -r"3,6,7,8,9,10,16,17,18,19,20,21,22,23,24,29,31,34,35,37,38,40,64,78,79,80"

:: iteration 2: filter / mip focus (11 metrics, 30 s)
adb shell timeout 30 ovrgpuprofiler -r"7,8,9,21,31,37,38,69,70,71,72"

:: full metric ID list for this device
adb shell ovrgpuprofiler -m
```

Metric IDs are device- and runtime-specific. The IDs above are valid for the
Quest 3 with the current Horizon OS build (81 supported metrics). Re-run
`ovrgpuprofiler -m` if reproducing on a different device/runtime.
