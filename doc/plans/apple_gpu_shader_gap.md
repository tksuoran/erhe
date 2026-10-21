# Apple GPU: the standard shader's cost through SPIRV-Cross vs KosmicKrisp

Status: blocked

Metal is fixed; MoltenVK waits for the SPIRV-Cross change to reach an
upstream release and a MoltenVK built with it ("Remaining" below).

On an Apple GPU the editor's forward pass costs about 3x more GPU time
through the Metal backend and through MoltenVK (both SPIRV-Cross MSL, then
Apple's compiler) than through KosmicKrisp (Mesa; its own NIR-based
compilation to Metal) for the same scene and the same image. This plan
finds the cause by comparing the final shader code of the two paths on a
small repro scene instead of Bistro.

## Root cause (found 2026-10-21)

The whole gap is SPIRV-Cross's MSL lowering of GLSL `precise`. The two
`precise` additions in `get_light_relative_position()`
(`res/shaders/erhe_light.glsl`, used by the spot / directional shadow lookup
and the point shadow lookup) become SPIR-V `NoContraction` operations, and
SPIRV-Cross emits every `NoContraction` add / sub / mul as a call to a
helper declared `[[clang::optnone]]` (`spvFAdd`, `spvFSub`, `spvFMul`, ...;
`spirv_msl.cpp` `SPVFuncImplFAdd`, unchanged on upstream `main` as of
2026-10-21; there is no option to avoid it). Apple's compiler cannot inline
an `optnone` function, so each shadow lookup in the light loops makes real
GPU function calls. KosmicKrisp keeps the same operations exact with
scoped `{ #pragma METAL fp math_mode(safe) ... }` blocks inline
(`MESA_KK_DEBUG=msl` output), which cost nothing.

Evidence, Bistro with the `bistro_frame_bench.py` camera, Viewport pass
GPU timer, GPU saturated (`frame_pacing_tier="off"`,
`force_disable_vsync=true`), median of 12 one-second samples:

| Viewport pass, ms | Metal | MoltenVK | KosmicKrisp |
|---|---|---|---|
| HEAD | 26.6 | 25.1 | 5.9 |
| shadow lookups replaced by 1.0 | 4.8 | - | 4.55 |
| `precise` removed from `get_light_relative_position()` | 6.2 | 6.0 | - |
| HEAD, Metal MSL helpers rewritten to inline `math_mode(safe)` | 6.0 | - | - |

The last row keeps `precise` and the exactness: the helpers become
`inline T spvFAdd(T l, T r) { #pragma METAL fp math_mode(safe) return l + r; }`
(and `spvFSub` alike) in the Metal backend's MSL before `newLibrary`, and
Bistro `render_scene_image` is byte-identical to HEAD (c08a8600). Rejected
on the way (no effect or slower on Metal): reading `Light` fields in place
instead of the whole-struct copy SPIRV-Cross emits, non-unrollable light
and PCF loops, a constant shadow map size instead of `textureSize`, one
gather instead of four.

How the shaders were obtained: the Metal backend logs every MSL in
`logs/log.txt`; KosmicKrisp dumps its MSL with `MESA_KK_DEBUG=msl
MESA_LOG=file MESA_LOG_FILE=<path>` (Mesa does not log to stderr by default
on macOS), and only for pipelines it compiles: move the KosmicKrisp file
in `pipeline_cache/` aside for the run (with it, pipelines are
deserialized and nothing is dumped).

Fix (2026-10-21): the SPIRV-Cross fork `tksuoran/SPIRV-Cross`, branch
`msl-inline-precise-helpers` (pinned in `CMakeLists.txt`), emits the
helpers without `optnone` and with `#pragma metal fp math_mode(safe)` /
`#pragma metal fp contract(off)` from MSL 3.2, and the Metal backend
compiles MSL 3.2 (`doc/erhe/metal_backend.md` "Precise arithmetic"). Metal
Viewport pass 26.6 -> 5.5 ms (KosmicKrisp 5.9 ms). Bistro
`render_scene_image` differs from HEAD in one pixel by one LSB (the
precise multiply and matrix helpers of the vertex shader, now inlined);
upstream SPIRV-Cross `main` without the change renders HEAD's image.
Metal ctest 1765/1765 including the shadow head-on tests. After a display resolution change (smaller
viewport) the pinned build measures Metal 3.7 ms vs KosmicKrisp 3.95 ms.

Remaining:
- Verified on the fork commit: SPIRV-Cross's 13 reference suites
  of `test_shaders.sh` pass (SDK glslang / SPIRV-Tools, MSL references
  compiled by the Metal toolchain); the `tests-other/` unit tests were not
  run (they need SPIRV-Cross's own `external/` glslang and SPIRV-Tools
  builds) and upstream CI runs them on the pull request. Upstream `main`
  lacks the reference `texture-shadow-lod.vk.frag.vk` (plain and opt);
  `test_shaders.py` writes it on every run - delete it, it is unrelated.
- Upstream pull request from the fork branch (the user opens it; the
  commit message of 5dbbf6da is its description).
- MoltenVK keeps the optnone helpers (its bundled SPIRV-Cross) until the
  change is upstream and in a MoltenVK release: an upstream pull request
  from the fork branch. A `precise`-free formulation of
  `get_light_relative_position()` would fix MoltenVK without that, at the
  cost of its own correctness argument and the shadow verification gates.
- Return to `KhronosGroup/SPIRV-Cross` once a release has the change.

## What is measured (2026-10-21, M4 Pro, Release, windowed, Dell 120 Hz)

Bistro (`scripts/bistro_frame_bench.py`, default camera of the script),
GPU service time per frame from the frame pacing records, after c8dc0080c
(spot / point lights skipped beyond their range):

| | Metal | MoltenVK | KosmicKrisp |
|---|---|---|---|
| GPU per frame | 40 ms | 35 ms | 13.4 ms |
| before c8dc0080c | 123 ms | 108 ms | 18 ms |

`get_gpu_timers` "Viewport window" (the forward pass) with lights deleted
in the running editor (`delete_nodes`), editor default camera after
`open_scene`, before c8dc0080c:

| Viewport pass, ms | Metal | MoltenVK | KosmicKrisp |
|---|---|---|---|
| 43 lights | 130.8 | 135.6 | 4.8 |
| 8 shadow-mapped lights only | 83.4 | 75.8 | 4.4 |
| no lights | 6.5 | 8.2 | 3.6 |
| shader debug mode 7 (texcoord_0) | 8.5 | - | - |

After c8dc0080c: 43 lights 27.5 / 28.6 / 3.6 ms; no lights 12.7 / 9.9 /
3.55 ms. The pass timers run at whatever GPU clock the governor picks
(the no-lights numbers differ between the two tables for that reason), so
compare drivers only at the same load and, where possible, with the GPU
saturated (see Method).

So the gap has two parts: the light loops (most of it, the per-light cost
is far higher on the SPIRV-Cross path even with the range early-out), and
a smaller part without any lights (base material shading, texture heap
access).

## Scene facts the repro must reproduce

Bistro's lights (43): 29 point + 4 shadow-mapped point, 6 spot + 4
shadow-mapped spot, all range 15 m, no directional light. These give the
standard shader variant its light counts (`ERHE_LIGHT_COUNT_*`,
`doc/erhe/shader_variants.md`): the loops are unrolled at compile time. The
raw `get_scene_lights` reply (colors, intensities, cone angles, shadow
indices) and a `get_draw_lists` reply (129 draw lists, 7576 entries,
2683 objects) are saved per machine in `memory-bank/local/bistro_lights_2026_10_21.json`
and `memory-bank/local/bistro_draw_lists_2026_10_21.json`; regenerate
them with those MCP tools after `open_scene` of
`res/editor/assets/bistro_lights_compressed/bistro_lights_c.gltf` if
missing. Bistro materials are textured PBR (base color, normal,
metallic-roughness, some emissive) through the texture heap.

## Method

1. **Repro scene.** A creation-style script (`scripts/creations/`
   conventions, `erhe-creations` skill) that builds a small scene with the
   same light set - 33 point (4 shadow-mapped, the first ones) and 10 spot
   (4 shadow-mapped), range 15 m, placed so a floor and a few boxes lie
   inside several ranges - and a few textured PBR materials, so the
   standard shader's Shader_key (light counts, material axes) matches a
   Bistro variant. Verify by comparing the variant's defines in the log
   (the Metal backend logs every compiled MSL, see 2) with a Bistro run.
   Confirm the gap reproduces: Viewport pass on Metal vs KosmicKrisp.
2. **Get both shaders.**
   - Metal backend: `logs/log.txt` holds every MSL it compiles ("Fragment
     MSL for 'standard' compiled successfully (N bytes):", followed by
     the source; `metal_shader_stages_prototype.cpp`). MoltenVK runs the
     same SPIRV-Cross; its MSL can be dumped by MoltenVK's shader logging
     configuration (check the MoltenVK configuration documentation in the
     SDK for the current variable names).
   - KosmicKrisp: find out how Mesa's KosmicKrisp driver produces Metal
     code (MSL text from NIR, or a direct path) and which debug
     environment variable dumps it or its NIR (Mesa source,
     `src/kosmickrisp/`; Mesa's usual `*_DEBUG` variables and
     `NIR_DEBUG=print`).
   - If the KosmicKrisp code cannot be dumped: compare at the GPU level
     instead - an Xcode GPU frame capture (works on any Metal process,
     KosmicKrisp and MoltenVK included) shows per-pipeline and per-draw
     shader cost, register usage, occupancy and the compiled shader; or
     the Instruments shader profiler (the Metal System Trace template's
     `gpu-shader-profiler-*` tables came back empty without enabling
     shader profiling).
3. **Compare** the fragment shader of the matching variant: unrolled vs
   looping light code, branches kept vs flattened (`select`), texture heap
   access (SPIRV-Cross: `constant array<texture2d<float>, 4096>&
   s_texture` argument buffer indexing), precision (half vs float),
   register pressure / spilling, helper calls inlined or not.
4. **Isolate** with shader edits in `res/shaders/standard.frag` (loaded at
   startup; no rebuild): e.g. a non-unrolled loop over a uniform light
   count, the shadow lookup removed, a fixed texture index. Each A/B on
   both drivers in the repro scene; keep only root-cause fixes, and check
   images byte-identical where the change must not alter output
   (`memory-bank/local/metal_gates/bistro_img.py` style
   `render_scene_image` comparison).

Measuring: saturate the GPU so the clock is at its maximum and the pass
timers are comparable - `frame_pacing_tier="off"` plus
`force_disable_vsync=true` in `config/editor/erhe_graphics.json` (restore
afterwards), or read the clock ratio from the frame pacing records
(`get_frame_pacing_frames` `gpu_clock_ratio`, Metal and MoltenVK only).
KosmicKrisp's timestamp behaviour differs from MoltenVK's
(`doc/agents/macos.md`); cross-check its pass timers against its
GPU-bound frame interval. Instruments' Metal System Trace fixes a GPU
performance state: do not take absolute durations from it.

## Tools

- Builds: `build_xcode_metal_tracy`, `build_xcode_vulkan_tracy` (Release,
  editor only); `VK_DRIVER_FILES=<ICD directory>/MoltenVK_icd.json`
  or `<ICD directory>/libkosmickrisp_icd.json` selects the Vulkan driver
  (`doc/agents/macos.md`).
- MCP: `get_gpu_timers`, `get_scene_lights`, `delete_nodes`,
  `create_light`, `edit_light`, `push_shader_debug` / `pop_shader_debug`,
  `set_draw_lists_enabled` (classic forward path: 76 ms vs 135 ms on Metal
  before c8dc0080c - also worth explaining), `render_scene_image`.
- `scripts/bistro_frame_bench.py` for the frame-level numbers on Bistro
  (`ERHE_TRACY_DIR` = a directory with `tracy-capture` and
  `tracy-csvexport`).
