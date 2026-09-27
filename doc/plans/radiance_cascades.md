# Radiance cascades

Status: in progress

World-space radiance cascades as a second producer of the runtime indirect
diffuse probe field, next to DDGI ([../editor/ddgi.md](../editor/ddgi.md)).
Source: Alexander Sannikov, "Radiance Cascades: A Novel Approach to Calculating
Global Illumination" (github.com/Raikiri/RadianceCascadesPaper, MIT). Section
numbers below ("paper 2.3.1") cite that paper.

## 1. The technique in erhe terms

A **radiance interval** `L_{a,b}(p, w)` is the radiance arriving at `p` from
direction `w` from surfaces at distance `t in [a, b]` only, paired with a
**transparency** `beta_{a,b}(p, w)` (1 when the segment is empty, 0 when it
hits). Adjacent intervals merge exactly (paper 2.3.1):

```
L_{a,c} = L_{a,b} + beta_{a,b} * L_{b,c}
beta_{a,c} = beta_{a,b} * beta_{b,c}
```

A **cascade** `i` is a regular probe grid storing intervals `[t_i, t_{i+1}]`
over a set of directions. The penumbra condition (paper 2.1) says the far
field needs less spatial and more angular resolution, so from one cascade to
the next the probe spacing doubles, the angular resolution doubles per axis
and the interval length doubles (paper 2.4, "3D space"):

| | cascade 0 | cascade i |
|---|---|---|
| Probe spacing | `s0` | `s0 * 2^i` |
| Probe count | `P0` | `P0 / 8^i` |
| Octahedral tile side | `q0` | `q0 * 2^i` |
| Directions per probe | `q0^2` | `q0^2 * 4^i` |
| Interval | `[0, r0]` | `[r0 * (2^i - 1), r0 * (2^(i+1) - 1)]` |
| Texels | `M0` | `M0 / 2^i` |

All cascades together cost less than `2 * M0` texels and rays, while the
merged cascade 0 carries the angular information of the top cascade (paper 3):
every cascade 0 texel becomes a cone-averaged full-range radiance sample with
no Monte Carlo noise.

Merging runs top-down: the top cascade merges with sky radiance, then each
cascade `i` merges with cascade `i + 1` interpolated to its probe positions
(trilinear over the 8 surrounding cascade `i + 1` probes) and averaged over the
2x2 child directions of each of its texels. Octahedral tiles nest exactly: the
cascade `i` texel `(u, v)` covers the cascade `i + 1` texels
`(2u .. 2u+1, 2v .. 2v+1)`.

Diffuse lighting (paper 2.6) only needs merged cascade 0: its `q0^2` cone
directions are cosine-convolved into irradiance per probe.

### Chosen variant and why

The paper describes three storage layouts. erhe builds the **world-space 3D
cascade grid** first ("Radiance 3d", paper 4.3), because it is the one that
fits what exists:

- Ray query intervals are native: `rayQueryInitializeEXT(..., tmin, tmax)`
  traces exactly `[t_i, t_{i+1}]`. The paper's interval-extension trick
  (paper 2.3.3) exists to shorten raymarches and is not needed with hardware
  ray query.
- The scene-global volume, `Scene_tlas`, the shared hit shading in
  `res/shaders/erhe_ray_hit.glsl`, the 2D-atlas layout forced by the
  image2D-only storage images and the CPU-sized dispatches all carry over from
  DDGI unchanged.
- The result is view-independent: one field serves every viewport, the
  multi-view XR path and off-screen light, with no per-viewport cost.

The screen-space layouts (Path of Exile 2 flatland cascades, screen-space
probes with world-space intervals) are follow-ups in section 11; erhe's forward
renderer has no depth / normal prepass to place probes on today.

## 2. Output: the DDGI probe field format

The RC renderer reduces merged cascade 0 into exactly the atlases the
forward pass already samples for DDGI:

| DDGI resource | Produced from |
|---|---|
| Irradiance atlas (RGBA16F, octahedral, border texels) | cosine convolution of merged cascade 0 per probe |
| Distance atlas (RG16F, mean / mean-squared distance) | cascade 0 raw hit distances, clamped to `r0`, `pow(cos, depth_sharpness)` weighted |
| Probe data (RGBA32F) | inactive flag from the cascade 0 backface-hit ratio; offset xyz 0 |

Consequences, all by construction:

- The consumer side is shared: heap slots 5-7, the `Light_block` `ddgi_*`
  fields, `res/shaders/erhe_ddgi.glsl`, the `USE_DDGI` shader axis, the
  `DDGI Irradiance` debug mode (33) and the prewarm list. No new shader axis,
  no new heap slots, no growth of the variant space.
- DDGI and RC cannot both be active, because there is one field. The editor
  selects the producer with one enum (section 4), not two `enabled` flags.
- Chebyshev visibility keeps working against leaks. It is valid for RC when
  `r0 >= sqrt(3) * s0`: any wall between a fragment and a cascade 0 probe of its
  cell is then within cascade 0's interval and shows in the distance moments.
  The config enforces `r0 = interval_scale * sqrt(3) * s0` with
  `interval_scale >= 1`.

What RC gains over DDGI with the same consumer: noise-free irradiance (the
merged cones cover the sphere exactly instead of 128 random rays), far-field
angular resolution that grows with the cascade count, and fast convergence,
because hysteresis only smooths the trace-direction jitter instead of
integrating Monte Carlo noise.

## 3. Data layout

Grid fit as DDGI: the padded content AABB, cascade 0 spacing `s0`, probe count
clamped to `max_probes_cascade0` by growing `s0`. Cascade `i` probes sit at the
centres of 2x2x2 blocks of cascade `i - 1` probes, so the trilinear merge
weights are the constants 0.25 / 0.75 per axis. Cascade count: until cascade
`N - 1` has at most 2 probes on its longest axis, or `max_cascades`.

Per cascade, two textures (storage + sampled, RGBA16F, rgb radiance, a beta):

- **Raw** - traced intervals with temporal hysteresis; the history the trace
  blends into.
- **Merged** - raw merged with everything beyond it; rewritten by the merge
  pass, never blended.

Atlas tiling per cascade: probe tiles of `q_i x q_i` texels, no border (the
merge filters by hand). Tile placement wraps `probe_index` into rows so both
atlas sides stay below `Device_info::max_texture_size`; DDGI's
`tiles_x = nx * nz` rule overflows at cascade 0 densities.

Sizing example, a 20 x 10 x 20 m volume, `s0 = 0.5 m`, `q0 = 4`: 32000 cascade 0
probes, 512 K texels; all cascades raw + merged about 16 MB, about 1 M interval
rays for a full refresh. The Radiance cascades window reports the real numbers.

## 4. Settings and selection

- `Radiance_cascades_config` (`src/editor/config/definitions/radiance_cascades_config.py`,
  erhe_codegen, `reflect=True`): `probe_spacing_m` (s0), `max_probes_cascade0`,
  `max_cascades`, `cascade0_tile_texels` (q0, default 4), `interval_scale`
  (default 1.0), `volume_padding_m`, `hysteresis`, `texels_per_frame`,
  `direction_jitter`, `multi_bounce`, `merge_mode`, `debug_cascade_mask`,
  `debug_draw_probes`, `debug_draw_cascade`. Irradiance texels, distance texels,
  depth sharpness, normal / view bias and intensity are the field's sampling
  parameters and are read from `Ddgi_config` so both producers render the same
  way.
- `Indirect_diffuse_source` enum `{ambient, ddgi, radiance_cascades}` in the
  editor settings selects the producer and replaces `Ddgi_config::enabled`,
  with a config migration mapping `enabled = true` to `ddgi`. The Settings
  window shows it as one combo; MCP gets `set_indirect_diffuse` and
  `set_radiance_cascades` (explicit arguments, like `set_ddgi`).

## 5. Passes

`src/editor/renderers/radiance_cascades_renderer.{hpp,cpp}`, built on
`Ddgi_renderer`'s recipe: constructed next to it in `editor.cpp`'s
`post_processing_task`, published through `App_context`, ticked from
`Editor::tick()` after `flush_draw_lists()` on its own compute thread slot,
`is_supported()` = ray query available. Only the selected producer ticks; the
editor then calls `Forward_renderer::set_ddgi(...)` with that producer's
textures.

1. **`res/editor/shaders/rc_trace.comp`** - one thread per raw texel of a
   texel range (the `texels_per_frame` budget walks a cursor over all cascades,
   cascade 0 first). Direction = octahedral decode of the texel centre, jittered
   inside the texel footprint by a per-frame random offset when
   `direction_jitter` is on. Ray query over `[t_i, t_{i+1}]` from the probe
   centre. Hit: shade with `erhe_ray_hit.glsl` (lights, traced shadow rays,
   emission), beta 0. Backface hit: radiance 0, beta 0, and for cascade 0 the
   negative distance for classification. Miss: radiance 0, beta 1. Blend into
   raw with `hysteresis`.
2. **`rc_merge.comp`** - one dispatch per cascade, `N - 1` down to 0. Top
   cascade: `merged = raw.rgb + raw.a * sky(dir)` with the scene ambient as sky
   (the atmosphere-LUT sky is the shared DDGI follow-up). Lower cascades: 2x2
   child average from each of the 8 upper probes, trilinear weights,
   `merged = raw.rgb + raw.a * upper`, with the upper weights of the
   selected `merge_mode` (below).
3. **`rc_reduce.comp`** - one workgroup per cascade 0 probe, writing the three
   DDGI atlases of section 2, borders included (the `ddgi_blend.comp` border
   copy is reused).

The merge and reduce passes run whenever a trace dispatch ran; they touch
about `2 * M0` texels, far below the trace cost.

Merge-quality option `merge_mode`, three modes; the default is chosen after
phase 4 from the surface-level `gi_verify.py` accuracy:

- `interpolate` (default for now) - trilinear weights over the 8 upper
  probes. Upper intervals start at the upper probe, not at the lower probe
  (start-point parallax), so parallax across one upper spacing can show as
  ringing near high-contrast emitters, and an upper interval can start on
  the other side of a wall (the `leak_pair` leak measured in
  doc/editor/radiance_cascades.md "Verification").
- `visibility_masked` - a visibility pass (`rc_visibility.comp`, run on
  layout refit and scene geometry change, not per frame) records per probe
  whether it is inside geometry and which of its 8 upper probes it can
  see, and the merge renormalizes the trilinear weights over the usable
  upper probes (`upper = (0, 0)` when none is). Reduces the leak, but
  darkens closed rooms: coarse probes outside a room carry part of its far
  field through intervals that start back inside it.
- `per_neighbour_trace` - the community "bilinear fix": cascade `i` traces
  eight intervals per texel, one from its probe to the interval start of each
  upper neighbour, and merges each with that neighbour before the trilinear
  weight. Costs 8x cascade `i` tracing; removes the ringing.

## 6. Multi-bounce and change response

- `multi_bounce`: the trace samples the previous frame's field at the hit
  point (`ddgi_sample_irradiance`) and adds albedo times irradiance; the field
  converges to infinite bounces over frames. The same code serves the DDGI
  infinite-bounce follow-up in [ddgi.md](ddgi.md).
- The trace is a continuous integrator like DDGI's, not per-frame syncing of
  derived state. Hysteresis reset on edits is driven by the existing change
  messages on `App_message_bus` (light, transform, material, content), shared
  with the DDGI "change-driven hysteresis reset" item.

## 7. Test scene: `creation_24_gi_test_rooms`

Every GI measurement runs on a dedicated scene built by the creation tooling
(`scripts/creations/creation_24_gi_test_rooms.py`, on `common.py`; see the
`erhe-creations` skill), so the measurements need no external asset. The
scene is a test asset in the `creation_23_joint_constraint_test` pattern: the
module exports a `STATIONS` table that `scripts/gi_verify.py` imports, and a
`build_station(creation, name)` function.

Each station is built as **its own scene**, because the probe volume is fitted
to the content of the single scene root; one station per scene keeps each
volume small, keeps the cascade 0 spacing fine, and makes each station's probe
grid predictable. Every station entry carries: the builder, one or more
measurement views (camera eye / target) and, per view, named screen-space
rectangles with the statistic to take over them. All rooms are closed boxes
(floor, four walls, ceiling) built from thick parts, plain white (albedo 0.8)
unless stated, so light can reach a surface outside the direct light only
through the indirect term under test. Scene ambient is black in every station
except `courtyard`, so the flat ambient term cannot mask a result.

| Station | Content | Measures |
|---|---|---|
| `leak_pair` | Two 4 x 3 x 4 m rooms sharing one 0.1 m wall; point light in room A only. | Room B mean luminance relative to room A: light leaking through a wall thinner than the probe spacing. |
| `probe_offset_sweep` | Three copies of `leak_pair` side by side. After the build the script reads the fitted grid (origin, spacing) over MCP and moves each shared wall so its centre plane sits 0.0, 0.25 and 0.5 probe spacings from the nearest probe plane. Plus a 0.6 m high crawl space under a raised floor and a 0.3 m square pillar centred on a probe. | Leak ratio per offset; dark splotches next to the pillar and in the crawl space. Exercises probe placement directly: probes inside walls, probes on wall planes, cells thinner than the spacing. |
| `cornell` | 3 x 3 x 3 m room, left wall red, right wall green, spot light aimed at the floor. | Red / green ratio of the floor strip next to each coloured wall. |
| `emissive_only` | Closed room, no analytic light, emissive panels of 1.0, 0.25 and 0.05 m side on one wall. | Floor luminance in front of each panel (small sources are where the penumbra hypothesis breaks down); temporal noise. |
| `corridor` | 1.5 x 2.5 x 24 m corridor, spot light on the end wall at one end. | Floor luminance profile along the centre line: monotonic falloff, no steps at cascade interval boundaries. Far-field transport through the upper cascades. |
| `courtyard` | Walled 8 x 8 m yard, open top, directional light, non-black ambient as sky. | Shadowed wall luminance: sky radiance through escaping rays and the top cascade merge. |
| `dynamic` | `cornell` plus scripted events: the light moves 1 m, then a 1 x 2 m door part slides open into a dark side room. | Frames until mean luminance is within 5 % of its settled value after each event. |

The same stations serve DDGI: phase 0 runs them against DDGI before any RC
code exists, and every later comparison is RC versus DDGI on the same station,
same machine, same run.

## 8. Performance measurement

The target hardware includes integrated GPUs, and no cost figure is assumed in
advance. Costs are measured, recorded and compared:

- **Compute GPU timing.** `erhe::graphics::Gpu_timer` brackets a `Render_pass`
  only, and the DDGI compute dispatches are not timed. Phase 0 adds a
  compute-scoped timer (timestamps around a range of compute dispatches in one
  command buffer; Vulkan, plus the OpenGL and Metal implementations next to the
  existing timer backends) and times the DDGI trace, blend and relocate passes.
  This is a public API change of `erhe::graphics`, so it adds a `CHANGELOG.md`
  line.
- **Reporting.** Per-pass GPU milliseconds (last frame and a 60-frame average)
  in the DDGI and Radiance cascades windows, and from an MCP stats query
  `get_indirect_diffuse_stats`: source, grid origin / spacing / counts,
  cascades, texels or rays per frame, per-pass GPU ms, texture memory.
- **Cost model.** Both renderers amortize through a per-frame budget
  (`texels_per_frame`, `probes_per_frame`), so per-frame cost is a knob. The
  stats also report cost per million rays (interval rays for RC, probe rays for
  DDGI) and the full-refresh time (every texel or probe traced once), which is
  what compares the two.
- **Budget.** Phase 0 measures DDGI at its defaults on every station and
  records the numbers. The RC budget is: at RC defaults, per-frame GPU time
  <= DDGI's per-frame GPU time on the same station, and full-refresh time
  <= DDGI's. Phase 6 chooses the RC defaults (`texels_per_frame`, `q0`, `s0`)
  to meet it. Absolute numbers depend on the machine and go to
  `memory-bank/local/`, not into this document.

## 9. Phases

Each phase is one commit (or a small series), builds the editor, `src/example`,
`src/hello_swap`, `src/hextiles`, and keeps Vulkan validation clean.

0. **Test scene and DDGI baseline.** `creation_24_gi_test_rooms.py` with all
   stations; the compute GPU timer and DDGI pass timings;
   `get_indirect_diffuse_stats`; `scripts/gi_verify.py` measuring the stations
   for the current source. Run it against DDGI and record the baseline
   (quality numbers and timings). The probe-placement defects
   `probe_offset_sweep` showed in DDGI - probes embedded in a wall never
   relocated out of it, probes on a face looked through it, and the backface
   weight was judged from the biased point - are fixed in DDGI, because RC
   shares the consumer (Chebyshev visibility, probe state); what DDGI still
   gets wrong is discretization, described in
   [../editor/ddgi.md](../editor/ddgi.md) "Accuracy".
1. **Selection and skeleton** - built, described in
   [../editor/radiance_cascades.md](../editor/radiance_cascades.md): the
   `Indirect_diffuse_source` selection with the `Ddgi_config::enabled`
   migration, `Radiance_cascades_config` (the fields phase 1 uses; the trace,
   merge and debug fields of section 4 arrive with their phases), the cascade
   fit and atlas allocation, the Radiance Cascades window, the MCP tools
   `set_indirect_diffuse` / `set_radiance_cascades` and the
   `radiance_cascades` stats object, and the `editor_renderer_tests` unit
   tests of the layout math. `scripts/gi_verify.py --source
   radiance_cascades` builds and reports the layout per station and measures
   the flat ambient term until phase 4 binds a field.
2. **Trace** - built, described in
   [../editor/radiance_cascades.md](../editor/radiance_cascades.md) "Trace":
   `rc_trace.comp` with the texel budget, hysteresis and first-fill rule, the
   cascade 0 signed distance texture, the raw atlas preview, the timed pass
   and its cost in `get_indirect_diffuse_stats`, and the
   `get_radiance_cascades_texels` readback.
3. **Merge** - built, described in
   [../editor/radiance_cascades.md](../editor/radiance_cascades.md) "Merge":
   `rc_merge.comp` with the `interpolate` and `visibility_masked` merge
   modes (`rc_visibility.comp`, probe state textures), the merged atlas
   preview,
   `debug_cascade_mask` (a masked cascade keeps its beta and contributes no
   radiance, bit 12 masks the sky), the timed pass, merged texels in
   `get_radiance_cascades_texels`, and the merged checks of
   `scripts/rc_texel_verify.py` for both modes.
4. **Reduce and render** - built, described in
   [../editor/radiance_cascades.md](../editor/radiance_cascades.md)
   "Reduce": `rc_reduce.comp` (cosine / `pow(cos, depth_sharpness)`
   footprint-integrated weights, DDGI's border copy and classification
   rule), the probe field atlases of cascade 0's grid, published through
   `set_ddgi` by the single `get_indirect_diffuse_field()` site, which the
   `sample_indirect_diffuse` query samples too; the DDGI atlas tiling
   generalized to wrapped tile rows (`tiles_per_row`, shared by both
   producers and the forward pass); the reduce exact-algebra check of
   `scripts/rc_texel_verify.py`; and the first full `gi_verify.py` run for
   RC, both merge modes (section 10, "Radiance cascades (phase 4)").
5. **`per_neighbour_trace` merge mode**, its cost reported separately.
   Directly after phase 4: it is the fix for the start-point parallax leak
   phase 3 measured, and the merge-mode default is chosen with it.
6. **Temporal, multi-bounce, defaults.** Direction jitter, `multi_bounce`,
   change-driven hysteresis reset; RC defaults chosen against the section 8
   budget.
7. **Debug.** Probe overlay for a chosen cascade (CPU-phase debug lines only,
   see the DDGI traps).

## 10. Verification

`scripts/gi_verify.py` builds each station in the headless Vulkan editor and
waits until the field stats are stable. Quality metrics are measured with the
MCP tool `sample_indirect_diffuse` at world points defined by each station:
linear float irradiance from the exact function the forward pass uses
(doc/editor/ddgi.md "Irradiance queries"), so values around 1 % are not
quantized away. Screenshots of the station views serve the disabled-regression
item and visual review. Each acceptance number is the worst
value over three runs. `--source ambient|ddgi|radiance_cascades` selects the
producer; `--compare` runs DDGI and RC back to back and prints a table.
Typical runs: `py -3 scripts/gi_verify.py --station all --source ddgi --runs 3`
and `py -3 scripts/gi_verify.py --compare ddgi,radiance_cascades`; the script
docstring lists the options (screenshot reference / compare, `--enforce`) and
the convergence rule, and the JSON result lands in `logs/gi_verify/`.

1. **Disabled regression** - source `ambient`: screenshots identical to the
   pre-change build. Source `ddgi`: identical to the pre-change DDGI output.
2. **Leak** - `leak_pair` room B mean luminance <= 1 % of room A with RC; on
   `probe_offset_sweep` the worst offset <= 2 % and not above DDGI's worst.
3. **Placement** - the `probe_offset_sweep` groups (shared-wall faces per
   offset, pillar faces, pillar base, crawl-space floor) meet item 12
   (Accuracy). Their min / median ratios are recorded only: the reference
   itself reads 0.26 on the pillar faces and 0.23 on the base, and the crawl
   space is dark in the reference (0.07 % of the lit floor).
4. **Bounce** - `cornell` floor strip next to the red wall red / green >= 1.2,
   next to the green wall green / red >= 1.2; about 1.0 with `ambient`.
5. **Small emitters** - `emissive_only` floor in front of the 1.0 m and 0.25 m
   panels brighter than the room median; the 0.05 m panel result is recorded,
   not gated (it probes the documented limit).
6. **Far field** - `corridor` profile monotonic; largest second difference of
   the logarithm of the profile (`max_log_second_difference`, no steps at
   cascade interval boundaries) at most DDGI's.
7. **Convergence** - `dynamic`: frames to settle after each event, RC at most
   half of DDGI's.
8. **Noise** - static `cornell`, per-pixel luminance standard deviation over 30
   frames on the back wall: RC below DDGI.
9. **Cost** - the section 8 budget, against the phase 0 baseline.
10. **Validation** - Vulkan validation on for one run: zero errors.
11. OpenGL and Metal builds compile and run with the source forced to
    `ambient` (no ray query there).
12. **Accuracy** - against the ground truth of the MCP tool
    `reference_indirect_diffuse` (doc/editor/ddgi.md "Reference irradiance":
    the probe ray's own light transport, integrated at each sample point, so
    only the field's discretization differs). `gi_verify.py` reports per
    sample group `ref.<group>.mean_rel_err` (group mean against reference
    group mean) and `ref.<group>.worst_point_rel_err`, both relative to
    `max(reference, 1 % of the station's brightest reference group)`. RC:
    every group's `mean_rel_err` at most DDGI's, and on every station
    `ref.worst_group_mean_rel_err <= 0.25`, except `emissive_only`
    `floor_0.05` (the documented small-source limit, recorded). The bound was
    set at the DDGI errors of the bounce-lit groups before the DDGI placement
    fixes (`cornell` 0.20, pillar faces 0.25); DDGI's worst groups now read
    0.65, 0.37 and 0.22 (baseline below).

### Baseline (phase 0)

DDGI at the pinned station settings (`DDGI_SETTINGS` in the creation module:
1.5 m spacing, 128 rays per probe, hysteresis 0.97), worst of three runs,
measured 2026-09-27 on the headless Vulkan editor after the DDGI placement
fixes (relocation out of embedded geometry, probe rays from `t_min = 0`,
backface weight from the surface point; doc/editor/ddgi.md). The gates above
are the RC gates; the DDGI column is what RC is compared against.

| Metric | DDGI | Gate |
|---|---|---|
| `leak_pair` leak (room B / room A; worst of floor and shared-wall face) | 0.0008 | <= 0.01 pass |
| `probe_offset_sweep` leak at offset 0.0 / 0.25 / 0.5 | 0.0026 / 0.0020 / 0.0006 | worst <= 0.02 pass |
| `probe_offset_sweep` placement, min / median: pillar faces, pillar base, crawl floor | 0.18, 0.25, 0.47 | recorded |
| `probe_offset_sweep` crawl-floor median irradiance | 0.0004 (room A floor: 0.26) | item 12 |
| `cornell` red strip R / G, green strip G / R | 1.94, 1.94 | >= 1.2 pass |
| `emissive_only` panel floor / room median, 1.0 / 0.25 / 0.05 m | 6.40 / 4.59 / 0.49 | 1.0 and 0.25 m > 1 pass |
| `corridor` monotonic violations; max log second difference | 0; 0.19 | 0; RC <= DDGI |
| `courtyard` shadowed wall mean irradiance | 0.17 | - |
| `dynamic` updates to settle, light move / door open | 90 / 246 | RC <= half |
| `cornell` back wall noise, per-point std / mean (mean, max) | 0.0042, 0.0088 | RC below |
| `emissive_only` 1.0 m panel floor noise (mean, max) | 0.027, 0.037 | - |

Accuracy against `reference_indirect_diffuse` (16384 rays per point, seed 1;
worst of three runs; `mean` = `mean_rel_err`, `worst` =
`worst_point_rel_err`; DDGI / ref = ratio of the group means, run 1, with the
other runs' value where they differ). Gate 12: RC `mean` <= DDGI's per group,
and every station's worst group `mean` <= 0.25 (except `floor_0.05`).

| Group | Reference mean | DDGI / ref | mean | worst |
|---|---|---|---|---|
| `leak_pair` room A floor / shared wall | 0.253 / 0.242 | 1.01 / 1.08 | 0.01 / 0.08 | 0.10 / 0.24 |
| `leak_pair` room B floor / shared wall | 0 / 0 | - | 0.02 / 0.08 | 0.10 / 0.38 |
| `probe_offset_sweep` room A shared wall, offset 0.0 / 0.25 / 0.5 | 0.193 / 0.171 / 0.150 | 1.37 (1.16) / 1.04 / 1.19 | 0.37 / 0.05 / 0.19 | 0.77 / 0.25 / 0.45 |
| `probe_offset_sweep` room B shared wall, offset 0.0 / 0.25 / 0.5 | 0 | - | 0.26 / 0.15 / 0.05 | 1.34 / 0.76 / 0.19 |
| `probe_offset_sweep` pillar faces | 0.0562 | 1.21 (1.12) | 0.21 | 0.74 |
| `probe_offset_sweep` pillar base | 0.0480 | 1.12 (0.80) | 0.20 | 1.26 |
| `probe_offset_sweep` crawl floor | 0.00017 | 2.23 | 0.09 | 0.18 |
| `cornell` red strip / green strip / floor / back wall | 0.041 / 0.058 / 0.050 / 0.069 | 0.90 / 0.90 / 0.92 / 0.96 | 0.10 / 0.11 / 0.08 / 0.04 | 0.22 / 0.18 / 0.20 / 0.15 |
| `emissive_only` floor at 1.0 / 0.25 / 0.05 m panel | 0.536 / 0.129 / 0.021 | 0.37 / 1.08 / 0.72 | 0.65 / 0.08 / 0.36 | 0.76 / 1.44 / 0.85 |
| `emissive_only` room floor | 0.0934 | 0.60 | 0.42 | 0.73 |
| `corridor` profile | 0.0653 | 0.79 | 0.22 | 1.06 |
| `courtyard` shadowed wall / shadowed floor / sunlit floor | 0.175 / 0.099 / 0.153 | 0.99 / 0.97 / 0.96 | 0.01 / 0.03 / 0.04 | 0.03 / 0.03 / 0.06 |
| `dynamic` floor / side room floor | 0.049 / 0 | 0.89 / - | 0.11 / 0.01 | 0.30 / 0.05 |

What the reference says about the phase 0 findings:

- **Crawl space**: the reference crawl floor is near-black too (mean 0.00017,
  0.07 % of the lit floor): the darkness is the scene, not a DDGI defect.
  DDGI is 2.2x brighter there, which is 0.1 % of the lit floor in absolute
  terms.
- **Pillar**: the reference's own min / median is 0.26 on the pillar faces and
  0.23 on the pillar base, so the dark sides are mostly real shading and the
  gate 3 threshold of 0.25 sits at the reference's own value. The probe on
  the pillar's axis leaves the pillar through one face and serves that side
  (doc/editor/ddgi.md "Accuracy"), so the face and base errors depend on
  which face that is (DDGI / ref 1.12 - 1.21 on the faces, 0.80 - 1.12 on
  the base over three runs).
- **Where DDGI is wrong**: all of it is discretization, separated from bias
  with a spacing sweep (doc/editor/ddgi.md "Accuracy"): the floor right in
  front of the 1.0 m emitter (near-field transport between 1.5 m probes;
  over-bright at 0.5 m spacing), the emissive room floor (0.60, 0.94 at
  0.75 m), the corridor profile (22 % dark on average, 9 % at 0.75 m), the
  bounce-lit `cornell` floor (about 8 - 10 % dark: the nearest probe layer is
  0.15 m above the floor, where the reference itself is that much lower), and
  the room A face of the shared wall when the wall sits on a probe plane
  (37 % bright when that plane's probes relocate into room B, 16 % when they
  relocate into room A). The reference standard error per point
  (`ref.<group>.ref_point_rel_se`) is at most 2.5 % on the other groups but
  8 - 20 % on the `emissive_only` floors (small sources) and up to 71 % on the
  far `corridor` points (the lit end wall is a tiny solid angle there), so
  those groups' worst-point numbers are largely reference noise; their group
  means are not.

The field updates once per frame, so updates equal frames. The corridor
profile falls off roughly exponentially from the lit end (halving about every
metre), so its smoothness is measured on the logarithm of the profile. The `ambient` source reads 0
for every indirect metric (scene ambient is black) except `courtyard` (0.099,
the flat sky term); its ratios are undefined rather than 1.0.

### Radiance cascades (phase 4)

RC at the pinned `RC_SETTINGS` of the creation module (the
`Radiance_cascades_config` defaults: `s0` 0.5 m, `q0` 4, `interval_scale` 1,
65536 texels per frame, hysteresis 0.9, no direction jitter) with the field
sampling settings of `DDGI_SETTINGS`, both merge modes, worst of three
runs, measured 2026-09-27 back to back with DDGI on the headless Vulkan
editor (`gi_verify.py --compare ddgi,radiance_cascades --runs 3
--rc-merge-mode ...`). The DDGI column of the same runs matches the phase 0
baseline within its run-to-run noise.

| Metric | RC `interpolate` | RC `visibility_masked` | Gate |
|---|---|---|---|
| `leak_pair` leak (worst of floor and shared-wall face) | 0.039 (floor; wall 0.011) | 0.017 (floor; wall 0.0000) | <= 0.01 fail / fail |
| `probe_offset_sweep` leak at offset 0.0 / 0.25 / 0.5 | 0.047 / 0.035 / 0.051 | 0.035 / 0.018 / 0.025 | worst <= 0.02 fail / fail |
| `probe_offset_sweep` placement, min / median: pillar faces, pillar base, crawl floor | 0.30, 0.49, 0.40 | 0.29, 0.47, 0 | recorded |
| `probe_offset_sweep` crawl-floor median irradiance | 0.0042 | 0.0000 | item 12 |
| `cornell` red strip R / G, green strip G / R | 1.97, 1.97 | 2.05, 2.05 | >= 1.2 pass / pass |
| `emissive_only` panel floor / room median, 1.0 / 0.25 / 0.05 m | 17.0 / 0.62 / 0.018 | 158 / 5.8 / 0.25 | 1.0 and 0.25 m > 1: fail / pass |
| `corridor` monotonic violations; max log second difference | 0; 11.9 | 1; 9.3 | 0; <= DDGI (0.25): fail / fail |
| `courtyard` shadowed wall mean irradiance | 0.15 | 0.14 | - |
| `dynamic` updates to settle, light move / door open | 44 / 51 | 41 / 52 | <= half of DDGI (85 / 236, 78 / 225): fail (0.52, 0.53) / pass |
| `cornell` back wall noise, per-point std / mean | 0 | 0 | below DDGI pass / pass |
| Vulkan validation (item 10) | 0 errors | 0 errors | pass |
| Cost (item 9): per update and full refresh vs DDGI | 1.7 - 2.6 x; 1.7 - 9 x | 1.7 - 2.6 x; 1.7 - 9 x | <= DDGI fail / fail |

Accuracy (item 12), `mean_rel_err` per group, RC `interpolate` /
`visibility_masked` (DDGI of the same runs in parentheses):

| Group | RC | (DDGI) |
|---|---|---|
| `leak_pair` room A floor / shared wall | 0.30 / 0.28; 0.44 / 0.18 | (0.01 / 0.08) |
| `leak_pair` room B floor / shared wall | 2.71 / 0.75; 0.97 / 0.00 | (0.02 / 0.09) |
| `probe_offset_sweep` room A floor, offset 0.0 / 0.25 / 0.5 | 0.31 / 0.28 / 0.29; 0.45 / 0.41 / 0.44 | (0.07 / 0.03 / 0.04) |
| `probe_offset_sweep` room B floor, offset 0.0 / 0.25 / 0.5 | 3.26 / 2.40 / 3.24; 1.93 / 1.02 / 1.24 | (0.04 / 0.03 / 0.01) |
| `probe_offset_sweep` room A wall, offset 0.0 / 0.25 / 0.5 | 0.27 / 0.29 / 0.36; 0.12 / 0.16 / 0.15 | (0.37 / 0.04 / 0.19) |
| `probe_offset_sweep` room B wall, offset 0.0 / 0.25 / 0.5 | 0.67 / 0.32 / 2.18; 0.06 / 0.00 / 0.30 | (0.25 / 0.15 / 0.05) |
| `probe_offset_sweep` pillar faces / pillar base / crawl floor | 0.24 / 0.27 / 1.87; 0.22 / 0.24 / 0.08 | (0.21 / 0.19 / 0.09) |
| `cornell` red strip / green strip / floor / back wall | 0.18 / 0.18 / 0.16 / 0.23; 0.21 / 0.19 / 0.18 / 0.33 | (0.10 / 0.11 / 0.08 / 0.04) |
| `emissive_only` floor at 1.0 / 0.25 / 0.05 m panel | 0.07 / 0.84 / 0.97; 0.07 / 0.84 / 0.96 | (0.65 / 0.06 / 0.37) |
| `emissive_only` room floor | 0.25; 0.33 | (0.42) |
| `corridor` profile | 0.03; 0.00 | (0.23) |
| `courtyard` shadowed wall / shadowed floor / sunlit floor | 0.13 / 0.03 / 0.07; 0.19 / 0.01 / 0.10 | (0.01 / 0.03 / 0.04) |
| `dynamic` floor / side room floor | 0.18 / 0.81; 0.19 / 0.36 | (0.11 / 0.01) |

Gate 12 fails in both modes: RC is below DDGI's error on the
`emissive_only` 1.0 m panel and room floors, the `corridor` profile and the
`probe_offset_sweep` room A walls, and above it everywhere else; the worst
group bound 0.25 holds only on `corridor` and `courtyard` (and `cornell` in
`interpolate`, 0.23). What the numbers say:

- **The field is dark by 16 - 30 %** in lit rooms (`cornell`, the room A
  floors, `dynamic`), `visibility_masked` darker than `interpolate`: the
  merged-cascade bias measured in phase 3 ([../editor/radiance_cascades.md](../editor/radiance_cascades.md)
  "Verification": -17 % / -21 % on `cornell`'s merged texels), carried
  through the reduce unchanged - the reduce itself is exact
  (`rc_texel_verify.py`: every checked field texel within the half-float
  store of the CPU convolution of the read-back merged texels).
- **Leaks** are the start-point parallax of the merge: room B's merged
  texels already carry about 2 % of room A's light, which `interpolate`
  shows on the room B floor at 3.9 % of room A's (darker) floor;
  `visibility_masked` halves it. Phase 5's `per_neighbour_trace` is the fix
  the plan assigns.
- **Small near emitters** depend on cascade 0's 16 texel-centre directions
  (`q0` 4, no jitter): the 0.25 m panel reads 0.62 of the room median with
  `interpolate` (its floor 84 % dark), the 0.05 m panel is missed. Direction
  jitter (phase 6) integrates the footprint instead of its centre ray.
- **Far field** in the corridor: the profile error is the smallest of any
  field (3 %, DDGI 23 %) up to about 16 m, then the last five points fall
  to about 1e-9 - the log second-difference gate reads that cliff, not
  steps between cascades.
- **Convergence and noise** are the expected RC gains: no per-update noise
  without jitter, and 2 - 5 x fewer updates to settle; the light-move event
  misses the half-of-DDGI gate by 2 - 3 updates.
- **Cost** is not yet within the section 8 budget at the RC defaults: the
  reduce is the largest pass (the numbers are machine-specific and not
  recorded here); phase 6 chooses the defaults against the budget.

## 11. Follow-ups (not in the phases)

- **Screen-space probes with world-space intervals** (paper 4.5): probes on the
  depth buffer at `2^i` pixel spacing, bilateral spatial interpolation, same
  ray-query intervals. Per-pixel resolution instead of cascade 0 spacing; needs
  a depth / normal prepass rendergraph node and costs per viewport and per XR
  view.
- **Screen-space flatland cascades** (paper 4.2, Path of Exile 2): cheapest,
  but screen-space occlusion only.
- **Surface cascades for the lightmap baker** (paper 2.5.3): cascade memory
  constant per cascade on a 2D atlas; a gather alternative for
  [lightmap/lightmap_baking.md](lightmap/lightmap_baking.md).
- **Glossy specular** (paper 2.6): cone queries across the unmerged cascades,
  sampled in `standard.frag`; needs the per-cascade atlases bound to the forward
  pass, so new heap slots and a shader axis.
- **Camera-centred clipmap volume** for large scenes, cascade 0 scrolling with
  the camera.
- **Non-ray-query fallback** for OpenGL, Metal and Quest: a GPU voxel or SDF
  scene representation raymarched per interval, where paper 2.3.3 interval
  extension pays off.
