# DDGI (dynamic diffuse global illumination)

Stability: experimental

DDGI gives non-baked scenes runtime indirect diffuse light with no authoring
step: one scene-wide probe volume is fitted to the content bounding box, the
probes are traced with ray queries, the results are blended into octahedral
irradiance and distance atlases, and `res/shaders/standard.frag` samples those
atlases in place of the flat `light_block.ambient_light` term.

The feature requires GPU ray query (`Device_info::use_ray_query`), exactly like
`Ray_trace_renderer`; on backends without it every part of the renderer no-ops
and the flat ambient term stands.

## Scope

| Question | Answer |
|---|---|
| Volume authoring | One scene-wide volume, auto-fitted to the padded content AABB. |
| Feature set | Octahedral irradiance and distance / Chebyshev visibility, temporal hysteresis, probe relocation, probe classification, border texels. |
| Lightmap interaction | Mutually exclusive per draw: a lightmapped primitive keeps its baked term (and its analytic-light gate); every other draw gets DDGI in place of the flat ambient. |
| Backend | Ray query only. |

Probes see direct light plus one implicit bounce through whatever the ray hits;
the field is not fed back into the trace. Rays that escape the scene take the
scene ambient as sky radiance. On an open scene lit mostly by ambient the DDGI
term is therefore close to the flat ambient it replaces, and the difference
shows in enclosed geometry; the intensity knob and the `DDGI Irradiance` shader
debug mode (33) make it visible.

## Two constraints from the graphics abstraction

1. **Storage images are `image2D` only.** `Glsl_type`
   (`src/erhe/graphics/erhe_graphics/enums.hpp`) has no `image_2d_array` or
   `image_3d`. All probe state is therefore laid out as 2D atlases - which is
   the classic DDGI layout anyway.
2. **No indirect dispatch.** `Compute_command_encoder::dispatch_compute` takes
   literal sizes, so probe and ray counts are CPU-known.

## Data layout

Grid: `nx * ny * nz` probes over the padded content AABB, spacing from settings,
total clamped to `max_probes`. `probe_index = x + nx * (y + ny * z)`.
Atlas tiling: `tiles_x = nx * nz`, `tiles_y = ny`.

| Resource | Format | Size | Purpose |
|---|---|---|---|
| Ray data | RGBA16F storage + sampled | `rays_per_probe` x `probe_count` | radiance rgb, hit distance a (negative = backface hit) |
| Irradiance atlas | RGBA16F | tile = `irradiance_texels + 2` square (default 6 + 2) | octahedral irradiance + 1-texel border |
| Distance atlas | RG16F | tile = `distance_texels + 2` square (default 14 + 2) | mean distance, mean squared distance |
| Probe data | RGBA32F | 1 texel per probe | relocation offset xyz, state w (full float so the debug overlay readback is a plain memcpy) |

Single-buffered: each texel is written by exactly one invocation per pass, so no
ping-pong is needed and a `memory_barrier` between passes suffices.

## Passes (all compute, all ray-query gated)

1. **`res/editor/shaders/ddgi_trace.comp`** - `rays_per_probe x probe_count`
   threads. Spherical-Fibonacci directions rotated by a per-frame random
   rotation from the control UBO. Origin = probe centre + relocation offset.
   On hit: fetch attributes via the instance-record device addresses, look up
   the material, shade against the `Light_buffer` lights with traced shadow
   rays - the `ray_trace.comp` hit path minus the Whitted branching, shared
   through `res/shaders/erhe_ray_hit.glsl`. On miss: scene ambient. Backface
   hit: store `-distance` and zero radiance.
2. **`ddgi_blend.comp`, irradiance variant** - one workgroup per probe,
   striding over the tile's texels. Cosine-weighted accumulation of the
   probe's rays, hysteresis blend against the existing texel, then the
   border-texel copy in the same dispatch.
3. **`ddgi_blend.comp`, distance variant** (`ERHE_DDGI_BLEND_DISTANCE`) - same
   shape, with `pow(max(0, cos), depth_sharpness)` weighting of distance and
   distance squared, plus the border copy.
4. **`ddgi_relocate.comp`** - one thread per probe. The backface-hit ratio over
   the probe's rays gives the inactive state; the offset is nudged toward the
   most open direction, clamped to `0.5 * spacing`. Writes the probe data
   texture.

Budgeting: a round-robin probe cursor with a `probes_per_frame` budget,
mirroring the lightmap tile cursor.

## Runtime sampling

- `res/shaders/erhe_ddgi.glsl`:
  `ddgi_sample_irradiance(world_pos, normal, view_dir)` - surface-biased sample
  point, 8 probe taps, trilinear x smooth-backface normal weight x Chebyshev
  visibility weight, log-space blend, active-probe gate.
- Three texture heap slots next to `c_texture_heap_slot_lightmap`
  (`src/erhe/scene_renderer/erhe_scene_renderer/light_buffer.hpp`): **5**
  `s_ddgi_irradiance`, **6** `s_ddgi_distance`, **7** `s_ddgi_probe_data` (read
  with `texelFetch`, so it shares the bilinear clamp sampler). Declared in
  `program_interface.cpp` alongside `s_lightmap`; bound by
  `Light_buffer::bind_ddgi(...)` with 1x1 black fallbacks from both
  `Forward_renderer` begin-pass sites.
- Grid parameters ride in the existing `Light_block` (grid origin, spacing,
  counts + rays, and a params vec4 of normal bias / view bias / irradiance gamma
  / intensity) rather than a new binding point; `Light_buffer::update()` takes a
  `Ddgi_parameters` argument.
- Variant gating: `X(USE_DDGI)` in `ERHE_SHADER_BOOL`
  (`src/erhe/scene_renderer/erhe_scene_renderer/shader_key.hpp`), seeded
  scene-level by `Forward_renderer` like the light counts. The prewarm list
  (`src/editor/renderers/prewarm.cpp`) warms DDGI variants only while the
  feature is enabled, so the variant space does not double when it is off.
- `standard.frag` samples the field only when the draw has no valid lightmap
  region; the analytic light loops keep running, because DDGI is indirect only.

## Component wiring

`src/editor/renderers/ddgi_renderer.{hpp,cpp}` follows `Ray_trace_renderer` for
the construction / bind-group / pipeline recipe and the lightmap tick for
lifecycle:

- Constructed in `editor.cpp`'s `post_processing_task` next to
  `Ray_trace_renderer`, held as a `unique_ptr` member, published to
  `App_context` in `fill_app_context()`. A part constructor may not read
  `context.editor_settings` - it is assigned after part construction.
- Ticked from `Editor::tick()` after `flush_draw_lists()`, recording into
  `m_app_context.current_command_buffer`, then
  `m_forward_renderer->set_ddgi(irradiance, distance, probe_data, params)`.
  The volume is scene-global, so it is neither a rendergraph node nor per-view.
- `is_supported()` mirrors `Ray_trace_renderer::is_supported()`.
- `Ddgi_renderer` is also a `Renderable`: the probe overlay
  (`debug_draw_probes`) draws probe spheres coloured from a periodic probe-data
  readback.

## Phases

The renderer is built out of these parts; the labels are cited from source
comments.

**1. `Scene_tlas`** (`src/editor/renderers/scene_tlas.{hpp,cpp}`) - the bottom
level cache, the per-frame-in-flight top level slots and the instance-record
SSBO, shared with `Ray_trace_renderer`. `Lightmap_baker` keeps its own copy of
this code because its instance records carry texcoord-2 addresses.

**2. Settings and skeleton.** `Ddgi_config`
(`src/editor/config/definitions/ddgi_config.py`, erhe_codegen, `reflect=True`,
shown in the Settings window): `enabled`, `probe_spacing_m`, `volume_padding_m`,
`max_probes`, `rays_per_probe`, `irradiance_texels`, `distance_texels`,
`hysteresis`, `depth_sharpness`, `normal_bias`, `view_bias`, `intensity`,
`probes_per_frame`, `relocation_enabled`, `classification_enabled`,
`debug_draw_probes`. Plus the grid fit and texture allocation in
`Ddgi_renderer`, the developer `Ddgi_window`
(`src/editor/developer/ddgi_window.{hpp,cpp}`) reporting grid dimensions, probe
count and memory, and the MCP `set_ddgi` tool.

**3. `ddgi_trace.comp`** and the ray data texture, previewable in the window.

**4. Blend passes** - irradiance and distance, including borders and hysteresis.

**5. `ddgi_relocate.comp`** - relocation and inactive-probe classification.

**6. Runtime sampling** - heap slots, `Light_block` fields, `erhe_ddgi.glsl`,
the `USE_DDGI` axis, the `standard.frag` branch, `Forward_renderer::set_ddgi`,
prewarm.

**7. Debug and tooling** - the probe overlay, the `DDGI Irradiance` shader debug
mode and the MCP `set_ddgi` tool.

## Traps

- A `Renderable` may submit debug lines only in the CPU phase
  (`Render_context::encoder == nullptr`). Lines submitted in the encoder phase
  miss the debug renderer's compute dispatch, and its buffer bookkeeping then
  trips an assert at frame end.
- Vulkan offsets `combined_image_sampler` bindings past the max buffer binding
  in a bind group; raw bindings (acceleration structure, storage image) are not.
  Pick user binding points that do not collide after the offset.
- `accelerationStructureEXT` must be declared by hand in GLSL; samplers, storage
  images and uniform blocks are auto-injected from the bind group layout.
- Compute command buffers use dedicated thread slots (lightmap = 6,
  texture-graph export = 7); DDGI uses its own.
- After changing a codegen definition, build twice or the binary is stale.

## Verification

1. Headless verify loop: build `build_vs2026_vulkan_headless`, launch, then
   `py -3 scripts/mcp_call.py set_ddgi {"enabled":true,"show_window":true}`,
   `get_async_status`, `capture_screenshot`. Compare a Sponza / Bistro
   screenshot with DDGI off versus on: bounce colour on shadowed walls, no
   light through closed geometry.
2. Build the OpenGL configuration too, to confirm the `USE_DDGI`-off path still
   compiles and links.
3. Vulkan validation stays at zero errors - watch the image layout transitions
   between the trace and blend dispatches.
4. Regression: with DDGI disabled the frame matches the non-DDGI output, and a
   lightmap-baked scene looks unchanged with DDGI on.
5. Performance: the per-frame DDGI cost is reported in the Ddgi window; the
   lightmap baker's ~1.5 ms/frame budget is the benchmark to stay under.

## Future work

- [plans/ddgi.md](../plans/ddgi.md) - infinite bounces, sky radiance from the
  atmosphere LUTs, authored and cascaded volumes, the non-ray-query fallback.
