# Radiance cascades

Stability: experimental

World-space radiance cascades are the second producer of the runtime indirect
diffuse probe field, next to DDGI ([ddgi.md](ddgi.md)). The design, its
measurements and the remaining work are in
[../plans/radiance_cascades.md](../plans/radiance_cascades.md); this document
describes what exists: the source selection, the cascade layout and its
atlases, the interval trace, the merge, the reduce into the probe field
the forward pass samples, the probe overlay, the developer window and the
MCP tools.

The feature requires GPU ray query (`Device_info::use_ray_query`), like DDGI;
without it `Radiance_cascades_renderer::is_supported()` is false and its tick
does nothing.

## Source selection

`Editor_settings_config::indirect_diffuse_source`, the codegen enum
`Indirect_diffuse_source` (`src/editor/config/definitions/indirect_diffuse_source.py`),
selects the producer of the one probe field the forward pass samples:

| Value | Producer |
|---|---|
| `ambient` (default) | none: the flat scene ambient term |
| `ddgi` | `Ddgi_renderer` ([ddgi.md](ddgi.md)) |
| `radiance_cascades` | `Radiance_cascades_renderer` (this document) |

- Shown as the Source combo of the Settings window's Indirect Diffuse group,
  and repeated at the top of the DDGI and Radiance Cascades windows
  (`imgui_enum_combo()` in `src/editor/windows/config_ui.hpp`).
- Every change goes through `set_indirect_diffuse_source()`
  (`src/editor/renderers/indirect_diffuse.{hpp,cpp}`), called by the three
  combos when they report an edit and by the MCP tools `set_indirect_diffuse`
  and `set_ddgi`. It stores the value and hands each producer its
  `Producer_selection`: the deselected producer releases its textures right
  there, the selected one fits on its next tick. The producers are
  constructed with the selection of the loaded (and migrated) settings, and
  do not poll the setting.
- `Editor::tick()`, after `flush_draw_lists()`, ticks radiance cascades only
  while they are selected. DDGI ticks every frame for pending reference
  irradiance queries (which do not depend on the source) and updates its
  probes only while selected. `Editor::tick()` then publishes the selected
  producer's field - `get_indirect_diffuse_field()`
  (`src/editor/renderers/indirect_diffuse.{hpp,cpp}`), a `Probe_field` of
  `Ddgi_parameters` and the three atlases from `Ddgi_renderer::get_field()`
  or `Radiance_cascades_renderer::get_field()` - to
  `Forward_renderer::set_ddgi()`, and clears it when there is none (source
  `ambient`, or a producer without a field yet). That is the single
  publishing site; the MCP irradiance query samples the same field.
- Migration: the enum replaces `Ddgi_config::enabled` (removed in
  `Ddgi_config` v2). `Editor_settings_config` v5 added the enum and the
  `radiance_cascades` section; a migration callback registered by
  `Editor_settings_store` maps a pre-v5 file's `ddgi.enabled = true` to
  `ddgi` and anything else to `ambient`. It runs after the whole file is
  deserialized, when the removed field still holds the file's value.

## Settings

`Radiance_cascades_config` (`src/editor/config/definitions/radiance_cascades_config.py`,
erhe_codegen, `reflect=True`, the Settings window's Radiance Cascades
section, `editor_settings.radiance_cascades`):

| Field | Default | Meaning |
|---|---|---|
| `probe_spacing_m` | 1.5 | cascade 0 target spacing `s0` |
| `volume_padding_m` | 1.0 | growth of the content box before the fit |
| `max_probes_cascade0` | 65536 | cascade 0 probe budget; `s0` grows until the grid fits |
| `max_cascades` | 8 | upper bound on the cascade count (at most 12) |
| `cascade0_tile_texels` | 8 | `q0`, the cascade 0 octahedral tile side |
| `interval_scale` | 2.0 | `r0 = interval_scale * sqrt(3) * s0`, at least 1 |
| `texels_per_frame` | 131072 | trace budget: raw texels (one interval ray each) traced per frame |
| `hysteresis` | 0.9 | blend weight kept from a raw texel's previous value each time it is traced (less during a history reset, "Trace") |
| `direction_jitter` | `none` | `Radiance_cascades_direction_jitter`: `none` traces the texel centre direction, `footprint` a new random point of the texel footprint per trace ("Trace") |
| `bounces` | `single` | `Indirect_diffuse_bounces` (shared with DDGI): `single`, or `multi` - hits also sample the previous field ("Trace") |
| `merge_mode` | `per_neighbour_trace` | `Radiance_cascades_merge_mode`: `interpolate`, `visibility_masked` or `per_neighbour_trace` ("Merge"); not in the Settings window, edited with the Radiance Cascades window's combo and MCP `set_radiance_cascades`. The default is the mode that passed the most `gi_verify.py` gates (doc/plans/radiance_cascades.md section 10, "Merge mode default") |
| `debug_cascade_mask` | 0 | debug bitmask of the merge ("Merge"): bit `i` zeroes cascade `i`'s radiance, bit 12 the sky; not in the Settings window, edited with the Radiance Cascades window's checkboxes |
| `debug_draw_probes` | `none` | `Radiance_cascades_probe_overlay`: `none`, `state` or `state_and_irradiance` ("Probe overlay"); not in the Settings window, edited with the Radiance Cascades window and MCP `set_radiance_cascades` |
| `debug_draw_cascade` | 0 | the cascade the probe overlay draws, clamped to the fitted cascade count; edited like `debug_draw_probes` |

`Radiance_cascades_config` v2 added `texels_per_frame` and `hysteresis`, v3
`debug_cascade_mask`, v4 `merge_mode`, v5 `direction_jitter` and `bounces`,
v6 `debug_draw_probes` and `debug_draw_cascade`;
an older file reads them as the defaults. `per_neighbour_trace` is a later
enum value of the same v4 field, so a v4 file reads any of the three modes.
The defaults of `probe_spacing_m`, `cascade0_tile_texels`,
`interval_scale`, `texels_per_frame`, `direction_jitter` and `bounces` are
the point of the section 8 budget sweep (doc/plans/radiance_cascades.md
section 10, "Defaults") the user chose: accuracy at an update and full
refresh cost of about 1.5 x DDGI's on the test stations, with a full
refresh every update there.

The field's sampling parameters (irradiance / distance texels, depth
sharpness, biases, intensity) are the DDGI settings (`Ddgi_config`, passed
to the renderer's constructor), so both producers render the same way.

## Layout

The layout math is pure (`src/editor/renderers/radiance_cascades_layout.{hpp,cpp}`,
glm only) and unit tested by `editor_renderer_tests`
(`src/editor/renderers/test/`).

- **Content box.** `compute_padded_content_bounds()`
  (`src/editor/renderers/content_bounds.{hpp,cpp}`) - the union of the
  visible content meshes' world bounds, grown by the padding - shared with
  DDGI. So is the refit rule, `Probe_volume_bounds`
  (`src/editor/renderers/probe_grid.{hpp,cpp}`): the layout is refitted when a
  fit setting changes (to the content exactly), when the content leaves the
  volume (grown to include both) or shrinks below half of it (to the
  content), never on every content transform.
- **Cascade 0** is `fit_probe_grid()` of the box, DDGI's fit: counts from
  `s0`, at least 2 per axis, probe planes on the box faces, `s0` grown until
  the count fits `max_probes_cascade0`. The spacing is per axis; `r0` uses the
  largest one, so `r0 >= sqrt(3) * s0` bounds the cell diagonal on every axis.
- **Cascade i** (`get_upper_grid()`): spacing doubled, counts
  `ceil(lower / 2)` per axis, centred on cascade `i - 1`, so every cascade
  covers the same volume. Per axis, an even lower count puts the upper
  probes at the centres of the lower pairs (origin moved by half a lower
  spacing), an odd one on the even lower probes (origin unchanged, both
  span the same extent). A grid anchored at the lower origin instead
  drifts half a lower spacing toward `+x +y +z` per odd count: in the
  `corridor` station (9 x 11 x 54 cascade 0 probes) the single cascade 4
  probe per x / y sat at x 1.71, y 2.47, outside the 1.5 m wide corridor,
  so no interval of 13 - 27 m ever saw the lit end wall and the floor
  beyond 18 m read about 1e-9. Octahedral tile side
  `q_i = q0 * 2^i`. Interval `[r0 * (2^i - 1), r0 * (2^(i+1) - 1)]`
  (`get_radiance_interval()`), contiguous from cascade to cascade.
- **Cascade count**: cascades are added until the top cascade has at most 2
  probes on its longest axis, or `max_cascades` exist.
- **Atlas tiling**: each cascade's probe tiles (`q_i x q_i`, no border) wrap
  `probe_index = x + nx * (y + ny * z)` into rows of
  `min(ceil(sqrt(probes)), max_texture_size / q_i)` tiles
  (`Radiance_cascade::get_tile_origin()`), so the atlas stays close to square
  and both sides stay within `Device_info::max_texture_size`. In the
  `per_neighbour_trace` merge mode every cascade but the top one also has a
  neighbour atlas of 2 x 1 texels per raw texel ("Textures"); the fit
  settings then carry that block (`atlas_block_width` /
  `atlas_block_height`, `c_neighbour_block_width` / `_height`) and the
  row length is chosen so the neighbour atlas fits too. Cascade 0's merged
  atlas has 2 x 2 texels per raw texel in every mode
  (`c_merged_cascade0_block`, "Merge"), so cascade 0 always fits with at
  least that block. The block is part of the fit settings, so switching
  into or out of `per_neighbour_trace` refits. When a cascade would still
  exceed the limit, the cascade 0 budget is lowered (growing `s0`) and the
  fit repeated; at the default 16384 limit the blocks change no layout of
  the stations (unit tested).
- **Merge math**: `get_child_texels()` - cascade `i` texel `(u, v)` covers
  cascade `i + 1` texels `(2u .. 2u+1, 2v .. 2v+1)`, the octahedral nesting of
  the merge; `get_upper_probes()` - the 8 cascade `i + 1` probes of a cascade
  `i` probe with their trilinear weights: per axis, with an even lower count
  lower probe `k` sits at upper grid coordinate `k / 2 - 1 / 4` (weights
  0.25 / 0.75), with an odd one at `k / 2` (weights 1 / 0 for even `k`,
  0.5 / 0.5 for odd `k`); indices clamped at the upper grid's edges. `octahedral_encode()` /
  `octahedral_decode()` follow `res/shaders/erhe_ddgi.glsl`.

Because the upper counts round up, small upper cascades do not halve the
texel count exactly: the total is above the paper's `2 * M0` bound for a
small volume (the window and the MCP stats report the real numbers).

## Textures

`Radiance_cascades_renderer` (`src/editor/renderers/radiance_cascades_renderer.{hpp,cpp}`)
is constructed in `editor.cpp`'s `post_processing_task` next to
`Ddgi_renderer`, owned by `Editor`, and published as
`App_context::radiance_cascades_renderer`. Per cascade it allocates two
atlases, RGBA16F, storage + sampled + transfer: **raw** (the traced
intervals, rgb radiance and a transparency beta, the cascade's atlas size)
and **merged** (raw merged with everything beyond it, "Merge"; the atlas
size, for cascade 0 twice it per axis: cascade 0 is merged at cascade 1's
angular resolution). Cascade 0 also has a **distance** texture of its atlas
size, RGBA32F: the hit distance statistics of each raw texel (see "Trace"),
which the reduce pass turns into the distance moments and the probe
classification. In the
`visibility_masked` merge mode every cascade also has a **probe state**
texture, R32F, one texel per probe at its tile coordinates
(`tiles_per_row x tile_rows`): the visibility pass's bits ("Merge"); it is
allocated on the first tick in that mode (and after each refit in it) and
released when the mode is left. All are cleared to zero at allocation,
recorded into the frame command buffer, and left in
`shader_read_only_optimal`. Texture memory per cascade is
`2 x atlas width x atlas height x 8` bytes (cascade 0: 5 x, the merged atlas
being 4 times the raw one), plus `tiles_per_row x tile_rows x 4` bytes for
the probe state and `atlas width x atlas height x 16` bytes for cascade 0's
distance texture. In the `per_neighbour_trace` merge mode every cascade but
the top one also has a **neighbours** texture, RGBA16F, 2 x 1 texels per
raw texel (`2 x atlas width` by `atlas height`): the visibilities of the 8
connecting segments of each texel, one channel each ("Trace", "Merge"),
segment `n` of raw texel `(x, y)` in channel `n & 3` of texel
`(2 x + (n >> 2), y)` (`rc_neighbour_texel()` in
`res/editor/shaders/erhe_rc_upper.glsl`), allocated with the layout in that
mode.
The probe field atlases of "Reduce" come on top (the reported total
`texture_bytes` includes them).

## Trace

`res/editor/shaders/rc_trace.comp`, recorded by
`Radiance_cascades_renderer::tick()` into the frame command buffer after the
refit (doc/plans/radiance_cascades.md section 5, pass 1).

- **Texel order and budget.** All cascades' probe texels form one global
  order: cascade 0 first, and within a cascade
  `texel_index = probe_index * q^2 + v * q + u`
  (`probe_index = x + nx * (y + ny * z)`, `(u, v)` the texel of the probe's
  `q x q` tile). A cursor walks this order; each frame traces the next
  `texels_per_frame` texels (clamped to the total, so no texel is traced
  twice in one frame), wrapping to cascade 0 after the last cascade. Each
  contiguous run inside one cascade is one dispatch of one thread per texel
  (64-wide workgroups, runs split at 65535 workgroups), with its own control
  block from the ring buffer (cascade grid, interval, tile side, tiles per
  row, run start and length, flags, hysteresis). A dispatch declares a write
  of its whole raw atlas, so a second run of a cascade in the same frame
  (the cursor wrapping into the cascade it started in, or a run split at the
  dispatch limit) is ordered after the first by a barrier; runs of different
  cascades write different images and need none. Two pipeline variants of
  the shader (`ERHE_RC_TRACE_WRITE_DISTANCE`) keep the upper cascades from
  referencing the distance texture, so only cascade 0 dispatches count as
  its writers (Vulkan synchronization validation). The sizes are CPU-known; there is no
  indirect dispatch.
- **Ray.** From the probe's grid position (no relocation) along the
  octahedral decode of the texel centre (`get_texel_direction()`, the same
  convention as `erhe_ddgi.glsl`), over the cascade's interval
  `[t_i, t_{i+1}]`: `ray query` with `tmin = t_i`, `tmax = t_{i+1}`.
- **Direction jitter** (`direction_jitter` `footprint`): the ray instead
  follows a random point of the texel's footprint in the octahedral
  parameter, a hash of the texel's global index and a per-update seed
  (control block `run`), new every time the texel is traced, in every
  cascade; the hysteresis blend averages the traces into the footprint
  mean. Without it every trace samples the same centre direction, which
  aliases whatever a texel's footprint holds besides it - a small emitter,
  a sunlit patch, a wall edge (measured in
  [../plans/radiance_cascades.md](../plans/radiance_cascades.md) section
  10, "Phase 6").
- **Transport** is DDGI's (`ddgi_trace_ray_segment()` in
  `res/editor/shaders/erhe_ddgi_ray.glsl`, whose `[0, t_max]` form is the
  DDGI probe and reference ray): a front face hit carries `shade_surface()`
  (direct lights with traced shadow rays, scene ambient x base colour,
  emission), beta 0; a backface hit carries radiance 0, beta 0; a miss
  carries radiance 0, beta 1 - the merge pass adds what lies beyond the
  interval.
- **Hysteresis and history reset.** The result is blended into the raw
  atlas as `mix(traced, history, h)`, with the texel's `h` from the
  temporal history (`Temporal_history`,
  `src/editor/renderers/indirect_diffuse.{hpp,cpp}`, and
  `res/editor/shaders/erhe_temporal_history.glsl`, shared with DDGI;
  [ddgi.md](ddgi.md) "History reset"): after a reset the k-th trace of a
  texel is blended with `min(hysteresis, k / (k + 1))`, so its first trace
  replaces the history and the next ones form the running mean until it
  reaches `hysteresis`. Every allocation resets (the first fill: the
  history is the allocation clear), and so do the committed changes - a
  geometry edit, a removal, `Scene_lighting_changed_message` (committed
  node transforms, content or lights added or removed, light and material
  edits; a live drag does not reset, [ddgi.md](ddgi.md) "History reset") -
  at the next update, at the cursor.
  Without direction jitter a static scene is exact after one full sweep
  after a reset. The control block carries the reset state per run
  (`history`), and the run's first texel in the global order (`run.x`),
  which is the texel index the history counts with.
- **Connecting segments** (merge mode `per_neighbour_trace` only): after
  the raw runs, the same runs are traced again with the neighbour variant
  (`ERHE_RC_TRACE_NEIGHBOURS 1`, `record_neighbour_trace()`), for every
  cascade but the top one. Per texel, for each of the 8 upper probes
  `u_n` of the merge stencil (`get_upper_probes()`, shared by the
  shaders through `erhe_rc_upper.glsl`) the segment from the END of the
  texel's interval, `p + t_{i+1} d`, to `u_n`'s interval start
  `u_n + t_{i+1} d` (`d` the direction the raw trace used this update,
  jittered with it; the segment vector is `u_n - p`) is a visibility
  test: `segment_free()` (`res/shaders/erhe_ray_hit.glsl`), a ray query
  that ends at the first hit of any face and fetches nothing, 1 when the
  segment is free. The 8 visibilities are blended into the texel's 2 x 1
  block of the neighbour atlas with the texel's history hysteresis. Upper
  probes of weight 0 (odd lower counts) are not traced and store 0. The
  variant writes only the neighbour atlas. **Budget**: `texels_per_frame`
  counts texels, as in the other modes, so a texel below the top cascade
  costs its raw ray plus up to 8 visibility rays. The segments run in
  their own pass rather than in the merge: the merge runs over every
  texel each update, so tracing there would make its cost 8 rays per
  texel of the whole layout per update, outside the budget, and would
  drop the hysteresis the raw intervals have.
- **Cascade 0 distance statistics.** Cascade 0 runs also blend, with the
  texel's hysteresis, the texel's hit statistics into the distance texture
  (same texel address as the raw atlas): x the mean distance `d`, y the
  mean `d^2` (`d` the hit distance, front or back face, or `r0` on a miss)
  along the texel's CENTRE direction, and z the backface fraction of the
  raw traces. With direction jitter the centre distance comes from an extra
  unshaded ray (`trace_closest_from()`): the Chebyshev test needs the
  distance along one direction, and the spread of a jittered footprint
  (wall hits mixed with grazing misses) would widen the variance and let
  light through thin walls.
- **Bounces** (`bounces`). Every trace variant is compiled with
  `ERHE_RT_INDIRECT_FIELD` and binds the probe field atlases as samplers
  (user binding points 4 - 6, Vulkan 8 - 10). With `multi` the trace's
  light block carries this producer's field - the previous update's reduce
  output, intensity 1 - and `shade_surface()` takes a hit's ambient term
  from `ddgi_sample_irradiance()`, so light bounces once more per update;
  with `single` the light block carries no field and hits take the flat
  scene ambient ([ddgi.md](ddgi.md) "Bounces").
- **Trace inputs** are built as for DDGI: the scene root's forward
  `Material_set`, a `Light_buffer` with projections fitted by
  `fit_trace_light_projections()` (`src/editor/renderers/trace_lights.{hpp,cpp}`,
  shared with `Ddgi_renderer`; no trace without a scene camera), and the
  renderer's own `Scene_tlas`, rebuilt each frame it traces. Binding points
  follow DDGI's trace layout: material 0, light 1, control 2, instance
  records 3, TLAS 4, raw atlas 5 (`rgba16f`), distance 6 (`rgba32f`),
  neighbour atlas 7 (`rgba16f`; the raw variants bind their raw atlas
  there, unreferenced), the field samplers (user 4 - 6); the texture heap
  is set 1.
- **Timing.** One explicit-range `Gpu_timer` per pass
  (`Scoped_gpu_timer`, plot names `RC trace`, `RC neighbour trace` and
  `RC merge`) brackets all of
  a frame's dispatches of that pass. `Radiance_cascades_renderer::get_stats()`
  derives, like DDGI's: `trace`, `neighbour_trace` (0 outside its mode),
  `merge` and their sum `total`, each last /
  mean over the last 60 samples; `texels_per_update`, `rays_per_update`
  (one ray per texel plus the connecting segments) and
  `neighbour_rays_per_update` (the visibility segments: their count per full sweep,
  from `get_upper_probes()` at allocation, spread over the updates of a
  sweep), `ms_per_million_rays` (total mean per million rays),
  `updates_per_full_refresh` = ceil(total texels / texels per update) and
  `full_refresh_ms` (updates x total mean), `update_count`,
  `timing_sample_count`, `completed_sweeps` and `history_reset_count`
  (temporal history resets). The timing history is cleared when another
  source or another merge mode is selected.

## Merge

`res/editor/shaders/rc_merge.comp`, recorded by
`Radiance_cascades_renderer::record_merge()` right after the trace of every
tick that traced (doc/plans/radiance_cascades.md section 5, pass 2). Every
merged texel depends on raw texels of its own and every higher cascade, so
each update re-merges every cascade; the cost is proportional to the texel
count, not to the trace budget.

- **Equations** (paper 2.3.1). Each texel is the interval `[t_i, t_{i+1}]`
  of one probe direction, and its merged value is the interval merged with
  everything beyond it: `merged.rgb = raw.rgb + raw.a * upper.rgb`,
  `merged.a = raw.a * upper.a`. The merged alpha is the transparency of the
  whole ray from `t_i` on: the fraction that escapes past the top cascade.
- **Top cascade**: `upper = (sky, 1)`. The sky is the scene ambient, the
  radiance the DDGI probe rays return on a miss.
- **Lower cascades**: `upper` is the merged atlas of cascade `i + 1`,
  interpolated to the probe - the 8 upper probes and trilinear weights of
  `get_upper_probes()` ("Layout") - and averaged at
  each upper probe over the 2x2 child texels of `get_child_texels()`, the
  directions nesting in this texel.
- **Cascade 0 at child resolution**: cascade 0 is merged into 2 x 2 texels
  per raw texel (`c_merge_flag_child_resolution`): merged texel `(U, V)` is
  raw texel `(U / 2, V / 2)` merged with the upper value of the one
  cascade 1 texel `(U, V)` instead of the 2x2 mean. The mean of the 4 is
  the texel merged at its own resolution; the reduce convolves the far
  field at cascade 1's angular resolution ("Reduce"). **Border rule**: upper indices are
  clamped to the upper grid (clamp to edge). A lower probe beyond the
  outermost upper probe of an axis - the first and the last lower index
  when the lower count is even - puts that axis' 0.25 weight on the
  same edge probe as its 0.75 weight, so it takes the edge probe's value
  on that axis instead of extrapolating; with an odd lower count the edge
  probes coincide with upper probes.
- **Merge modes** (`merge_mode`, three variants of the shader,
  `ERHE_RC_MERGE_MODE` 0 / 1 / 2, the enum values). `interpolate`: all 8
  upper probes with their trilinear weights. `per_neighbour_trace`: each
  upper probe is weighted by the visibility `V_n` of its connecting
  segment ("Trace") - from the end of the texel's interval to the upper
  interval's start - and the weights are renormalized,
  `upper = sum_n w_n V_n upper_n / sum_n w_n V_n`, `(0, 0)` when none is
  visible. The texel's own interval stays the ray along its direction; an
  upper interval that starts behind a wall from the lower ray's end is left
  out. (The segment was first a radiance interval replacing the texel's
  own - the community "bilinear fix" - which bends the direction toward the
  upper probe by up to `atan(1.5 / interval_scale)` at cascade 0 and made
  floor probes see the sunlit floor through their horizon texels;
  doc/plans/radiance_cascades.md section 10, "Phase 6".) The top cascade
  takes the `interpolate` variant (it merges with the sky in every mode).
  `visibility_masked`: only usable upper probes
  take part: the
  lower probe's state marks the segment to that upper probe unobstructed,
  and the upper probe's state does not mark it inside geometry. The
  trilinear weights of the usable probes are renormalized to sum to 1.
  No mode is better everywhere (measured under "Verification"), so
  all three stay selectable; `set_merge_mode()` is called by the change sites
  (the Radiance Cascades window combo, MCP `set_radiance_cascades`) after
  they store the setting, and the constructor takes the loaded one.
  With no usable upper probe, `upper = (0, 0)`: nothing beyond the
  interval is known, and the texel keeps its own interval. The fallback is
  rare and did not change a measured value: on every station the results
  with `(0, 0)` and with the unfiltered trilinear blend as the fallback
  were identical to four digits. Few probes have no usable upper probe:
  none of `cornell`'s interior cascade 0 - 2 probes, 20 of `leak_pair`'s
  180 interior cascade 1 probes (the layer 3 cm above the floor). `(0, 0)`
  is kept because it never carries light across a wall.
- **Visibility pass** (`res/editor/shaders/rc_visibility.comp`,
  `record_visibility()`, `visibility_masked` mode only): one dispatch per
  cascade, one thread per probe,
  writing the probe state texture as an integer-valued float. Bit 8: the
  probe is inside geometry, by DDGI's classification rule
  (`ddgi_relocate.comp`): more than a quarter of 64 fixed spherical
  Fibonacci directions hit a backface, and the nearest hit is a backface
  (rays over `[0, top interval end]`, through the shared hit path
  `trace_closest_from()`). Bits 0 - 7: the segment from the probe to upper
  probe `n = i + 2 j + 4 k` (the `get_upper_probes()` order) has no hit,
  front or back; 0 for the top cascade. The states depend only on the
  scene geometry and the layout, so the pass runs when either changed, not
  per frame: after every allocation (refit), after switching into the mode,
  and after the change messages
  `Node_touched_message`, `Mesh_geometry_changed_message` and
  `Items_removed_message`, in the next tick before the trace. It is timed
  by its own `Gpu_timer` (`RC visibility`), reported as the last
  measurement and a run count. Content added without a refit and without
  one of those messages (a new part inside the current volume) does not
  mark the states stale (doc/plans/radiance_cascades.md section 9,
  "Remaining work").
- **Order and bindings.** One dispatch per cascade, top cascade first,
  one thread per atlas texel (8 x 8 workgroups over the atlas; the unused
  tiles of a partly filled last row return at once). The raw atlas and the
  merged atlas of the cascade above are combined image samplers read with
  `texelFetch` (the raw atlases are `shader_read_only_optimal` after the
  trace; the upper merged atlas is moved there after its own dispatch,
  which orders its writes before the next dispatch's reads); the merged
  atlas being written is the only storage image, so each dispatch has
  exactly one write. The probe states of the cascade and of the cascade
  above and the cascade's neighbour atlas are sampled too (only the
  variant of their mode references them; the raw atlas is bound in their
  place otherwise). The top cascade binds its
  own raw atlas and state for the unused upper samplers. Binding points: control block 2, raw
  sampler 0, upper sampler 1, state 2, upper state 3, neighbour atlas 4
  (Vulkan offsets the samplers past the control block, to 3 - 7), merged
  storage image 8.
- **Debug cascade mask** (`debug_cascade_mask`, read every tick): a masked
  cascade contributes its transparency but no radiance, and bit 12 masks
  the sky, so the merged atlases show only the unmasked interval bands, as
  in the paper's figure 3. The merge is linear in the interval radiances,
  so the merged values of the single bands add up to the merged value of
  all bands.
- **Accuracy.** The merge is exact where the upper intervals see what the
  lower probe's ray sees beyond `t_{i+1}`; both modes interpolate them
  from probes that sit elsewhere, up to three quarters of an upper spacing
  away, and the interval of an upper probe starts at `upper + t_{i+1} d`,
  not at `lower + t_{i+1} d`. That **start-point parallax** is the error
  both modes share: the start can lie on the other side of a wall, so
  room B of `leak_pair` reads light from room A through texels whose
  cascade 1 upper probes are all visible. `visibility_masked` removes the
  upper probes behind a wall or inside a part, which reduces that leak,
  but the same test also removes coarse probes that lie outside a closed
  room or inside its walls whose intervals, starting back inside the
  room, carried part of its far field - so in `cornell` the far field gets
  darker. The `per_neighbour_trace` merge mode tests the visibility from
  the lower ray's end to each upper interval start, so an upper interval
  behind a wall is not continued: it removes the start-point parallax;
  what remains is the
  upper interval's own offset (it starts at `u_n + t_{i+1} d`, up to
  three quarters of an upper spacing from the lower ray) and the child
  directions of the upper value, whose interval starts lie up to
  `t_{i+1}` times half a lower texel from the tested point
  ("Verification").

## Reduce

`res/editor/shaders/rc_reduce.comp`, recorded by
`Radiance_cascades_renderer::record_reduce()` right after the merge of every
tick that traced (doc/plans/radiance_cascades.md section 2 and section 5,
pass 3). It writes the probe field in exactly the DDGI atlas format
([ddgi.md](ddgi.md) "Data layout"), so the forward pass, the
`sample_indirect_diffuse` query and the `DDGI Irradiance` debug mode sample
it with `erhe_ddgi.glsl` unchanged.

- **Grid.** The field's grid is cascade 0's: its origin, spacing and counts
  are the `Ddgi_parameters` `get_field()` publishes, with the DDGI
  sampling settings (irradiance / distance texels, depth sharpness, normal
  / view bias, intensity). Probes are not relocated: the probe data offset
  is 0.
- **Atlases** (`allocate_field()`, on every refit and when a field sampling
  setting changes): irradiance RGBA16F and distance RG16F, tiles of
  `texels + 2` with the 1-texel octahedral border, and the probe data
  RGBA32F, placed by the shared DDGI tiling (`get_probe_field_tile()`,
  [ddgi.md](ddgi.md) "Data layout"): cascade 0 densities overflow the
  classic one-row-per-y-layer layout (64 x 16 x 64 probes with 16-texel
  distance tiles would be 65536 texels wide), and the wrapped rows keep
  both sides within `max_texture_size`. The cascade 0 budget is capped so
  the field atlases fit (`get_probe_field_max_probes()`). The field
  sampling settings live in `Ddgi_config`, which the Settings window edits
  through reflection and MCP `set_ddgi` without a change notification, so
  `update_layout()` compares them with the ones the field was allocated
  with, as it compares the fit settings.
- **Irradiance** (variant `ERHE_RC_REDUCE_DISTANCE 0`). The input is
  merged cascade 0 at child resolution: `(2 q0)^2` texels per probe, each
  the full-range radiance averaged over its footprint, so the texel for
  direction `n` stores `sum_j W_j(n) L_j / sum_j W_j(n) (1 - b_j)` with
  `W_j(n) = integral over footprint j of max(0, n . w) dw` - the
  cosine-weighted mean radiance (`E / pi`) DDGI's irradiance blend stores -
  and `b_j` the backface fraction of the texel's cascade 0 parent (its
  radiance is the mean over all traces, backface ones contributing 0, so
  only the normalization drops them; without jitter `b_j` is 0 or 1 and
  this skips backface texels, as the DDGI blend skips backface rays). At
  cascade 0's own resolution (`q0` 4, a footprint about 45 degrees wide)
  the cosine weighting of footprint means overstates radiance that sits
  near the horizon of a texel: a floor probe's texels that hold both the
  sky and a sunlit wall near the horizon read up to 15 % bright (measured,
  doc/plans/radiance_cascades.md section 10, "Phase 6"); the child
  resolution removes most of it for the far field at no trace cost.
- **Distance** (variant 1). The blended moments `(d, d^2)` of the
  cascade 0 texels (distance statistics x, y; `d <= r0`), weighted with
  `integral over footprint j of pow(max(0, n . w), depth_sharpness) dw`.
  A miss stores `r0` (the interval end), so a free direction reads at
  least `r0 >= sqrt(3) * s0`, the cell diagonal: the Chebyshev test of a
  shading point against a probe of its cell passes unless a surface lies
  between them within cascade 0's interval.
- **Weights.** `W_j(n)` depends only on the tile sizes and the lobe
  exponent, never on the probe, so it is integrated on the CPU
  (`compute_octahedral_lobe_weights()` in
  `src/editor/renderers/radiance_cascades_layout.{hpp,cpp}`, unit tested):
  each input texel is split into about `32 / q` squares per axis of
  the octahedral parameter, each contributing at its centre direction `w`
  with the solid angle `dA * |w|_1^3` (the octahedral map's area element).
  Each output texel stores a sparse list of the input texels its lobe
  reaches - weights above `c_reduce_weight_threshold` (1e-4) of its largest
  one: a header of (first pair, pair count) per output texel, then
  (input texel, weight) pairs - for the irradiance (`(2 q0)^2` inputs) and
  the distance (`q0^2` inputs) lobes, followed by the cascade 0 texel
  solid angles, in one read-only storage buffer, created on change only.
  The dense loop spent most of the reduce, the largest pass, on zero
  weights.
- **Probe state** (written by the irradiance variant): inactive when more
  than a quarter of the probe's sphere, by solid angle weighted with each
  texel's backface fraction, hit a backface - DDGI's classification rule on
  cascade 0's rays. It is the one rule in both merge modes: the `visibility_masked`
  mode's inside bit (64 rays over the full range, nearest hit a backface)
  answers a different question - whether the probe's merged intervals can
  carry light - and does not classify the field.
- **Shape.** One workgroup of 64 invocations per cascade 0 probe (a 2D
  grid of workgroups, rows of 32768), striding over the tile's texels; the
  probe's `(2 q0)^2` merged child texels and `q0^2` distance statistics
  are read once into shared memory (tiles up to 16 x 16; the merged tile
  side `2 q0` rides in the control block, `weights.w`), then the border is
  filled with DDGI's border copy (`res/editor/shaders/erhe_ddgi_border.glsl`).
  No hysteresis: the raw intervals are blended over time already, and the
  field is rewritten from merged cascade 0 every update. The two variants
  write different images, so they need no barrier between them; the
  atlases are left `shader_read_only_optimal`.
- **Timing**: its own `Gpu_timer` (`RC reduce`), the `reduce` pass of
  `get_stats()`, included in the total and the cost figures.

## Probe overlay

`Radiance_cascades_renderer` is a `Renderable`, registered with
`App_rendering` next to `Ddgi_renderer` in `editor.cpp`: while radiance
cascades are active and `debug_draw_probes` is not `none`, `render()`
draws into every viewport the box of cascade `debug_draw_cascade`'s probes
and one wire sphere per probe of it (radius 0.06 of its smallest spacing),
in the CPU phase only ([ddgi.md](ddgi.md) "Traps"). It is an editor aid:
`render_scene_image` does not draw renderables, so only
`capture_screenshot` (and the windows) show it.

- **State colour**: green active, red inside geometry, grey unclassified.
  Cascade 0 is classified by the field probe data (the reduce's
  classification, "Reduce"); any cascade by the inside bit of its probe
  state texture in the `visibility_masked` merge mode ("Merge"); a probe
  counts as inside when either says so. The upper cascades of the other
  merge modes have no classification and draw grey.
- **Irradiance** (`state_and_irradiance`): a short thick line from each
  probe toward +Y in the colour of its merged irradiance toward +Y - the
  mean of the cascade's merged tile (cascade 0: its child resolution
  tile) weighted with `max(0, w.y)` times each texel's solid angle, the
  cosine-weighted mean radiance the irradiance atlas stores - normalized
  by the brightest channel over the cascade's probes of the copy, so the
  lines show relative brightness and colour bleeding.
- **Readback**: the tick records a copy of what the chosen mode needs -
  the cascade's probe state texture, cascade 0's field probe data, and
  with `state_and_irradiance` the cascade's merged atlas - into one
  host-visible buffer after the reduce, at most one copy in flight and
  at most one every 10 frames (`c_probe_overlay_interval_frames`), and
  reduces a retired copy on the CPU into a per-probe list (capacity kept).
  A copy is drawn only while its grid is the chosen cascade's current
  grid, so after a refit or a cascade change the probes draw grey until the
  next copy retires. With `none` nothing is copied and the buffer is
  released; `probe_overlay.readback_count` of the MCP stats stays put.

## Radiance Cascades window

The developer window `Radiance_cascades_window`
(`src/editor/developer/radiance_cascades_window.{hpp,cpp}`, ini label
`radiance_cascades`) shows the source combo and, while radiance cascades are
selected, the probe field's grid and tiling, a table with one row per
cascade - probe counts, probe count, spacing, tile size, interval, texels,
atlas size and memory - plus totals, the cascade 0 origin and `r0`; the
trace, merge, reduce and total GPU times, the
visibility pass's last time and run count, and the
cost figures; the debug cascade mask as one checkbox per cascade plus Sky
(checked bands contribute radiance; they edit `debug_cascade_mask`); and an
atlas preview of a chosen cascade, atlas (raw or merged) and channel
(radiance with a scale, beta, or cascade 0 distance statistics - green
front face, red backface (backface fraction above one half), brightness
the mean distance / `r0`); cascade 0's merged atlas shows its child
resolution; and the probe overlay's mode and cascade with the counts of
the last copy ("Probe overlay").

The atlases carry beta in alpha, which the ImGui image widget would use as
opacity (every hit texel would vanish), so the preview is an opaque copy
written by `res/editor/shaders/rc_preview.comp` into one preview texture
(`Radiance_cascades_renderer::request_preview()`), after the trace and the
merge. The window requests it each frame it shows the preview and the next
tick records it, so the copy costs nothing while the window is closed.

## MCP

- `set_indirect_diffuse {source, show_window}` selects the producer
  (`"ambient"`, `"ddgi"`, `"radiance_cascades"`) and returns the source and
  whether each producer is supported. `set_ddgi {enabled}` remains a
  shorthand: true selects DDGI, false returns a DDGI selection to ambient.
- `set_radiance_cascades {probe_spacing_m, volume_padding_m,
  max_probes_cascade0, max_cascades, cascade0_tile_texels, interval_scale,
  texels_per_frame, hysteresis, merge_mode, debug_cascade_mask,
  direction_jitter, bounces, debug_draw_probes, debug_draw_cascade,
  show_window}` writes the settings (explicit arguments, omitted ones
  unchanged) and returns the layout of the last fit plus the stored
  `config`; the renderer refits on its next tick.
- `get_indirect_diffuse_stats` reports `source` (the selected value) and a
  `radiance_cascades` object: `supported`, `active`, `has_field` (the probe
  field atlases exist), `field` (`grid_origin`, `grid_spacing`,
  `grid_counts`, `irradiance_texels`, `distance_texels`,
  `depth_sharpness`, `tiles_per_row`; null without a field), `merge_mode`,
  `cascade_count`, `r0`, the total `probe_count`,
  `texels` and `texture_bytes`, `fit_count`, and `cascades`, one entry per
  cascade with `grid_origin`, `grid_spacing`, `grid_counts`, `probe_count`,
  `tile_texels`, `interval` `[start, end]` in metres, `texels`, `atlas_size`
  and `texture_bytes`, and the trace cost ("Trace"): `update_count`,
  `timing_sample_count`, `completed_sweeps`, `texels_per_update`,
  `rays_per_update`, `neighbour_rays_per_update`, `gpu_ms`
  `{trace, neighbour_trace, merge, reduce}` and `gpu_ms_total` (their
  sum; each `last_ms`, `average_ms`), `timing_history_size`, `ms_per_million_rays`,
  `updates_per_full_refresh` and `full_refresh_ms`, `visibility`
  `{last_ms, update_count}` (the visibility pass, "Merge"),
  `history_reset_count` and `probe_overlay` `{readback_count, cascade,
  probe_count, active, inside, unclassified, irradiance, update_count}`
  (the last retired overlay copy, `cascade` -1 without one; "Probe
  overlay") - the fields
  `scripts/gi_verify.py` reads for both sources.
- `get_radiance_cascades_texels {texels}` reads raw and merged texels back,
  on request only: it asks `Radiance_cascades_renderer::request_texel_readback()`
  for a copy of every raw and merged atlas and the distance texture after
  the next trace and merge
  (one host-visible buffer, allocated on the first request and grown to the
  largest layout asked for) and answers once that frame retired
  (`poll_texel_readback()`; a few frames, so not inside `batch`). Needs the
  source active. Result: `update_count` and `completed_sweeps` at copy time;
  `cascades`, per cascade `{index, texel_count, mean_radiance,
  beta_one_fraction, mean_merged_radiance, mean_merged_beta,
  inside_probe_count, upper_visible_fraction}` (raw beta > 0.5: the
  interval is empty along that direction; the merged beta is the escaping
  fraction; the probe states: probes inside geometry, and the mean
  fraction of upper probes a probe can see), cascade 0 also `backface_fraction` and `backface_probe_count`
  (probes with at least one backface texel - probes inside geometry); and
  `texels`, index-aligned with the optional input `[{cascade, probe:[x,y,z],
  texel:[u,v]}]` (at most 4096): `{cascade, probe, texel, probe_position,
  direction, interval, radiance, beta, merged_radiance, merged_beta,
  probe_inside, upper_visible_mask}` (the probe's state, "Merge"),
  cascade 0 also `merged_children` (the 4 child resolution texels, rgba,
  child `x + 2 y`; `merged_radiance` / `merged_beta` are their mean),
  `signed_distance` (the mean centre distance, negative when the backface
  fraction exceeds one half), `distance_mean_squared` and
  `backface_fraction`. With a field the copy also holds the
  three field atlases: `field` (the parameters they were copied with) and
  `field_texels`, index-aligned with the optional input
  `field_texels: [{probe:[x,y,z], texel:[u,v], atlas}]` (cascade 0 probe,
  interior texel of the tile, `"irradiance"` or `"distance"`):
  `{atlas, probe, texel, direction, probe_state, irradiance | moments}`.
- `sample_indirect_diffuse` samples the radiance cascades field while it
  is the selected source (`source` `"radiance_cascades"`, `update_count`
  the renderer's), through the same `ddgi_sample.comp` as DDGI
  ([ddgi.md](ddgi.md) "Irradiance queries").

## Verification

All checks below ran at the defaults of "Settings" (`s0` 1.5 m, `q0` 8,
`interval_scale` 2); the per-mode ones for every merge mode.

- Trace against an analytic ground truth,
  `py -3 scripts/rc_texel_verify.py [--station ...] [--reuse]` (self-launching
  headless editor with config backup / restore like `gi_verify.py`; exit
  1 on any failure; tolerances and the skip rule in its docstring; direction
  jitter `none` and single bounce pinned): every raw texel of every cascade
  of `cornell`, `emissive_only`, `courtyard`, `leak_pair` and `corridor`
  (read with `get_radiance_cascades_texels` after two sweeps) matches a CPU
  ray / axis-aligned box intersection of the station's parts over the
  texel's interval, shaded with the `shade_surface()` formula (GGX +
  Lambert, spot / directional light with range, cone and shadow tests,
  ambient, emission): hit / miss / backface agree for every texel, cascade
  0 distance within 1 mm, front face radiance within 0.1 % (half-float
  storage; the script's tolerance is 0.5 %), the emitter panels exactly
  4.0. Texels whose reference hit lies within 0.1 mm of an interval end, at
  ties between coincident faces of touching parts (the floor top under a
  wall bottom), or whose ray passes within 0.1 mm of a part's edge or
  corner are skipped and counted: up to 2 % of cascade 0's texels, because
  the round probe grid puts rays exactly through room corners.
- Merge, same script and readback ("Merge"):
  - **Exact algebra** (`interpolate`, `visibility_masked`): every merged
    texel of every cascade equals the merge recomputed on the CPU from the
    read-back raw texel, the read-back merged texels of the cascade above
    and the read-back probe states (8 upper probes with the documented
    weights and border rule, the unusable ones skipped and the rest
    renormalized, 2x2 child average; sky for the top cascade), and every
    cascade 0 child texel the raw texel merged with its one child's upper
    value: 0 failures, worst relative difference 0.1 % (the half-float
    store).
  - **Per-neighbour exact check** (`per_neighbour_trace`; the visibilities
    are not read back): 1500 sampled texels per cascade below the top one,
    each merge (and each cascade 0 child) recomputed from CPU ground-truth
    visibilities of the connecting segments (box crossings from
    `p + t_{i+1} d` to `u_n + t_{i+1} d`), the read-back raw texel and the
    read-back upper texels; the top cascade takes the sky check. 0 failures
    on every station, worst relative difference 0.1 % (the half-float
    store), no texel skipped.
  - **Approximation** against the full-range ground truth (400 interior
    cascade 0 texels per station, 8 x 8 sub-directions per texel, relative
    luminance error with a floor of 10 % of the sample's mean truth,
    `per_neighbour_trace`): median 0.00, p90 0.00 - 0.58; the mean
    bias is within 5 % on `cornell`, `courtyard` and `leak_pair`, +131 % on
    `emissive_only` (a centre ray hitting a small panel stands for its
    whole footprint) and -17 % on `corridor` (the far field beyond the
    cascades whose probes lie outside the corridor, "Merge").
    `leak_pair` room B: every sampled texel merged 0. The script fails
    only on median > 0.25 or p90 > 1.5: a broken merge (wrong child texels
    or upper probes) reads as errors of order 1 on most texels.
  - **Mask decomposition** (`cornell`): the mean merged cascade 0
    luminance with all bands equals the sum of the single-band means
    (worst channel 0.001 %), all bands masked 0.
- Reduce, same script and readback ("Reduce"; every supported station,
  every merge mode): for 3 free interior cascade 0 probes per station, the
  irradiance and distance field texels containing the normals +y, +x and
  -z, and the probe state, against the reduce recomputed on the CPU from
  the read-back merged cascade 0 child texels and distance statistics of
  the same copy (the footprint integration of the weights over the full
  lobes, backface texels skipped, DDGI's classification threshold): 0
  failures, worst relative difference 0.09 % (the half-float store;
  tolerance 0.2 %) - the sparse weight lists leave out nothing measurable.
- Cost: every pass is timed ("Trace"). The reduce is the largest pass at
  every setting (about 60 % of an update), then the trace, the connecting
  visibility segments and the merge; the sparse reduce weights cut the
  reduce by 20 - 30 %. At the defaults an update (a full refresh: the
  default budget covers every station's layout) costs 1.15 - 1.57 x DDGI's
  update at its pinned station settings; the sweep is in
  [../plans/radiance_cascades.md](../plans/radiance_cascades.md) section
  10, "Defaults".
- Vulkan validation: a `dynamic` `gi_verify.py` comparison with both
  producers at `bounces` multi and direction jitter on (the change
  messages, history resets, field feedback), a `leak_pair` comparison and
  a `cornell` `rc_texel_verify.py` run through all three merge modes and
  the mask check report no validation error and no warning naming the
  indirect diffuse resources.
- Probe overlay: on `cornell` and `probe_offset_sweep`, 60 updates with
  the overlay off record no copy; `state` on cascade 0 classifies 125 / 125
  `cornell` probes active and 47 of 600 `probe_offset_sweep` probes inside, cascade 1 draws
  grey outside `visibility_masked` and classifies there (`probe_offset_sweep`
  2 of 96 inside); `state_and_irradiance` shows red lines at the red wall,
  green at the green wall and white over the lit floor of `cornell`
  (`capture_screenshot`); `render_scene_image` of the same view shows no
  overlay; Vulkan validation reports no error through all modes.
- `editor_renderer_tests`: cascade fit (cascade 0 counts and spacing, nesting
  of the upper grids, cascade count and `max_cascades`, probe budget, atlas
  size limit, tile placement, the neighbour atlas block and cascade 0's
  merged block within the size limit and without effect at the default
  one), interval bounds, octahedral round trip and 2x2 nesting, and the
  upper-probe indices and weights (for an interior probe the weighted upper
  positions reproduce the lower probe position).
- `py -3 scripts/gi_verify.py --station all --source radiance_cascades
  [--rc-merge-mode ...] [--rc-set KEY=VALUE ...] [--bounces single|multi]`
  pins `RC_SETTINGS` (the config defaults) and the field sampling settings
  (`DDGI_SETTINGS`) of the creation module, with overrides for a sweep
  point, selects the source, prints each station's cascade layout, stores
  it under `radiance_cascades` in the JSON record, and measures the field
  like DDGI's; both producers run single bounce unless `--bounces multi`
  (the reference is single bounce). The comparison against DDGI, its gates
  and the defaults sweep are in
  [../plans/radiance_cascades.md](../plans/radiance_cascades.md) section
  10.
