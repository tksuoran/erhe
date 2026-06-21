# Procedural sky -- status & handoff

This is the working/handoff doc for the physically-based procedural sky
(Hillaire atmosphere) work. The full design + per-file detail is in
[`doc/procedural_sky.md`](procedural_sky.md); the SDL3-GPU<->erhe API mapping
(including the new storage-image compute rows) is in
[`doc/forge-erhe.md`](forge-erhe.md). This file captures **what is done**, the
**one open bug** (atmosphere renders nothing visible), and the **plan to finish
verifying + fixing** in a fresh session with more tooling.

## Status (one line)

Implemented and **building clean** (Vulkan + OpenGL + headless editors link);
all sky shaders **compile at runtime**; atmosphere mode (`Sky_config::mode == 1`)
**runs without crashing**, BUT **renders nothing visible** (only the clear
color). Verification is WIP and a fix is still needed.

## What was built (branch `ls/main`)

Ported the forge-gpu `forge-procedural-sky` skill (Hillaire EGSR 2020) as a
second, selectable sky mode alongside the existing checker/gradient sky
(`Sky_config::mode`: 0 = gradient, 1 = atmosphere). Reuses erhe's existing HDR +
bloom + tonemapping unchanged. Two LUTs (transmittance 256x64, multi-scatter
32x32) are generated once at startup by compute shaders writing storage images;
the atmosphere fragment ray march samples them.

Commits (newest last):

- `f6d1a4b7` graphics: add storage-image compute support -- the foundational,
  reusable bit. erhe compute previously supported only SSBOs; this adds
  `Binding_type::storage_image`, `Glsl_type::image_2d` GLSL emission,
  `Compute_command_encoder::set_storage_image`, and the Vulkan `STORAGE_IMAGE`
  descriptor + `GENERAL`-layout path. GL/Metal/Null are compile-only no-ops, so
  **atmosphere is Vulkan-only** (gradient sky is used on other backends).
- `7d7b453d` editor: add physically-based procedural sky (Hillaire atmosphere)
  -- `Sky_renderer` (LUTs + compute pipelines + atmosphere pipeline + a reused
  `Camera_buffer`), `Sky_composition_pass` (mode switch), camera-UBO
  `Sky_parameters` additions (sun_direction + atmosphere), `sky_config` v3
  (mode + knobs), shaders, and the `ensure_luts` hooks before the viewport /
  headset render passes.
- `1e688940` fix: declare the `Camera` struct type in the atmosphere shader
  (was a runtime GLSL "unexpected IDENTIFIER" on the camera UBO block).
- `207b1036` fix: release the camera `Ring_buffer_range` in `render_atmosphere`
  (its dtor asserts released/cancelled; `update_views` only closes it).
- `354b1e6a` instrument for verification: RenderDoc debug groups + one-time logs
  (see "Diagnostics in place").

## The open bug: atmosphere draws but nothing is visible

Symptom: with Sky Mode = 1, the viewport shows only the clear color; in RenderDoc
no sky draw was visible (the draw had no debug scope -- now added).

What is already known (do not re-derive):

- The atmosphere shaders **compile** -- `logs/log.txt` shows "Creating shader
  module: sky_transmittance_lut compute / sky_multiscatter_lut compute /
  sky_atmosphere vertex / sky_atmosphere fragment" with no error.
- `render_atmosphere` **is reached and the draw IS issued**. Proof: an earlier
  `Ring_buffer_range` dtor assert fired *inside* `render_atmosphere`, on the
  callstack *after* `draw_primitives`. So `is_atmosphere_supported()` is true,
  `m_luts_ready` is true (ensure_luts ran), the pipeline is non-null, and
  `draw_primitives` executed. (That assert is now fixed.)
- Therefore the draw runs but the output is black/invisible.

Leading hypothesis (most likely): **the LUT textures are all zero** because the
brand-new storage-image compute write is not actually landing (descriptor /
image-view / layout issue in `set_storage_image`, or the dispatch not running).
With transmittance == 0 the fragment ray march produces 0 -> black sky -> reads
as clear color.

Alternatives to rule out:

- LUTs are written but the **atmosphere math / exposure** is ~0 (too dark).
- The fullscreen draw is **depth/stencil rejected** (less likely -- it uses the
  exact same far-plane depth==EQUAL + stencil==0 state as the working gradient
  sky, in the same render pass).

## Diagnostics in place (committed, build & run mode 1)

In `src/editor/renderers/sky_renderer.cpp`:

- `ensure_luts`: logs `Sky_renderer::ensure_luts: generating atmosphere LUTs`
  and wraps the two compute dispatches in a `"Sky LUT generation"` debug group.
- `render_atmosphere`: a one-time log
  `render_atmosphere first call: supported=.. luts_ready=.. encoder=.. render_pass=.. views=..`,
  a `warn` if the pipeline is null, and a `"Sky atmosphere"` debug group around
  the draw.

So a RenderDoc capture in mode 1 should now show a `Sky LUT generation` compute
scope and a `Sky atmosphere` draw scope, and `logs/log.txt` should show the two
`Sky_renderer::` lines.

## Plan to finish (fresh context + more tools)

1. **Get a GPU-level view.** Two options, ideally both:
   - **Visual Studio MCP server** -- it was NOT connected this session
     (`mcp__visualstudio__*` tools absent). Start it (VS: Tools -> MCP Server ->
     Start Server) and reconnect Claude Code, then breakpoint `ensure_luts` and
     `render_atmosphere`, step the first frames, inspect `m_luts_ready`, the
     pipeline, and the draw.
   - **Vulkan validation layer** -- the single most useful tool here: it will
     immediately flag a STORAGE_IMAGE descriptor mismatch, a wrong image layout
     at dispatch/sample, or a missing barrier. NOTE: enabling
     `vulkan_validation_layers` previously broke `vkCreateInstance` on this
     machine (crowded layer stack, per CLAUDE.md). If the new tooling fixes
     that, turn it on for this debug.
   - **RenderDoc** -- decisive even without the above: capture a mode-1 frame,
     then:
     - open the `Sky LUT generation` dispatch outputs `i_transmittance`
       (256x64) and `i_multiscatter` (32x32): are they smooth gradients or
       all-zero/black?
     - select the `Sky atmosphere` draw: does the triangle write the color
       target (pass depth/stencil)? what are the bound `s_transmittance` /
       `s_multiscatter` textures? what is the fragment output?

2. **Branch on the finding:**
   - **LUTs are zero** -> the storage-image compute write is the bug. Check, in
     `vulkan_compute_command_encoder.cpp::set_storage_image` and
     `Sky_renderer::ensure_luts`: that the `VkImageView` returned by
     `get_vk_image_view(COLOR,0,1)` is valid as a STORAGE_IMAGE; that the
     descriptor is actually pushed (`vkCmdPushDescriptorSetKHR`,
     `VK_PIPELINE_BIND_POINT_COMPUTE`); that the texture is in
     `VK_IMAGE_LAYOUT_GENERAL` at dispatch; that the bind-group-layout binding
     number the shader expects (raw `binding=0/1`) matches what `set_storage_image`
     writes; and that the dispatch group counts cover the image. The
     `memory_barrier(shader_image_access_barrier_bit)` between the two passes and
     the final `GENERAL -> shader_read_only_optimal` transitions are the other
     suspects.
   - **LUTs fine but draw output black** -> atmosphere math / exposure. Quick
     isolation: temporarily make `sky_atmosphere.frag` output a constant bright
     color (ignore the LUTs) to confirm the draw path, then re-check exposure
     scaling (`camera.exposure`) and the inscatter magnitude vs the bloom/tonemap.
   - **Draw not in the capture** -> depth/stencil. Compare the atmosphere
     pipeline's depth/stencil + viewport_depth_range against the gradient sky
     (they are intended to be identical).

3. **Strongly consider a graphics-lib unit test** for the new path
   (`src/erhe/graphics/test/test_compute_storage_image.cpp`, alongside
   `test_compute_2d.cpp`): compute-write a known pattern into an `image2D`, read
   it back, assert. This validates `set_storage_image` in isolation and is
   actually runnable (the full editor could not be launched from the agent shell
   -- no GPU session -- which is why this slipped through). It would have caught
   a zero-LUT bug immediately.

4. **Clean up** the diagnostics once fixed: keep the debug groups (useful), drop
   or downgrade the one-time logs, then update `doc/procedural_sky.md` status to
   "verified" and remove this handoff doc (or fold it in).

## Key entry points

- Storage-image compute infra: `src/erhe/graphics/erhe_graphics/` --
  `bind_group_layout.hpp`, `shader_resource.{hpp,cpp}`,
  `vulkan/vulkan_bind_group_layout.cpp`,
  `compute_command_encoder.{hpp,cpp}` + `vulkan/vulkan_compute_command_encoder.cpp`
  (`set_storage_image`).
- LUT generation + atmosphere draw: `src/editor/renderers/sky_renderer.cpp`
  (`ensure_luts`, `render_atmosphere`).
- Shaders: `res/editor/shaders/sky_atmosphere_common.glsl`,
  `sky_transmittance_lut.comp`, `sky_multiscatter_lut.comp`,
  `sky_atmosphere.{vert,frag}`.
- Mode switch: `Sky_composition_pass` in `src/editor/app_rendering.cpp`.
- ensure_luts hooks: `src/editor/scene/viewport_scene_view.cpp` and
  `src/editor/xr/headset_view.cpp` (before the render pass; gated on
  `editor_settings->sky.mode == 1`).
- Config: `src/editor/config/definitions/sky_config.py` (v3),
  `config/editor/editor_settings.json`.

## Build / run

- `cmake --build build_vs2026_vulkan --target editor --config Debug -- -m -clp:ErrorsOnly`
  (config codegen reruns automatically on the `.py` change). OpenGL + headless
  editors also build. Shaders compile at runtime; an offline `glslc` syntax
  check of all five shaders passed.
- Run `build_vs2026_vulkan/src/editor/Debug/editor.exe`, Settings -> Sky ->
  Sky Mode = 1.
