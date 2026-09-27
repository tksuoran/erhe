# Radiance cascades

Stability: experimental

World-space radiance cascades are the second producer of the runtime indirect
diffuse probe field, next to DDGI ([ddgi.md](ddgi.md)). The design and the
remaining phases are in
[../plans/radiance_cascades.md](../plans/radiance_cascades.md); this document
describes what exists: the source selection, the cascade layout and its
atlases, the interval trace, the developer window and the MCP tools. The
merge and reduce passes do not exist yet, so the renderer produces no probe
field: while radiance cascades are the selected source, no field is bound
and the forward pass shades non-lightmapped draws with the flat scene
ambient term.

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
  probes only while selected. `Editor::tick()` publishes DDGI's field to
  `Forward_renderer::set_ddgi()` while DDGI is active and clears it
  otherwise.
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
| `probe_spacing_m` | 0.5 | cascade 0 target spacing `s0` |
| `volume_padding_m` | 1.0 | growth of the content box before the fit |
| `max_probes_cascade0` | 65536 | cascade 0 probe budget; `s0` grows until the grid fits |
| `max_cascades` | 8 | upper bound on the cascade count (at most 12) |
| `cascade0_tile_texels` | 4 | `q0`, the cascade 0 octahedral tile side |
| `interval_scale` | 1.0 | `r0 = interval_scale * sqrt(3) * s0`, at least 1 |
| `texels_per_frame` | 65536 | trace budget: raw texels (one interval ray each) traced per frame |
| `hysteresis` | 0.9 | blend weight kept from a raw texel's previous value each time it is traced |

`Radiance_cascades_config` v2 added `texels_per_frame` and `hysteresis`; a
v1 file reads them as the defaults.

The field's sampling parameters (irradiance / distance texels, biases,
intensity) are the DDGI settings, so both producers render the same way.

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
- **Cascade i** (`get_upper_grid()`): probes at the centres of 2x2x2 blocks of
  cascade `i - 1` - spacing doubled, origin moved by half a lower spacing,
  counts `ceil(lower / 2)` per axis (an odd lower count gives the last upper
  probe a block half outside the lower grid). Octahedral tile side
  `q_i = q0 * 2^i`. Interval `[r0 * (2^i - 1), r0 * (2^(i+1) - 1)]`
  (`get_radiance_interval()`), contiguous from cascade to cascade.
- **Cascade count**: cascades are added until the top cascade has at most 2
  probes on its longest axis, or `max_cascades` exist.
- **Atlas tiling**: each cascade's probe tiles (`q_i x q_i`, no border) wrap
  `probe_index = x + nx * (y + ny * z)` into rows of
  `min(ceil(sqrt(probes)), max_texture_size / q_i)` tiles
  (`Radiance_cascade::get_tile_origin()`), so the atlas stays close to square
  and both sides stay within `Device_info::max_texture_size`. When a cascade
  would still exceed it, the cascade 0 budget is lowered (growing `s0`) and
  the fit repeated.
- **Merge math**: `get_child_texels()` - cascade `i` texel `(u, v)` covers
  cascade `i + 1` texels `(2u .. 2u+1, 2v .. 2v+1)`, the octahedral nesting of
  the merge; `get_upper_probes()` - the 8 cascade `i + 1` probes of a cascade
  `i` probe with their trilinear weights, products of 0.25 / 0.75 per axis
  (lower probe `k` sits at upper grid coordinate `k / 2 - 1 / 4`), indices
  clamped at the upper grid's edges. `octahedral_encode()` /
  `octahedral_decode()` follow `res/shaders/erhe_ddgi.glsl`.

Because the upper counts round up, small upper cascades do not halve the
texel count exactly: the total is above the paper's `2 * M0` bound for a
small volume (the window and the MCP stats report the real numbers).

## Textures

`Radiance_cascades_renderer` (`src/editor/renderers/radiance_cascades_renderer.{hpp,cpp}`)
is constructed in `editor.cpp`'s `post_processing_task` next to
`Ddgi_renderer`, owned by `Editor`, and published as
`App_context::radiance_cascades_renderer`. Per cascade it allocates two
atlases of the cascade's atlas size, RGBA16F, storage + sampled + transfer:
**raw** (the traced intervals, rgb radiance and a transparency beta) and
**merged** (raw merged with everything beyond it). Cascade 0 also has a
**distance** texture of its atlas size, R32F: the signed hit distance of
each raw texel's last trace (see "Trace"), which the reduce pass turns into
the distance moments and the probe classification. All are cleared to zero
at allocation, recorded into the frame command buffer, and left in
`shader_read_only_optimal`. Texture memory per cascade is
`2 x atlas width x atlas height x 8` bytes, plus
`atlas width x atlas height x 4` bytes for cascade 0's distance texture.

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
- **Transport** is DDGI's (`ddgi_trace_ray_segment()` in
  `res/editor/shaders/erhe_ddgi_ray.glsl`, whose `[0, t_max]` form is the
  DDGI probe and reference ray): a front face hit carries `shade_surface()`
  (direct lights with traced shadow rays, scene ambient x base colour,
  emission), beta 0; a backface hit carries radiance 0, beta 0; a miss
  carries radiance 0, beta 1 - the merge pass adds what lies beyond the
  interval.
- **Hysteresis.** The result is blended into the raw atlas as
  `mix(traced, history, hysteresis)`. **First fill:** until the cursor has
  wrapped once after an allocation (`completed_sweeps` 0) every traced texel
  is new and its history is the allocation clear, so those runs are written
  unblended. Without direction jitter a static scene is exact after the
  first sweep; the hysteresis matters once jitter or scene changes arrive
  (plan phase 5).
- **Cascade 0 distance.** Cascade 0 runs also store the signed hit distance
  in the distance texture, same texel address as the raw atlas and not
  blended: `+t` for a front face hit, `-t` for a backface hit, `r0` (the
  interval end) for a miss.
- **Trace inputs** are built as for DDGI: the scene root's forward
  `Material_set`, a `Light_buffer` with projections fitted by
  `fit_trace_light_projections()` (`src/editor/renderers/trace_lights.{hpp,cpp}`,
  shared with `Ddgi_renderer`; no trace without a scene camera), and the
  renderer's own `Scene_tlas`, rebuilt each frame it traces. Binding points
  follow DDGI's trace layout: material 0, light 1, control 2, instance
  records 3, TLAS 4, raw atlas 5 (`rgba16f`), distance 6 (`r32f`); the
  texture heap is set 1.
- **Timing.** One explicit-range `Gpu_timer` (`Scoped_gpu_timer`, plot name
  `RC trace`) brackets all of a frame's trace dispatches.
  `Radiance_cascades_renderer::get_stats()` derives, like DDGI's:
  `trace` last / mean over the last 60 samples, `texels_per_update` and
  `rays_per_update` (one ray per texel), `ms_per_million_rays`,
  `updates_per_full_refresh` = ceil(total texels / texels per update) and
  `full_refresh_ms`, `update_count`, `timing_sample_count` and
  `completed_sweeps`. The history is cleared when another source is
  selected.

## Radiance Cascades window

The developer window `Radiance_cascades_window`
(`src/editor/developer/radiance_cascades_window.{hpp,cpp}`, ini label
`radiance_cascades`) shows the source combo and, while radiance cascades are
selected, a table with one row per cascade - probe counts, probe count,
spacing, tile size, interval, texels, atlas size and memory - plus totals,
the cascade 0 origin and `r0`; the trace GPU time and cost figures; and an
atlas preview of a chosen cascade and channel (radiance with a scale, beta,
or cascade 0 signed distance - green front face, red backface, brightness
distance / `r0`).

The raw atlases carry beta in alpha, which the ImGui image widget would use
as opacity (every hit texel would vanish), so the preview is an opaque copy
written by `res/editor/shaders/rc_preview.comp` into one preview texture
(`Radiance_cascades_renderer::request_preview()`). The window requests it
each frame it shows the preview and the next tick records it, so the copy
costs nothing while the window is closed.

## MCP

- `set_indirect_diffuse {source, show_window}` selects the producer
  (`"ambient"`, `"ddgi"`, `"radiance_cascades"`) and returns the source and
  whether each producer is supported. `set_ddgi {enabled}` remains a
  shorthand: true selects DDGI, false returns a DDGI selection to ambient.
- `set_radiance_cascades {probe_spacing_m, volume_padding_m,
  max_probes_cascade0, max_cascades, cascade0_tile_texels, interval_scale,
  texels_per_frame, hysteresis, show_window}` writes the settings (explicit arguments, omitted ones
  unchanged) and returns the layout of the last fit plus the stored
  `config`; the renderer refits on its next tick.
- `get_indirect_diffuse_stats` reports `source` (the selected value) and a
  `radiance_cascades` object: `supported`, `active`, `has_field` (false until
  the reduce pass exists), `cascade_count`, `r0`, the total `probe_count`,
  `texels` and `texture_bytes`, `fit_count`, and `cascades`, one entry per
  cascade with `grid_origin`, `grid_spacing`, `grid_counts`, `probe_count`,
  `tile_texels`, `interval` `[start, end]` in metres, `texels`, `atlas_size`
  and `texture_bytes`, and the trace cost ("Trace"): `update_count`,
  `timing_sample_count`, `completed_sweeps`, `texels_per_update`,
  `rays_per_update`, `gpu_ms` `{trace}` and `gpu_ms_total` (`last_ms`,
  `average_ms`), `timing_history_size`, `ms_per_million_rays`,
  `updates_per_full_refresh` and `full_refresh_ms` - the fields
  `scripts/gi_verify.py` reads for both sources.
- `get_radiance_cascades_texels {texels}` reads raw texels back, on request
  only: it asks `Radiance_cascades_renderer::request_texel_readback()` for a
  copy of every raw atlas and the distance texture after the next trace
  (one host-visible buffer, allocated on the first request and grown to the
  largest layout asked for) and answers once that frame retired
  (`poll_texel_readback()`; a few frames, so not inside `batch`). Needs the
  source active. Result: `update_count` and `completed_sweeps` at copy time;
  `cascades`, per cascade `{index, texel_count, mean_radiance,
  beta_one_fraction}` (beta > 0.5: the interval is empty along that
  direction), cascade 0 also `backface_fraction` and `backface_probe_count`
  (probes with at least one backface texel - probes inside geometry); and
  `texels`, index-aligned with the optional input `[{cascade, probe:[x,y,z],
  texel:[u,v]}]` (at most 4096): `{cascade, probe, texel, probe_position,
  direction, interval, radiance, beta}`, cascade 0 also `signed_distance`.
- `sample_indirect_diffuse` answers `source` `"ambient"` while radiance
  cascades are selected: no field is bound.

## Verification

- Trace against an analytic ground truth,
  `py -3 scripts/rc_texel_verify.py [--station ...] [--reuse]` (self-launching
  headless editor with config backup / restore like `gi_verify.py`; exit
  1 on any failure; tolerances and the skip rule in its docstring): every
  raw texel of every cascade of `cornell`, `emissive_only` and
  `courtyard` (read with `get_radiance_cascades_texels` after two sweeps)
  matches a CPU ray /
  axis-aligned box intersection of the station's parts over the texel's
  interval, shaded with the `shade_surface()` formula (GGX + Lambert, spot /
  directional light with range, cone and shadow tests, ambient, emission):
  hit / miss / backface agree for every texel, cascade 0 signed distance
  within 1 mm, front face radiance within 0.1 % (half-float storage; the
  script's tolerance is 0.5 %), the emitter panels exactly 4.0. Texels whose
  reference hit lies within 0.1 mm of an interval end, or ties between
  coincident faces of touching parts (the floor top under a wall bottom,
  where either face is a valid hit), are skipped and counted - about 0.1 %
  of the texels.
- Per station summary: cascade 0 beta = 1 for 82 - 88 % of the texels
  (most short intervals are empty), backface texels only where probes sit
  inside parts (`leak_pair` 96, `probe_offset_sweep` 872 and `dynamic` 36
  cascade 0 probes; 0 in `cornell`, `emissive_only`, `corridor` and
  `courtyard`), every upward cascade 0 ray inside the `courtyard` walls
  empty. The top cascade reads beta = 1 for 97 - 100 % of its texels: in
  these rooms its interval starts beyond the far wall.

- `editor_renderer_tests`: cascade fit (cascade 0 counts and spacing, nesting
  of the upper grids, cascade count and `max_cascades`, probe budget, atlas
  size limit, tile placement), interval bounds, octahedral round trip and
  2x2 nesting, and the upper-probe indices and weights (for an interior probe
  the weighted upper positions reproduce the lower probe position).
- `py -3 scripts/gi_verify.py --station all --source radiance_cascades --runs 1`
  selects the source with `set_indirect_diffuse`, prints each station's
  cascade layout and stores it under `radiance_cascades` in the JSON record.
  Until the field exists the station metrics are measured as the flat ambient
  term (`"sampled": "ambient"` in the record).
