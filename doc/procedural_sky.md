# Procedural sky (Hillaire atmosphere)

Status: implemented and building (Vulkan, OpenGL, headless all link clean); shaders
pass an offline `glslc` syntax check. The atmosphere is wired on **both Vulkan and
OpenGL** (OpenGL requires GL 4.3+ for compute / storage-image load-store); only Metal
remains unwired. **Runtime / visual verification still pending**
(the editor cannot be launched from the implementing session's shell -- no GPU
session; see "Runtime status"). On branch `ls/main`.

Ported the forge-gpu `forge-procedural-sky` skill (Sebastien Hillaire, EGSR 2020:
physically-based atmospheric scattering with LUT-accelerated transmittance +
multi-scattering) onto erhe, mirroring the earlier cube-map point-light-shadows port.
This document records the design, per-file changes, how to finish verifying, and the
known risks / tuning knobs.

## Overview

The physically-based atmosphere is added as a **second sky mode** alongside the
existing checker/gradient sky (it does not replace it). `Sky_config::mode` selects:
`0` = gradient/checker (unchanged), `1` = atmosphere. The atmosphere reuses erhe's
existing HDR render target + bloom + tonemapping unchanged -- it just emits HDR
radiance (the bright sun disc becomes a soft glow through the existing post pass).

Two lookup tables are generated **once at startup via compute shaders** and then
sampled by the atmosphere fragment ray march:

- **Transmittance LUT** (256x64, RGBA16F): optical depth from a view point to the
  atmosphere top, Bruneton non-linear (height, view-zenith) parameterization.
- **Multi-scattering LUT** (32x32, RGBA16F): pre-integrated multiple scattering
  (Hillaire `NewMultiScatCS`), (altitude, sun-zenith) parameterization.

Generating an `image2D` with a compute shader and sampling it later required **new
storage-image compute support in `erhe::graphics`** (it previously supported only SSBO
compute). That is the foundational, reusable part of this change.

### Why this shape

- erhe already renders the sky as a fullscreen composition pass reading the camera UBO;
  the atmosphere reuses that camera UBO (correct, multiview-aware `world_from_clip`) via
  an owned `Camera_buffer`, so it never re-derives projection / reverse-Z / clip-space
  Y-flip conventions and the sky ray always matches the scene.
- The two LUT samplers + the dedicated atmosphere pipeline are isolated in a new editor
  `Sky_renderer`, so the shared scene bind group layout / forward render path is
  untouched.
- The sun direction follows the scene's first directional light (so the sky's sun
  matches scene lighting), with a config elevation/azimuth fallback when no directional
  light exists.

### Key decisions

- **Storage-image compute, not fragment-pass LUT generation.** The skill uses compute;
  erhe lacked it, so it was built (new `Binding_type::storage_image`, GLSL `image2D`
  emission, `Compute_command_encoder::set_storage_image`, Vulkan `STORAGE_IMAGE`
  descriptors + `GENERAL` layout barriers).
- **Vulkan + OpenGL.** `set_storage_image` is implemented on both the Vulkan backend
  (STORAGE_IMAGE push descriptor) and the OpenGL backend (`gl::bind_image_texture`); Metal
  is not yet wired. OpenGL additionally requires GL 4.3 (`use_compute_shader`) -- on GL <
  4.3, or on Metal/null, `Sky_renderer::is_atmosphere_supported()` returns false and the
  `Sky_composition_pass` falls back to the gradient sky regardless of `mode`.
- **Multiscatter reads transmittance via `imageLoad` + manual bilinear**, so the compute
  path only needs storage-image *write/read* (no compute *sampled*-image support). The
  fragment ray march samples both LUTs with a normal sampler (hardware bilinear).
- **Atmosphere parameters live in the camera UBO `Sky_parameters`** (additive,
  std140-safe): `sun_direction` (xyz toward sun, w = illuminance) and `atmosphere`
  (x = march steps, y = observer altitude km, z = cos sun angular radius, w = sun disc
  brightness). The gradient sky ignores them.

## Data flow / per-file changes

### erhe::graphics -- storage-image compute infrastructure (foundational, reusable)
- `enums.hpp` / `enums.cpp`: new `Glsl_type::image_2d`, mapped to `"image2D"` in
  `glsl_type_c_str` (and a `get_dimension` case).
- `bind_group_layout.hpp`: new `Binding_type::storage_image`; new
  `Bind_group_layout_binding::image_format` (GLSL format qualifier, e.g. `"rgba16f"`).
- `shader_resource.{hpp,cpp}`: new `Type::image`; classification helpers; an image
  constructor + `add_image(...)`; `get_layout_string` emits
  `layout(binding = N, <format>) uniform image2D name;` (format always emitted so
  `imageLoad`/`imageStore` are valid without readonly/writeonly). `get_type_details`
  treats `image_2d` as opaque (zero size) as a safety net.
- `vulkan/vulkan_bind_group_layout.cpp` and `gl/gl_bind_group_layout.cpp`: mirror
  storage_image bindings into the default uniform block via `add_image` (raw binding, no
  sampler offset). Vulkan also maps `to_vulkan_descriptor_type` storage_image ->
  `VK_DESCRIPTOR_TYPE_STORAGE_IMAGE`.
- `compute_command_encoder.{hpp,cpp}` + `vulkan/`, `gl/`, `metal/`, `null/`: new
  `set_storage_image(binding_point, texture)`. Vulkan pushes a STORAGE_IMAGE descriptor
  with `VK_IMAGE_LAYOUT_GENERAL`; GL binds image unit N via `gl::bind_image_texture`
  (read_write, the texture's `Internal_format`); Metal/Null are compile-only no-ops.
- Layout transitions use the existing public `Command_buffer::transition_texture_layout`
  (UNDEFINED->GENERAL before compute write, GENERAL->SHADER_READ_ONLY before fragment
  sample). The inter-pass write->read hazard (both LUTs stay GENERAL) is covered by
  `Command_buffer::memory_barrier(shader_image_access_barrier_bit)`.

### erhe::scene_renderer -- camera UBO atmosphere fields
- `camera_buffer.hpp`: `Sky_parameters` gained `sun_direction` + `atmosphere` (vec4 each);
  `Camera_struct` gained the matching offsets.
- `camera_buffer.cpp`: the camera interface block adds `sun_direction` + `atmosphere`;
  `write_camera_entry` writes them. Gradient sky / id pass pass defaults (harmless).

### editor -- Sky_renderer + atmosphere shaders + config + wiring
- `renderers/sky_renderer.{hpp,cpp}` (new): owns the two LUT textures + linear-clamp
  sampler, the two compute bind group layouts + shaders + `Compute_pipeline`s, the
  atmosphere bind group layout (camera UBO + 2 LUT samplers) + shader + far-plane
  `Base_render_pipeline`, and a `Camera_buffer`.
  - `ensure_luts()` -- one-time: transition + transmittance dispatch + barrier +
    multiscatter dispatch + barrier + transition both to shader-read-only.
  - `render_atmosphere()` -- resolves the sun direction (directional light or config
    fallback), fills `Sky_parameters`, writes the camera UBO via `update_views`, binds
    the pipeline + camera UBO + 2 LUTs, draws a fullscreen triangle into the viewport
    render pass. All GPU work is gated on backends with storage-image compute
    (`#if defined(ERHE_GRAPHICS_API_VULKAN) || defined(ERHE_GRAPHICS_API_OPENGL)`); the
    constructor additionally bails out when `Device_info::use_compute_shader` is false
    (GL < 4.3), leaving the members null so the gradient sky is used.
- `app_rendering.cpp`: the "Sky" composition pass is now a `Sky_composition_pass`
  subclass whose `render()` dispatches to `Sky_renderer::render_atmosphere` in atmosphere
  mode (when `is_atmosphere_supported()`) and to the base gradient `Composition_pass::render`
  otherwise.
- `app_context.hpp`: `Sky_renderer* sky_renderer` part pointer.
- `editor.cpp`: constructs `m_sky_renderer` in the post-processing init task (init command
  buffer available, like `Post_processing`); assigns the App_context pointer.
- `scene/viewport_scene_view.cpp` and `xr/headset_view.cpp`: call
  `sky_renderer->ensure_luts(...)` before the viewport render pass begins when
  `sky.mode == 1` (compute + barriers cannot run inside a render pass).
- `config/definitions/sky_config.py` (v3): `mode` + atmosphere knobs (`sun_intensity`,
  `march_steps`, `observer_altitude_km`, `sun_angular_radius_deg`, `sun_disc_intensity`,
  `sun_elevation_deg`, `sun_azimuth_deg`). Settings-window UI is auto-generated.
- `config/editor/editor_settings.json`: sky section bumped to v3 with the new keys.

### Shaders (`res/editor/shaders/`)
- `sky_atmosphere_common.glsl`: Hillaire constants (Rayleigh/Mie/ozone), ray-sphere,
  phase functions, medium sampling, and the Bruneton transmittance + multi-scatter UV
  mappings (pure math; shared by compute + fragment).
- `sky_transmittance_lut.comp`: `imageStore` the transmittance LUT.
- `sky_multiscatter_lut.comp`: `imageLoad` transmittance (manual bilinear) + sphere
  integration + ground bounce; `imageStore` the multi-scatter LUT.
- `sky_atmosphere.vert`: fullscreen triangle at the far plane; reconstructs the world ray
  from the camera UBO (same as `sky.vert`).
- `sky_atmosphere.frag`: ray march with single scatter (earth-shadowed, smooth
  terminator) + multi-scatter LUT + ground bounce + sun disc; outputs HDR radiance.

## Build / codegen

- Windows / Vulkan: `cmake --build build_vs2026_vulkan --target editor --config Debug -- -m -clp:ErrorsOnly`.
  Config codegen reruns automatically on the `sky_config.py` change (regenerates the v3
  `Sky_config`). Verified: Vulkan, OpenGL, and headless editor builds all link clean.
- Shaders are compiled at editor runtime (GLSL -> SPIR-V). They were additionally syntax
  checked offline with `glslc` (transmittance + multiscatter compute compile to SPIR-V;
  the atmosphere fragment compiles; the vertex uses `gl_VertexID` exactly like the
  existing `sky.vert`, which erhe's pipeline accepts).

## Runtime status (IMPORTANT -- not yet verified)

The implementing session could not run the editor (launched from a detached shell it
aborts before any sky code, in `choose_physical_device` -- no GPU session in that
context; this reproduces independently of these changes). So the build + shader syntax
are proven; visual behavior is not. `vulkan_validation_layers` must stay `false` on this
machine (it breaks `vkCreateInstance` on the crowded layer stack).

## How to finish verifying

1. Run `build_vs2026_vulkan/src/editor/Debug/editor.exe` interactively (real desktop GPU).
2. In Settings -> Sky, set **Sky Mode = 1** (atmosphere). The first atmosphere frame
   generates the LUTs (one-time) and compiles the atmosphere pipeline (a one-time hitch).
3. Confirm a physically-plausible sky: blue zenith, warmer horizon, a bright sun disc
   that blooms. Rotate the view; the sky should stay consistent.
4. Add / orient a **directional light** and confirm the sun disc + sky colors track it
   (sunrise/sunset reddening as the light nears the horizon). With no directional light,
   the `sun_elevation_deg` / `sun_azimuth_deg` config values drive the sun.
5. Tune `sun_intensity`, `sun_angular_radius_deg`, `sun_disc_intensity`, `march_steps`,
   `observer_altitude_km` in Settings and confirm they take effect live.
6. Regression: Sky Mode = 0 must look identical to before (gradient/checker). Toggling
   `enabled` off still hides the sky in both modes.
7. `grep -iE "error|fatal|No shader variant|No render pipeline" logs/log.txt`. With the
   Vulkan validation layer (on a machine where it loads) watch for STORAGE_IMAGE
   descriptor / image-layout warnings around the LUT compute passes.
8. OpenXR / Quest (secondary): the atmosphere is wired into both headset render-pass
   paths; verify per-eye correctness (multiview) on device.

### OpenGL

Build and run `build_vs2026_opengl/src/editor/Debug/editor.exe` on a GL 4.3+ desktop GPU,
then set **Sky Mode = 1**. The same visual / regression checks (1-7 above) apply.
`capture_screenshot` works only in the headless **Vulkan** build, so confirm GL visually in
the window (or with a RenderDoc capture: two compute dispatches binding the LUTs as images,
then the fullscreen atmosphere draw). In `logs/log.txt`, confirm
`Sky_renderer::ensure_luts: generating atmosphere LUTs` once and `render_atmosphere first
call: supported=true ...`; if the startup compute-support line is false (GL < 4.3 or
`force_no_compute_shader`), `supported=false` and the gradient sky is the expected fallback.

## Known risks / tuning knobs

- **Metal not wired.** On Metal the atmosphere is unsupported (no storage-image compute);
  the gradient sky is used even with `mode == 1`. Wiring Metal would need `setTexture` on
  the compute encoder. OpenGL is now wired (`gl::bind_image_texture` + the GL bind group
  layout mirroring storage_image bindings), gated on GL 4.3 (`use_compute_shader`).
- **Multiview unverified.** The atmosphere shader is compiled with the session
  `view_count` and the camera UBO is written per view, but per-eye correctness on Quest
  is not yet visually verified (same caveat as the point-light port).
- **Inter-pass barrier.** The transmittance->multiscatter dependency uses a global
  `memory_barrier(shader_image_access_barrier_bit)` between the two compute passes (both
  LUTs stay in `GENERAL`). If a validation layer flags a hazard, switch to explicit image
  memory barriers.
- **LUT quality.** Transmittance 256x64 / multiscatter 32x32 with 40 / 64x20 samples are
  the skill's defaults. The march step count is configurable (`march_steps`, default 32).
- **Sun disc.** Brightness (`sun_disc_intensity`) and size (`sun_angular_radius_deg`) are
  tuned for the existing bloom/tonemap; adjust if the disc clips or the glow is wrong.
- **Observer altitude.** The sky is a pure background from a fixed observer altitude
  (`observer_altitude_km`, default 0.5 km); the scene camera's world position does not
  move the sky (correct for a sky dome, and sidesteps planet-centric fp precision).

## Reference

- Skill: `/d/forge-gpu/.claude/skills/forge-procedural-sky/SKILL.md` (Lesson 26).
- API mapping: `doc/forge-erhe.md` (see the compute / storage-image rows).
- Hillaire, S. (2020). *A Scalable and Production-Ready Sky and Atmosphere Rendering
  Technique.* EGSR 2020.
