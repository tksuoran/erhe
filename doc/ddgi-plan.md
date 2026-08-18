# DDGI (Dynamic Diffuse Global Illumination) plan

Status: IN PROGRESS on branch `ddgi` (started 2026-08-18).

## Status

- Phase 0 (this document) - DONE.
- Phases 1-7 - not started.

## Why

erhe has no runtime indirect diffuse. `res/shaders/standard.frag` uses a flat
`light_block.ambient_light` term, or - for meshes that have been through the
lightmap baker - a baked atlas lookup. The lightmap path is progressive, tiled,
streamed and UV-unwrap dependent; it cannot serve dynamic geometry, moving
lights, or freshly loaded content.

Everything DDGI needs already exists in the tree and is proven:

- GPU ray query in compute: `Ray_trace_renderer`
  (`src/editor/renderers/ray_trace_renderer.{hpp,cpp}` +
  `res/editor/shaders/ray_trace.comp`) - per-`Buffer_mesh` BLAS cache,
  per-frame-in-flight TLAS slots, an instance-record SSBO of buffer device
  addresses for attribute fetch, material lookup via `Material_buffer` +
  `Texture_heap`, and light shading with traced shadow rays against
  `Light_buffer`.
- A second, independent ray-query gather with sky radiance and a cosine bounce:
  `Lightmap_baker`'s `c_gather_source` (`src/editor/renderers/lightmap_baker.cpp`,
  around line 509).
- Compute passes writing storage-image LUTs that raster shaders then sample:
  `Sky_renderer::ensure_luts`.
- A budgeted, per-frame-ticked GPU subsystem whose texture is published to the
  forward renderer: the lightmap block in `Editor::tick()`
  (`src/editor/editor.cpp`, around lines 807-990) plus
  `Forward_renderer::set_lightmap_texture`.

So this is mostly assembly of existing machinery, not new infrastructure.

`doc/lightmap_baking_plan.md` already lists "light probes for dynamic objects"
as its acknowledged sequel, and `doc/raytrace-plan.md` lists GI as not yet done.

## Decisions (user, 2026-08-18)

| Question | Decision |
|---|---|
| Volume authoring | One scene-wide volume, auto-fitted to the content AABB. Node-attached / cascaded volumes are follow-up work. |
| Scope | Full DDGI: octahedral irradiance **and** distance / Chebyshev visibility, temporal hysteresis, probe relocation, probe classification, border texels. |
| Lightmap interaction | Mutually exclusive per draw: a lightmapped primitive keeps its baked term (and its analytic-light gate); everything else gets DDGI in place of the flat ambient. |
| Backend | Ray query only (`Device_info::use_ray_query`), exactly like `Ray_trace_renderer`. A rasterized probe-cubemap fallback for Quest / GL is explicit future work. |

Intended outcome: enabling DDGI gives moving, non-baked scenes plausible bounce
light and colour bleeding that reacts to light and geometry edits within a few
frames, with no authoring step.

## Two hard constraints from the graphics abstraction

1. **Storage images are `image2D` only.** `Glsl_type`
   (`src/erhe/graphics/erhe_graphics/enums.hpp`) has no `image_2d_array` or
   `image_3d`, and the comment there says as much. All probe state is therefore
   laid out as 2D atlases - which is the classic DDGI layout anyway.
2. **No indirect dispatch.** `Compute_command_encoder::dispatch_compute` takes
   literal sizes. Probe and ray counts must be CPU-known; they are.

## Data layout

Grid: `nx * ny * nz` probes over the padded content AABB, spacing from settings,
total clamped to `max_probes`. `probe_index = x + nx * (y + ny * z)`.
Atlas tiling: `tiles_x = nx * nz`, `tiles_y = ny`.

| Resource | Format | Size | Purpose |
|---|---|---|---|
| Ray data | RGBA16F storage + sampled | `rays_per_probe` x `probe_count` | radiance rgb, hit distance a (negative = backface hit) |
| Irradiance atlas | RGBA16F | tile = `irradiance_texels + 2` square (default 6 + 2) | octahedral irradiance + 1-texel border |
| Distance atlas | RG16F | tile = `distance_texels + 2` square (default 14 + 2) | mean distance, mean squared distance |
| Probe data | RGBA16F | 1 texel per probe | relocation offset xyz, state w |

Single-buffered: each texel is written by exactly one invocation per pass, so no
ping-pong is needed; a `memory_barrier` between passes suffices.

## Passes (all compute, all ray-query gated)

1. **`res/editor/shaders/ddgi_trace.comp`** - `rays_per_probe x probe_count`
   threads. Spherical-Fibonacci directions rotated by a per-frame random
   rotation from the control UBO. Origin = probe centre + relocation offset.
   On hit: fetch attributes via the instance-record device addresses, look up
   the material, shade against the `Light_buffer` lights with traced shadow
   rays - the `ray_trace.comp` hit path minus the Whitted branching. On miss:
   sky radiance via the `sky_atmosphere` LUTs (the `sky_sample_*` helpers in
   `lightmap_baker.cpp`), or scene ambient when the sky is off. Backface hit:
   store `-distance` and zero radiance.
2. **`ddgi_blend_irradiance.comp`** - one workgroup per probe, one thread per
   interior texel. Cosine-weighted accumulation of the probe's rays, hysteresis
   blend against the existing texel, then the border-texel copy in the same
   dispatch.
3. **`ddgi_blend_distance.comp`** - same shape, with
   `pow(max(0, cos), depth_sharpness)` weighting of distance and distance
   squared, plus the border copy.
4. **`ddgi_relocate_classify.comp`** - one thread per probe. Backface-hit ratio
   over the probe's rays gives the inactive state; the offset is nudged toward
   the most open direction, clamped to `0.5 * spacing`. Writes the probe data
   texture.

Budgeting: a round-robin probe cursor with a `probes_per_frame` budget,
mirroring the lightmap tile cursor. Light / geometry change invalidation resets
hysteresis for a few frames (start with a simple FNV hash over light state and
content transforms - the same tiering idea as `Lightmap_baker`).

## Runtime sampling

- New `res/shaders/erhe_ddgi.glsl`:
  `ddgi_sample_irradiance(world_pos, normal, view_dir)` - surface-biased sample
  point, 8 probe taps, trilinear x smooth-backface normal weight x Chebyshev
  visibility weight, log-space blend, active-probe gate.
- Three texture heap slots next to `c_texture_heap_slot_lightmap`
  (`src/erhe/scene_renderer/erhe_scene_renderer/light_buffer.hpp`): **5**
  `s_ddgi_irradiance`, **6** `s_ddgi_distance`, **7** `s_ddgi_probe_data` (read
  with `texelFetch`, so it can share the bilinear clamp sampler). Declared in
  `program_interface.cpp` alongside `s_lightmap`; bound by a new
  `Light_buffer::bind_ddgi(...)` with 1x1 black fallbacks, called from both
  `Forward_renderer` begin-pass sites.
- Grid parameters ride in the existing `Light_block` (grid origin, spacing,
  counts + rays, and a params vec4 of normal bias / view bias / irradiance gamma
  / intensity) rather than a new binding point; `Light_buffer::update()` gains a
  `Ddgi_parameters` argument. This shifts std140 offsets, so
  `src/rendering_test/`'s duplicated shaders may fall out of sync - which
  `AGENTS.md` explicitly allows.
- Variant gating: add `X(USE_DDGI)` to `ERHE_SHADER_BOOL`
  (`src/erhe/scene_renderer/erhe_scene_renderer/shader_key.hpp`), seeded
  scene-level by `Forward_renderer` like the light counts. Keep the prewarm list
  (`src/editor/renderers/prewarm.cpp`) from doubling - prewarm DDGI variants
  only when the feature is enabled.
- `standard.frag`, replacing the current ambient / lightmap branch:

```glsl
vec3 ambient_term   = light_block.ambient_light.rgb;
bool lightmap_valid = false;
// ... existing lightmap branch sets ambient_term / lightmap_valid ...
#if defined(ERHE_USE_DDGI)
if (!lightmap_valid) {
    ambient_term = ddgi_sample_irradiance(v_position.xyz, N, V) * light_block.ddgi_params.w;
}
#endif
```

The analytic light loops keep running for non-lightmapped draws - DDGI is
indirect only.

## Component wiring

New `src/editor/renderers/ddgi_renderer.{hpp,cpp}`, modelled on
`Ray_trace_renderer` for the construction / bind-group / pipeline recipe and on
the lightmap tick for lifecycle:

- Constructed in `editor.cpp`'s `post_processing_task`, next to
  `Ray_trace_renderer`; stored as a `unique_ptr` member; published to
  `App_context` in `fill_app_context()`.
- Ticked from `Editor::tick()` after `flush_draw_lists()`, recording into
  `m_app_context.current_command_buffer`, then
  `m_forward_renderer->set_ddgi(irradiance, distance, probe_data, params)`.
  Scene-global, so not a rendergraph node and not per-view.
- `is_supported()` mirrors `Ray_trace_renderer::is_supported()`
  (`Device_info::use_ray_query`); everything no-ops otherwise.
- Do not read `context.editor_settings` in the constructor - it is assigned
  after part construction.

## Phases

Each phase is edit -> build (`scripts\build_ninja_win_vulkan.bat editor`) ->
independent review -> fix -> commit.

**0. This document.** DONE.

**1. Extract `Scene_tlas`** (`src/editor/renderers/scene_tlas.{hpp,cpp}`) - the
BLAS cache, per-frame-in-flight TLAS slots and instance-record SSBO, moved out
of `Ray_trace_renderer` and used by it. Mechanical; regressions show up in the
Ray Trace window and the MCP `set_ray_trace` tool. `Lightmap_baker` keeps its
own copy (its records carry texcoord-2 addresses); migrating it is a separate,
later job.

**2. Settings + skeleton.** `src/editor/config/definitions/ddgi_config.py` (copy
`ray_trace_config.py` / `lightmap_config.py`), a line in
`editor_settings_config.py`, three `_config_sources` entries in
`src/editor/CMakeLists.txt`, `add_config_section(settings.ddgi)` in
`settings_window.cpp`. Fields: `enabled`, `probe_spacing_m`, `volume_padding_m`,
`max_probes`, `rays_per_probe`, `irradiance_texels`, `distance_texels`,
`hysteresis`, `depth_sharpness`, `normal_bias`, `view_bias`, `intensity`,
`probes_per_frame`, `relocation_enabled`, `classification_enabled`,
`debug_draw_probes`. Plus `Ddgi_renderer` doing grid fit and texture allocation
only, and a developer `Ddgi_window`
(`src/editor/developer/ddgi_window.{hpp,cpp}`, template: `ray_trace_window.*`)
reporting grid dims / probe count / memory. Build twice after touching a codegen
definition.

**3. `ddgi_trace.comp`** plus the ray data texture, with the raw ray texture
previewable in the window. Cross-check one probe's rays against the
`Ray_trace_renderer` image.

**4. Blend passes** (irradiance + distance, including borders and hysteresis),
with atlas previews in the window. Expect convergence within a second and no
flicker.

**5. `ddgi_relocate_classify.comp`** - relocation and inactive-probe
classification.

**6. Runtime sampling** - heap slots, `Light_block` fields, `erhe_ddgi.glsl`,
the `USE_DDGI` axis, the `standard.frag` branch, `Forward_renderer::set_ddgi`,
prewarm. First phase with a visible viewport result.

**7. Debug + tooling** - probe spheres via a `Renderable`
(`Primitive_renderer::add_sphere`, colour from a periodic probe-data readback),
a `Shader_debug` mode showing the DDGI term alone, an MCP `set_ddgi` tool
mirroring `action_set_ray_trace` in `src/editor/mcp/mcp_server.cpp`, and
`src/editor/renderers/notes.md` + this document updated.

## Gotchas to carry into implementation

- Vulkan offsets `combined_image_sampler` bindings past the max buffer binding
  in a bind group; raw bindings (acceleration structure, storage image) are not.
  Pick user binding points that do not collide after the offset - this bit the
  lightmap gather.
- `accelerationStructureEXT` must be declared by hand in GLSL; samplers, storage
  images and uniform blocks are auto-injected from the bind group layout.
- Compute command buffers use dedicated thread slots (lightmap = 6,
  texture-graph export = 7); pick a fresh slot for DDGI.
- Adding a `Shader_bool` axis doubles the variant space - gate prewarm.
- After changing a codegen definition, build twice or the binary is stale.

## Verification

1. `scripts\build_ninja_win_vulkan.bat editor` after every phase; also build the
   OpenGL config once at phase 6 to confirm the `USE_DDGI`-off path still
   compiles and links.
2. Headless verify loop: build `build_vs2026_vulkan_headless`, launch, then
   `py -3 scripts/mcp_call.py set_ddgi {"enabled":true,"show_window":true}`,
   `get_async_status`, `capture_screenshot`. Compare a Sponza / Bistro
   screenshot with DDGI off vs on: bounce colour on shadowed walls, no light
   through closed geometry.
3. Vulkan validation must stay at zero errors - watch specifically for image
   layout transitions between the trace and blend dispatches.
4. Regression: with DDGI disabled the frame must match today's output, and a
   lightmap-baked scene must look unchanged with DDGI on.
5. Perf: report the per-frame DDGI cost in the Ddgi window (the lightmap baker's
   ~1.5 ms/frame budget is the benchmark to stay under).

## Explicitly out of scope (follow-ups)

Infinite bounces by sampling the previous frame's field at ray hits;
node-authored and cascaded / camera-scrolling volumes; the Quest / GL
probe-cubemap fallback; specular reuse of the field; per-scene volume overrides;
migrating `Lightmap_baker` onto `Scene_tlas`.
