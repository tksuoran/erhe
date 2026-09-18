# Procedural sky (Hillaire atmosphere)

Stability: mostly stable

A physically-based atmosphere (Sebastien Hillaire, EGSR 2020: atmospheric
scattering with LUT-accelerated transmittance and multi-scattering) is a second
sky mode beside the gradient / checker sky. `Sky_config::mode` selects it:
`0` = gradient / checker, `1` = atmosphere. The atmosphere emits HDR radiance
into the existing HDR render target, so bloom and tonemapping treat the bright
sun disc as any other bright source.

Two lookup tables are generated once at startup by compute shaders and then
sampled by the atmosphere fragment ray march:

- **Transmittance LUT** (256x64, RGBA16F): optical depth from a view point to
  the atmosphere top, Bruneton non-linear (height, view-zenith) parameterization.
- **Multi-scattering LUT** (32x32, RGBA16F): pre-integrated multiple scattering
  (Hillaire `NewMultiScatCS`), (altitude, sun-zenith) parameterization.

The atmosphere runs on Vulkan, OpenGL and Metal. OpenGL needs GL 4.3 or newer
for compute and storage-image load-store; Metal needs Tier-2 read-write
`RGBA16Float` (Apple Silicon). Where storage-image compute is unavailable,
`Sky_renderer::is_atmosphere_supported()` returns false and
`Sky_composition_pass` draws the gradient sky whatever `mode` says.

## Why this shape

- The sky is drawn as a fullscreen composition pass reading the camera UBO. The
  atmosphere reuses that camera UBO (correct, multiview-aware `world_from_clip`)
  through an owned `Camera_buffer`, so it never re-derives projection,
  reverse-Z or clip-space y-flip conventions and the sky ray always matches the
  scene.
- The two LUT samplers and the atmosphere pipeline are isolated in the editor's
  `Sky_renderer`, so the shared scene bind group layout and the forward render
  path are untouched.
- The sun direction follows the scene's first directional light, so the sky's
  sun matches scene lighting; with no directional light the config elevation and
  azimuth drive it.

## Key decisions

- **Storage-image compute, not fragment-pass LUT generation.** This is what
  `Binding_type::storage_image`, GLSL `image2D` emission,
  `Compute_command_encoder::set_storage_image` and the Vulkan `STORAGE_IMAGE`
  descriptors + `GENERAL` layout barriers exist for; the infrastructure is
  reusable by any other LUT.
- **Multiscatter reads transmittance with `imageLoad` plus manual bilinear**, so
  the compute path needs storage-image write and read only, and no compute
  sampled-image support. The fragment ray march samples both LUTs with a normal
  sampler (hardware bilinear).
- **Atmosphere parameters live in the camera UBO `Sky_parameters`** (additive,
  std140-safe): `sun_direction` (xyz toward the sun, w = illuminance) and
  `atmosphere` (x = march steps, y = observer altitude km, z = cos sun angular
  radius, w = sun disc brightness). The gradient sky ignores them.
- **The directional light's direction is already "toward the light".** erhe
  stores it as `world_from_node * +Z` and `standard.frag` uses it directly as
  `L`. `Sky_renderer::render_atmosphere` must use it unnegated; negating it puts
  the sun below the horizon and the atmosphere correctly integrates a near-black
  night sky.

## Storage-image compute support in erhe::graphics

This part is foundational and reusable:

- `enums.hpp` / `enums.cpp`: `Glsl_type::image_2d`, mapped to `"image2D"` in
  `glsl_type_c_str` (and a `get_dimension` case).
- `bind_group_layout.hpp`: `Binding_type::storage_image` and
  `Bind_group_layout_binding::image_format` (the GLSL format qualifier, e.g.
  `"rgba16f"`).
- `shader_resource.{hpp,cpp}`: `Type::image`, an image constructor and
  `add_image(...)`; `get_layout_string` emits
  `layout(binding = N, <format>) uniform image2D name;`. The format is always
  emitted, so `imageLoad` / `imageStore` are valid without a
  readonly / writeonly qualifier. `get_type_details` treats `image_2d` as opaque
  (zero size).
- `vulkan/vulkan_bind_group_layout.cpp` and `gl/gl_bind_group_layout.cpp` mirror
  storage-image bindings into the default uniform block via `add_image` (raw
  binding, no sampler offset); Vulkan maps the binding to
  `VK_DESCRIPTOR_TYPE_STORAGE_IMAGE`.
- `compute_command_encoder.{hpp,cpp}` plus the Vulkan, GL, Metal and null
  backends: `set_storage_image(binding_point, texture)`. Vulkan pushes a
  STORAGE_IMAGE descriptor with `VK_IMAGE_LAYOUT_GENERAL`; GL binds image unit N
  through `gl::bind_image_texture` (read_write, the texture's
  `Internal_format`); Metal sets the texture at the raw binding point, with the
  storage-image GLSL declaration mirrored into the default uniform block by
  `metal_bind_group_layout.cpp`, the `[[texture(N)]]` slot pinned in
  `compile_spirv_to_mtl_function`, and `MTL::TextureUsageShaderWrite` on the LUT
  textures.
- Layout transitions use `Command_buffer::transition_texture_layout`
  (UNDEFINED -> GENERAL before the compute write, GENERAL -> SHADER_READ_ONLY
  before the fragment sample). The transmittance -> multiscatter write-to-read
  hazard (both LUTs stay GENERAL) is covered by
  `Command_buffer::memory_barrier(shader_image_access_barrier_bit)`.

## Where the pieces live

| File | Contents |
|---|---|
| `src/erhe/scene_renderer/erhe_scene_renderer/camera_buffer.{hpp,cpp}` | `Sky_parameters::sun_direction` + `atmosphere`, written by `write_camera_entry` |
| `src/editor/renderers/sky_renderer.{hpp,cpp}` | LUT textures + linear-clamp sampler, the two compute bind group layouts / shaders / pipelines, the atmosphere bind group layout (camera UBO + 2 LUT samplers) + far-plane pipeline, and a `Camera_buffer`. `ensure_luts()` runs the one-time transitions and dispatches; `render_atmosphere()` resolves the sun, fills `Sky_parameters`, binds and draws a fullscreen triangle |
| `src/editor/app_rendering.cpp` | `Sky_composition_pass` dispatches to `render_atmosphere` in atmosphere mode and to the base gradient pass otherwise |
| `src/editor/editor.cpp`, `app_context.hpp` | construction in the post-processing init task, `Sky_renderer*` part pointer |
| `src/editor/scene/viewport_scene_view.cpp`, `src/editor/xr/headset_view.cpp` | call `ensure_luts(...)` before the viewport render pass begins (compute and barriers cannot run inside a render pass) |
| `src/editor/config/definitions/sky_config.py` | `mode`, `sun_intensity`, `march_steps`, `observer_altitude_km`, `sun_angular_radius_deg`, `sun_disc_intensity`, `sun_elevation_deg`, `sun_azimuth_deg` |
| `res/editor/shaders/sky_atmosphere_common.glsl` | Hillaire constants (Rayleigh / Mie / ozone), ray-sphere, phase functions, medium sampling, Bruneton UV mappings |
| `res/editor/shaders/sky_transmittance_lut.comp` | `imageStore` of the transmittance LUT |
| `res/editor/shaders/sky_multiscatter_lut.comp` | `imageLoad` of transmittance (manual bilinear) + sphere integration + ground bounce |
| `res/editor/shaders/sky_atmosphere.vert` / `.frag` | fullscreen triangle at the far plane reconstructing the world ray from the camera UBO; ray march with single scatter (earth-shadowed, smooth terminator) + multi-scatter LUT + ground bounce + sun disc |

## Risks and tuning knobs

- **Metal read-write textures.** The LUT `image2D`s carry no
  readonly / writeonly qualifier, so SPIRV-Cross emits
  `texture2d<float, access::read_write>`, which needs Tier-2 read-write texture
  support for `RGBA16Float`. To run on a Tier-1 Metal GPU, add
  readonly / writeonly to `Bind_group_layout_binding`, extend
  `Shader_resource::get_source`'s qualifier emission to cover `Type::image`, and
  mark each LUT binding by its actual access.
- **Inter-pass barrier.** The transmittance -> multiscatter dependency uses a
  global `memory_barrier(shader_image_access_barrier_bit)` between the two
  compute passes. Switch to explicit image memory barriers if a validation layer
  ever flags a hazard.
- **LUT quality.** Transmittance 256x64 / multiscatter 32x32 with 40 and 64x20
  samples; the march step count is `march_steps` (default 32).
- **Sun disc.** `sun_disc_intensity` and `sun_angular_radius_deg` are tuned for
  the existing bloom and tonemap; adjust if the disc clips or the glow is wrong.
- **Observer altitude.** The sky is a background from a fixed observer altitude
  (`observer_altitude_km`, default 0.5 km); the camera's world position does not
  move the sky. That is correct for a sky dome and sidesteps planet-centric
  floating-point precision.

## Verification

1. In Settings -> Sky set **Sky Mode = 1**. The first atmosphere frame generates
   the LUTs and compiles the pipeline (a one-time hitch).
2. Confirm a physically plausible sky: blue zenith, warmer horizon, a bright sun
   disc that blooms. Rotating the view keeps the sky consistent.
3. Add and orient a **directional light**; the sun disc and sky colours track it
   (reddening as the light nears the horizon). With no directional light,
   `sun_elevation_deg` / `sun_azimuth_deg` drive the sun.
4. `sun_intensity`, `sun_angular_radius_deg`, `sun_disc_intensity`,
   `march_steps` and `observer_altitude_km` take effect live.
5. Regression: Sky Mode = 0 is the unchanged gradient / checker sky, and
   `enabled` off hides the sky in both modes.
6. `grep -iE "error|fatal|No shader variant|No render pipeline" logs/log.txt`.
   In `logs/log.txt`, `Sky_renderer::ensure_luts: generating atmosphere LUTs`
   appears once and `render_atmosphere first call: supported=true ...` confirms
   the atmosphere path; `supported=false` means the gradient fallback.
7. On OpenGL, `capture_screenshot` serves the headless Vulkan build only, so
   confirm GL in the window or with a RenderDoc capture (two compute dispatches
   binding the LUTs as images, then the fullscreen atmosphere draw).

## Reference

- API mapping: `doc/reference/forge_erhe.md` (the compute / storage-image rows).
- Hillaire, S. (2020). *A Scalable and Production-Ready Sky and Atmosphere
  Rendering Technique.* EGSR 2020.

## Future work

- [plans/procedural_sky.md](plans/procedural_sky.md) - Metal and multiview
  runtime verification.
