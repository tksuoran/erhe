# DDGI (dynamic diffuse global illumination)

Stability: experimental

DDGI gives non-baked scenes runtime indirect diffuse light with no authoring
step: one scene-wide probe volume is fitted to the content bounding box, the
probes are traced with ray queries, the results are blended into octahedral
irradiance and distance atlases, and `res/shaders/standard.frag` samples those
atlases in place of the flat `light_block.ambient_light` term.

DDGI is active while it is the selected indirect diffuse source
(`Editor_settings_config::indirect_diffuse_source` = `ddgi`,
[radiance_cascades.md](radiance_cascades.md) "Source selection"); any
other source releases its probe textures.

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
total clamped to `max_probes` and to the probes whose tiles fit the texture
size limit (`get_probe_field_max_probes()`). `probe_index = x + nx * (y + ny * z)`.

Atlas tiling, shared by every producer of the probe field and the forward
pass (`get_probe_field_tile()` in `src/editor/renderers/probe_grid.{hpp,cpp}`,
`ddgi_probe_tile()` in `res/shaders/erhe_ddgi_tiles.glsl`): probe
`(x, y, z)` has the tile index `x + nx * (z + nz * y)`, wrapped into rows of
`tiles_per_row` tiles. `tiles_per_row` is `nx * nz` - one tile row per y
layer, tile `(x + nx * z, y)` - when both atlas sides then fit
`Device_info::max_texture_size` with the larger (distance) tile, otherwise
`ceil(sqrt(probe count))`, which keeps the atlas close to square; the
probe data texture has one texel per tile. `tiles_per_row` rides in
`Ddgi_parameters` (`light_block.ddgi_texels.z`) for the forward pass and in
the DDGI control block (`atlas.x`) for the probe update passes. The
octahedral border copy of the blend passes is
`res/editor/shaders/erhe_ddgi_border.glsl`, shared with the radiance
cascades reduce.

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
   hit: store `-distance` and zero radiance. This per-ray transport is
   `ddgi_trace_ray_radiance()` in `res/editor/shaders/erhe_ddgi_ray.glsl`,
   which the reference irradiance query (below) calls too; the radiance
   cascades interval trace calls its segment form
   `ddgi_trace_ray_segment()` ([radiance_cascades.md](radiance_cascades.md)
   "Trace"). Its rays start
   at `t_min = 0` (`trace_closest_from()` in `res/shaders/erhe_ray_hit.glsl`;
   `trace_closest()` keeps the 1 mm `t_min` for rays leaving a surface): the
   origin is a point in free space, so a face the probe sits on is hit - as a
   frontface from its open side, as a backface from inside the solid -
   instead of being looked through, which is what lets relocation move an
   on-face probe to the correct side.
2. **`ddgi_blend.comp`, irradiance variant** - one workgroup per probe,
   striding over the tile's texels. Cosine-weighted accumulation of the
   probe's rays, hysteresis blend against the existing texel, then the
   border-texel copy in the same dispatch.
3. **`ddgi_blend.comp`, distance variant** (`ERHE_DDGI_BLEND_DISTANCE`) - same
   shape, with `pow(max(0, cos), depth_sharpness)` weighting of distance and
   distance squared, plus the border copy.
4. **`ddgi_relocate.comp`** - one thread per probe, from the probe's rays of
   this update. State: inactive when more than 25 % of the rays hit
   backfaces. Offset, first rule that applies:
   - **inside geometry** - more than 25 % backfaces AND the closest hit is a
     backface (a point inside a closed solid meets the solid's boundary from
     within first): the offset moves along the closest backface's ray by its
     distance plus the near-surface distance (5 % of the smallest spacing),
     out of the solid through its nearest face;
   - **near a surface** - the closest hit is a frontface nearer than the
     near-surface distance: pushed straight away from it to that distance;
   - otherwise the offset relaxes toward the grid position (`x 0.98` per
     update); a relocated probe relaxes until the near-surface rule holds it,
     so it settles at the near-surface distance from the face it left
     through (a millimetre-scale relax / push jitter, no state change).

   The offset is clamped to 0.45 x the smallest spacing, so a probe deeper
   than that inside a solid cannot leave it and stays inactive. A probe in
   touching or interpenetrating solids whose closest hit is a frontface of
   the neighbouring solid is not moved and stays inactive. Writes the probe
   data texture.

Budgeting: a round-robin probe cursor with a `probes_per_frame` budget,
mirroring the lightmap tile cursor.

## Runtime sampling

- `res/shaders/erhe_ddgi.glsl`:
  `ddgi_sample_irradiance(world_pos, normal, view_dir)` - the sample point is
  biased by `normal_bias` along the normal and `view_bias` along the view
  direction; the 8 active probes of its cell are blended linearly with
  trilinear (of the biased point) x smooth-backface x Chebyshev visibility
  (from the biased point) weights, each weight floored at `1e-4`. The
  backface weight takes the direction from the unbiased surface point to the
  probe, so a probe between the surface and the biased point - a probe
  relocated off this surface, or a probe plane closer to the surface than the
  bias - keeps its full weight. With all 8 probes inactive the function
  returns the flat scene ambient.
- Three texture heap slots next to `c_texture_heap_slot_lightmap`
  (`src/erhe/scene_renderer/erhe_scene_renderer/light_buffer.hpp`): **5**
  `s_ddgi_irradiance`, **6** `s_ddgi_distance`, **7** `s_ddgi_probe_data` (read
  with `texelFetch`, so it shares the bilinear clamp sampler). Declared in
  `program_interface.cpp` alongside `s_lightmap`; bound by
  `Light_buffer::bind_ddgi(...)` with 1x1 black fallbacks from both
  `Forward_renderer` begin-pass sites.
- Grid parameters ride in the existing `Light_block` (grid origin, spacing,
  counts, texels - irradiance, distance, tiles per atlas row - and a params
  vec4 of normal bias / view bias / depth sharpness / intensity) rather than
  a new binding point; `Light_buffer::update()` takes a `Ddgi_parameters`
  argument.
- Variant gating: `X(USE_DDGI)` in `ERHE_SHADER_BOOL`
  (`src/erhe/scene_renderer/erhe_scene_renderer/shader_key.hpp`), seeded
  scene-level by `Forward_renderer` like the light counts. The init-time
  prewarm (`Forward_renderer::prewarm_standard_variants()`, driven by
  `src/editor/renderers/prewarm.cpp`) leaves the axis off, so the prewarmed
  variant space does not double; the `USE_DDGI` variants compile on demand
  on the first frame a field is bound.
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
  `m_app_context.current_command_buffer`. `Editor::tick()` then publishes
  the selected producer's field (`get_indirect_diffuse_field()`,
  `Ddgi_renderer::get_field()` while DDGI is selected) with
  `m_forward_renderer->set_ddgi(params, irradiance, distance, probe_data)`.
  The volume is scene-global, so it is neither a rendergraph node nor per-view.
- `is_supported()` mirrors `Ray_trace_renderer::is_supported()`.
- `Ddgi_renderer` is also a `Renderable`: the probe overlay
  (`debug_draw_probes`) draws probe spheres coloured from a periodic probe-data
  readback.

## Performance

Each update times its four GPU passes separately with explicit-range
`erhe::graphics::Gpu_timer`s (`Scoped_gpu_timer` around the dispatches): `trace`,
`blend_irradiance`, `blend_distance` and `relocate`. The timers also appear in
the Performance window as `DDGI trace`, `DDGI blend irradiance`, ... The
timestamps are taken after all earlier work completes, so the passes partition
the update's wall time: `relocate` runs concurrently with the blends (no barrier
between them) and usually reads close to 0, and the total is exact.

`Ddgi_renderer::get_stats()` is the single source of the derived figures the
Ddgi window's "GPU time" section and the MCP tool report:

- per pass and total: the last measurement and the mean over the last
  `c_timing_history_size` (60) measurements, in milliseconds. A result lags its
  update by the frames in flight; nothing is recorded until the first result
  arrives, and the history is cleared when another source is selected.
- `rays_per_update`: probes per update x rays per probe, as dispatched (the
  configured ray count rounded up to the trace workgroup size).
- `ms_per_million_rays`: total mean ms divided by millions of rays per update.
- `updates_per_full_refresh` = ceil(probe count / probes per update), and
  `full_refresh_ms` = that x total mean ms: the GPU time to trace every probe
  once.
- `update_count` (updates dispatched) and `timing_sample_count` (timing samples
  taken), which advance while the field is being updated.

The MCP tool `get_indirect_diffuse_stats` (no arguments) returns `source`
(the selected indirect diffuse source: `"ambient"`, `"ddgi"` or
`"radiance_cascades"`), a `radiance_cascades` object
([radiance_cascades.md](radiance_cascades.md) "MCP") and a `ddgi` object with the
grid origin / spacing / counts, probe count, rays per probe, probes and rays per
update, `gpu_ms` per pass and `gpu_ms_total` (`last_ms`, `average_ms`), the
derived figures above, `texture_bytes`, and `probe_states` (below).

## Probe state

The relocation / classification state is observable without the overlay:
every `get_indirect_diffuse_stats` call asks `Ddgi_renderer` for a copy of
the probe data texture after the next probe update
(`request_probe_states()`, a transfer into its own host-visible buffer,
recorded only on request) and reports the most recent copy that has retired
(`poll_probe_states()`): `probe_states` = `update_count` (field updates when
copied), `active`, `inactive`, `relocated` (`|offset|` > 1 mm) and
`max_offset_over_spacing`, or `null` until the first copy retires. The
optional argument `probes` (`[[x, y, z], ...]` grid coordinates) adds
`probe_data`, index-aligned: `{coords, offset, state}` from the same copy.

GI test stations at the pinned settings, converged: `probe_offset_sweep` 18
inactive / 65 relocated of 600 (the inactive ones sit where two room shells
touch, see the relocation rule above), `corridor` 0 / 68 of 380 (probes
closer to a wall than 5 % of the spacing, pushed out to that distance),
every other station 0 / 0.

## Irradiance queries

The MCP tool `sample_indirect_diffuse` evaluates the field at world points and
returns linear float RGB, so verification measures the indirect term itself
instead of an 8-bit tonemapped screenshot of it. While a field is published,
`res/editor/shaders/ddgi_sample.comp` includes `erhe_ddgi.glsl` and calls
`ddgi_sample_irradiance` - the forward pass's function - with the forward
pass's light block contents (the `Probe_field` of `get_indirect_diffuse_field()`
in `src/editor/renderers/indirect_diffuse.{hpp,cpp}`, the single source of what
`Editor::tick()` publishes, plus the scene ambient) and the same three
atlases and sampler. The query machinery lives in `Ddgi_renderer`, but it
samples whichever producer is selected: DDGI's field or the radiance
cascades field ([radiance_cascades.md](radiance_cascades.md) "Reduce").
Each result is exactly what `standard.frag` multiplies by base colour and
occlusion, the configured `intensity` included.

- Arguments: `samples` (1 to `c_max_irradiance_query_points` = 4096 entries of
  `{position, normal}`; the normal is normalized) and an optional
  `view_position`. The view direction is the surface-to-viewer vector, like
  `standard.frag`'s `V`: `normalize(view_position - position)`, or the normal
  when `view_position` is omitted (the sample point is then biased by
  `normal_bias + view_bias` along the normal).
- Result: `source` (`"ddgi"`, `"radiance_cascades"` or `"ambient"`),
  `update_count` (the producer's update counter when the query was recorded,
  the counter `get_indirect_diffuse_stats` reports; 0 for `ambient`),
  `intensity` and `intensity_included: true` (with a field), `view_direction` (`"normal"` or
  `"toward_view_position"`), `point_count`, and `samples`, index-aligned with
  the input, each `{irradiance: [r, g, b]}`.
- With no active field the tool answers immediately with the flat scene
  ambient per sample - what the forward pass shades with then.
- Flow: the first MCP pass hands the points to
  `Ddgi_renderer::begin_irradiance_query()` and defers the request;
  `Editor::tick()` calls `record_irradiance_query()` with the field it just
  published, after the producers' updates, which fills a persistent
  host-visible input buffer, dispatches one
  thread per point into a persistent host-visible output buffer and records
  the frame index; later passes poll `poll_irradiance_query()`, which reads the
  results back once `Device::is_frame_completed()` reports that frame retired
  (typically two to three frames). One query runs at a time; the tool cannot
  run inside `batch`.
- The bind group layout carries the light block (binding 1), the input and
  output storage buffers (2, 3) and the three atlases as combined image
  samplers at user bindings 4-6, which Vulkan offsets to 8-10.

## Reference irradiance

The MCP tool `reference_indirect_diffuse` is the ground truth
`sample_indirect_diffuse` is measured against: a Monte Carlo estimate of the
irradiance at world points with exactly the light transport of a probe ray,
so the two differ only by the field's discretization (probe placement,
interpolation, visibility), never by what a ray sees.

- Estimator: from each point, `rays_per_point` cosine-distributed rays leave
  `position + normal_bias * normal` (default 0.01 m), each through
  `ddgi_trace_ray_radiance()` (`res/editor/shaders/erhe_ddgi_ray.glsl`, shared
  with `ddgi_trace.comp`): a front-face hit carries `shade_surface()` -
  direct lights with traced shadow rays, ambient x base colour, emission;
  single bounce, no field feedback - a backface hit carries 0, a miss the
  scene ambient as sky radiance. The miss distance is unbounded; the probe
  trace's `4 x` volume diagonal cannot be reached by content inside the
  volume, so the two agree.
- Convention: with the cosine pdf, the mean ray radiance is `E / pi`, the
  cosine-weighted mean radiance the irradiance blend stores. The result is
  that times the configured `intensity` - the quantity
  `ddgi_sample_irradiance()` returns, same units, `intensity` included the
  same way. `standard_error` is the standard error of the mean of the ray
  luminances (`0.2126 R + 0.7152 G + 0.0722 B`), times the intensity.
- Arguments: `samples` as for `sample_indirect_diffuse` (1 to 4096
  `{position, normal}`), `rays_per_point` (1 to 65536, default 4096), `seed`
  (default 1; ray directions hash `(seed, point, ray)`, so equal arguments
  give equal results), `normal_bias`; `view_position` is accepted and
  ignored. Points x rays per point is capped at `2^26`.
- Result: `source` `"reference"`, `convention`, `intensity`,
  `intensity_included: true`, `rays_per_point`, `seed`, `normal_bias`,
  `ddgi_enabled`, `point_count`, and `samples` index-aligned with the input,
  each `{irradiance: [r, g, b], standard_error, sky_fraction,
  backface_fraction}`.
- Runs whatever the selected source; needs ray query (the tool answers
  with an error otherwise) and one open scene with a camera (the light block
  fit needs one; the query fails with that reason otherwise).
- Flow: `Ddgi_renderer::begin_reference_query()` takes the points;
  `Ddgi_renderer::tick()` builds its trace inputs - `Scene_tlas`, light
  block, material set - once for both the probe update and a pending
  reference query, and records the query as `ddgi_reference.comp`
  dispatches of whole points, about `c_reference_rays_per_frame` (2^21) rays
  per frame, so no submission runs long enough to risk a GPU timeout. One
  workgroup per point reduces its rays in shared memory; the CPU turns the
  per-point sums into the estimate once the frame of the last chunk
  retired. 4096 points x 4096 rays take about a second. One query at a
  time; the tool cannot run inside `batch`, and a request that expires
  cancels the chunks not yet recorded.
- Verified analytically in `courtyard` with the sun off: a floor-centre point
  sees the sky through the 8 x 8 m opening 3 m up (form factor 0.687) and
  walls of radiance `0.8 x ambient` elsewhere, and the estimate matches
  `ambient x (F + 0.8 (1 - F))` within one standard error.

`scripts/gi_verify.py` compares every station's measured field against this
reference ([plans/radiance_cascades.md](../plans/radiance_cascades.md)
section 10, "Accuracy").

## Accuracy

Measured against `reference_indirect_diffuse` on the GI test stations
(numbers in [plans/radiance_cascades.md](../plans/radiance_cascades.md)
"Baseline (phase 0)"). The probes themselves are accurate: the irradiance a
probe stores, read back at the probe centre, is within 3 % of the reference
evaluated at the probe position for the same normal, except in directions
where the octahedral resolution limits it (below). What remains is the field's
discretization, which a spacing sweep separates from bias (DDGI / reference
of the group means; 128 and 512 rays per probe agree within 2 %):

| Group | 1.5 m | 0.75 m | 0.5 m |
|---|---|---|---|
| `cornell` floor | 0.92 | 0.86 | 0.86 |
| `emissive_only` room floor | 0.60 | 0.94 | 1.11 |
| `emissive_only` floor at the 1.0 m panel | 0.36 | 1.29 | 1.55 |
| `corridor` profile | 0.78 | 0.91 | 0.89 |
| `leak_pair` room A wall | 1.08 | 1.08 | 1.02 |

- **Probe-to-surface distance.** A surface gets the irradiance of the probe
  layer in front of it, not its own. Near the `cornell` floor the irradiance
  falls 18 % within 0.3 m of height, and the DDGI floor value equals the
  reference at the height of the nearest interior probe layer (0.15 m above
  the floor at 1.5 m spacing, 0.27 m at 0.5 m - the grid is fitted to the
  padded content box, so the layer height does not shrink monotonically
  with the spacing). Same for the pillar base (`probe_offset_sweep`): it
  gets the irradiance 0.7 m above the slab, at the pillar's relocated probe,
  and the face that probe left the pillar through decides which side of the
  base it serves (0.80 to 1.12 x the reference over three runs).
- **Near-field emitters.** In front of a small emitter the irradiance varies
  faster than the probe spacing; the error changes sign between 1.5 m and
  0.5 m spacing (probe layers closer to the panel centre than the floor).
- **Octahedral resolution.** At `irradiance_texels` 6 the nearest texel
  centre to a direction is up to 19.5 deg away (for -z, the octahedral
  corners); a probe facing a small bright source reads up to 7 % dark there
  (the `corridor` probes facing the lit end wall).
- **A thin wall through a probe plane** (`probe_offset_sweep` offset 0.0):
  each probe of that plane leaves the wall through its nearer face - which
  side is decided by the update's ray rotation - and serves that side only.
  On the other side the nearest probes are a full spacing away and the wall
  face reads their brighter irradiance (1.37 x the reference on room A's
  face when the probes went to room B, 1.16 when they went to room A). A
  probe 0.075 m from the lit face of a thin wall also leaks slightly into
  the room behind it: its distance lobe toward points behind the wall at
  grazing angles mixes wall hits with long rays, so the Chebyshev variance
  lets it through (0.35 % of the lit room at worst).

## Phases

The renderer is built out of these parts; the labels are cited from source
comments.

**1. `Scene_tlas`** (`src/editor/renderers/scene_tlas.{hpp,cpp}`) - the bottom
level cache, the per-frame-in-flight top level slots and the instance-record
SSBO, shared with `Ray_trace_renderer`. `Lightmap_baker` keeps its own copy of
this code because its instance records carry texcoord-2 addresses.

**2. Settings and skeleton.** `Ddgi_config`
(`src/editor/config/definitions/ddgi_config.py`, erhe_codegen, `reflect=True`,
shown in the Settings window): `probe_spacing_m`, `volume_padding_m`,
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
   `py -3 scripts/mcp_call.py set_indirect_diffuse {"source":"ddgi","show_window":true}`,
   `get_async_status`, `capture_screenshot`. Compare a Sponza / Bistro
   screenshot with DDGI off versus on: bounce colour on shadowed walls, no
   light through closed geometry.
2. Build the OpenGL configuration too, to confirm the `USE_DDGI`-off path still
   compiles and links.
3. Vulkan validation stays at zero errors - watch the image layout transitions
   between the trace and blend dispatches.
4. Regression: with the `ambient` source the frame matches the non-DDGI output, and a
   lightmap-baked scene looks unchanged with DDGI on.
5. Performance: `py -3 scripts/mcp_call.py get_indirect_diffuse_stats` (and
   the Ddgi window's "GPU time" section) report the per-pass GPU cost;
   `scripts/gi_verify.py` records it per test station.
6. GI test stations: `py -3 scripts/gi_verify.py --station all --source ddgi`
   builds the `creation_24_gi_test_rooms` stations, measures leak, placement,
   bounce, small emitters, far field, convergence, noise and cost through
   `sample_indirect_diffuse` / `get_indirect_diffuse_stats`, and prints them
   against the gates of [plans/radiance_cascades.md](../plans/radiance_cascades.md)
   section 10, which also holds the DDGI baseline.

## Future work

- [plans/ddgi.md](../plans/ddgi.md) - infinite bounces, sky radiance from the
  atmosphere LUTs, authored and cascaded volumes, the non-ray-query fallback.
- [plans/radiance_cascades.md](../plans/radiance_cascades.md) - radiance
  cascades as a second producer of this probe field.
