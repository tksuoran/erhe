# Radiance cascades

Stability: experimental

World-space radiance cascades are the second producer of the runtime indirect
diffuse probe field, next to DDGI ([ddgi.md](ddgi.md)). The design and the
remaining phases are in
[../plans/radiance_cascades.md](../plans/radiance_cascades.md); this document
describes what exists: the source selection, the cascade layout and its
atlases, the developer window and the MCP tools. The trace, merge and reduce
passes do not exist yet, so the renderer produces no probe field: while
radiance cascades are the selected source, no field is bound and the forward
pass shades non-lightmapped draws with the flat scene ambient term.

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
**merged** (raw merged with everything beyond it). Both are cleared to zero at
allocation, recorded into the frame command buffer, and left in
`shader_read_only_optimal`. Texture memory per cascade is
`2 x atlas width x atlas height x 8` bytes.

## Radiance Cascades window

The developer window `Radiance_cascades_window`
(`src/editor/developer/radiance_cascades_window.{hpp,cpp}`, ini label
`radiance_cascades`) shows the source combo and, while radiance cascades are
selected, a table with one row per cascade - probe counts, probe count,
spacing, tile size, interval, texels, atlas size and memory - plus totals,
the cascade 0 origin and `r0`.

## MCP

- `set_indirect_diffuse {source, show_window}` selects the producer
  (`"ambient"`, `"ddgi"`, `"radiance_cascades"`) and returns the source and
  whether each producer is supported. `set_ddgi {enabled}` remains a
  shorthand: true selects DDGI, false returns a DDGI selection to ambient.
- `set_radiance_cascades {probe_spacing_m, volume_padding_m,
  max_probes_cascade0, max_cascades, cascade0_tile_texels, interval_scale,
  show_window}` writes the settings (explicit arguments, omitted ones
  unchanged) and returns the layout of the last fit plus the stored
  `config`; the renderer refits on its next tick.
- `get_indirect_diffuse_stats` reports `source` (the selected value) and a
  `radiance_cascades` object: `supported`, `active`, `has_field` (false until
  the reduce pass exists), `cascade_count`, `r0`, the total `probe_count`,
  `texels` and `texture_bytes`, `fit_count`, and `cascades`, one entry per
  cascade with `grid_origin`, `grid_spacing`, `grid_counts`, `probe_count`,
  `tile_texels`, `interval` `[start, end]` in metres, `texels`, `atlas_size`
  and `texture_bytes`. It carries the cost fields of the `ddgi` object
  (`update_count`, `gpu_ms_total`, `ms_per_million_rays`, ...), all 0 until
  the trace pass exists.
- `sample_indirect_diffuse` answers `source` `"ambient"` while radiance
  cascades are selected: no field is bound.

## Verification

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
