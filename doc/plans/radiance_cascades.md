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
clamped to `max_probes_cascade0` by growing `s0`. Cascade `i` doubles the
spacing of cascade `i - 1` and is centred on it; per axis its probes sit at
the centres of the lower pairs (even lower count, merge weights 0.25 /
0.75) or on the even lower probes (odd count, weights 1 / 0 and 0.5 / 0.5).
Cascade count: until cascade
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
  `direction_jitter`, `bounces`, `merge_mode`, `debug_cascade_mask`,
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
   cascade 0 first). Direction = octahedral decode of the texel centre, or
   of a per-update random point of the texel footprint when
   `direction_jitter` is `footprint`. Ray query over `[t_i, t_{i+1}]` from
   the probe centre. Hit: shade with `erhe_ray_hit.glsl` (lights, traced
   shadow rays, emission, with `bounces` multi the previous field), beta 0.
   Backface hit: radiance 0, beta 0, and for cascade 0 the backface
   statistics for classification. Miss: radiance 0, beta 1. Blend into raw
   with the texel's history hysteresis (section 6).
2. **`rc_merge.comp`** - one dispatch per cascade, `N - 1` down to 0. Top
   cascade: `merged = raw.rgb + raw.a * sky(dir)` with the scene ambient as sky
   (the atmosphere-LUT sky is the shared DDGI follow-up). Lower cascades: 2x2
   child average from each of the 8 upper probes, trilinear weights,
   `merged = raw.rgb + raw.a * upper`, with the upper weights of the
   selected `merge_mode` (below). Cascade 0 is merged at cascade 1's
   angular resolution (2 x 2 texels per texel, each with one child's upper
   value), the reduce's input.
3. **`rc_reduce.comp`** - one workgroup per cascade 0 probe, writing the three
   DDGI atlases of section 2, borders included (the `ddgi_blend.comp` border
   copy is reused); the irradiance convolution runs over the child
   resolution texels, with sparse per-output-texel lobe weights.

The merge and reduce passes run whenever a trace dispatch ran; they touch
about `2 * M0` texels, far below the trace cost.

Merge-quality option `merge_mode`, three modes; the default,
`per_neighbour_trace`, is the mode that passed the most section 10 gates
(section 10, "Merge mode default"):

- `interpolate` - trilinear weights over the 8 upper
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
- `per_neighbour_trace` (default) - every cascade below the top one
  traces, per texel, a visibility segment from the END of its interval,
  `p + t_{i+1} d`, to the interval start `u_n + t_{i+1} d` of each upper
  probe `u_n` of nonzero weight, and the merge weights each upper probe by
  that visibility and renormalizes; the texel's own interval stays the ray
  along `d`. The segments run in their own budgeted, hysteresis-blended
  pass next to the trace and are stored in a 2 x 1 neighbour atlas per
  cascade (one channel per segment;
  [../editor/radiance_cascades.md](../editor/radiance_cascades.md)
  "Trace", "Merge"). Removes the start-point parallax leak; costs up to 8
  more visibility rays (no shading) per texel below the top cascade. Up to
  phase 5 the segment ran from the interval START to the upper interval
  start and replaced the texel's own interval (the community "bilinear
  fix"); phase 6 measured that construction bending the texel's direction
  (section 10, "Phase 6").

## 6. Multi-bounce and change response

Built in phase 6, shared by both producers
([../editor/ddgi.md](../editor/ddgi.md) "History reset", "Bounces"):

- `bounces` (`Indirect_diffuse_bounces`, in `Ddgi_config` and
  `Radiance_cascades_config`): `multi` makes `shade_surface()` take a hit's
  ambient term from the producer's previous field
  (`ddgi_sample_irradiance()`, intensity 1), so the field converges to
  infinite bounces over updates; `single` (default) keeps the flat ambient.
  The reference irradiance stays single bounce, so accuracy is measured
  with `single`.
- The trace is a continuous integrator like DDGI's, not per-frame syncing of
  derived state. `Temporal_history` resets the history of both producers
  on the committed changes announced on `App_message_bus` - geometry
  edits, removals, and `Scene_lighting_changed_message` (committed node
  transforms, content or lights added or removed, light and material edits;
  a live drag keeps blending and resets once at its commit) - and on
  every allocation: the k-th trace of an item after a reset is blended with
  `min(hysteresis, k / (k + 1))`.

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
  records the numbers. The RC budget, the user's decision after the phase 6
  sweep (section 10, "Defaults"): at the RC defaults, RC's per-update GPU
  time and full-refresh time are at most about 1.5 x DDGI's on the same
  station, spent on accuracy - the settings within DDGI's own cost leave
  the leak and accuracy gates far off. Absolute numbers depend on the
  machine and go to `memory-bank/local/`, not into this document.

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
   RC.
5. **`per_neighbour_trace` merge mode** - built, described in
   [../editor/radiance_cascades.md](../editor/radiance_cascades.md)
   "Trace" and "Merge": the connecting segment pass
   (`rc_trace.comp` neighbour variant, the 4 x 2 neighbour atlases, the
   `RC neighbour trace` timer, `neighbour_rays_per_update`), the third
   `rc_merge.comp` variant, the layout's atlas block, the per-neighbour
   exact check of `scripts/rc_texel_verify.py`, and the merge-mode default
   `per_neighbour_trace`, chosen from the three-mode `gi_verify.py`
   comparison (section 10, "Radiance cascades (phase 5)"). Gates 2, 6, 9
   and 12 still fail; the causes measured so far are listed there.
6. **Temporal, multi-bounce, defaults** - built, described in
   [../editor/radiance_cascades.md](../editor/radiance_cascades.md) and
   [../editor/ddgi.md](../editor/ddgi.md) "History reset", "Bounces": the
   measured accuracy causes and their fixes (the `per_neighbour_trace`
   visibility segments, cascade 0 merged at child resolution for the
   reduce, the distance statistics), `direction_jitter`, `bounces` for both
   producers, the change-driven history reset for both producers, the
   sparse reduce weights, `gi_verify.py --rc-set / --ddgi-set / --bounces`,
   and the RC defaults chosen against the section 8 budget (section 10,
   "Radiance cascades (phase 6)").
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
9. **Cost** - the section 8 budget: RC's update and full refresh at most
   about 1.5 x DDGI's on every station.
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

### Radiance cascades (phase 5)

RC at the pinned `RC_SETTINGS` of the creation module (the
`Radiance_cascades_config` defaults: `s0` 0.5 m, `q0` 4, `interval_scale` 1,
65536 texels per frame, hysteresis 0.9, no direction jitter) with the field
sampling settings of `DDGI_SETTINGS`, all three merge modes, worst of three
runs each, measured 2026-09-28 back to back with DDGI on the headless
Vulkan editor (`gi_verify.py --compare ddgi,radiance_cascades --station all
--runs 3 --rc-merge-mode ...`, one comparison per mode). The DDGI columns of
the three comparisons match the phase 0 baseline within its run-to-run
noise; where a gate compares with DDGI, the DDGI value is the worst of all
nine DDGI runs (see "Merge mode default").

| Metric | `interpolate` | `visibility_masked` | `per_neighbour_trace` | DDGI | Gate |
|---|---|---|---|---|---|
| `leak_pair` leak | 0.027 | 0.011 | 0.0000 | 0.0008 | <= 0.01: fail / fail / pass |
| `probe_offset_sweep` leak at offset 0.0 / 0.25 / 0.5 | 0.046 / 0.029 / 0.048 | 0.041 / 0.0095 / 0.024 | 0.0003 / 0.0000 / 0.017 | 0.0047 / 0.0020 / 0.0006 | worst <= 0.02 and <= DDGI's worst: fail / fail / fail (0.017 > 0.0047) |
| `probe_offset_sweep` placement, min / median: pillar faces, pillar base, crawl floor | 0.36, 0.61, 0.31 | 0.34, 0.55, undefined (median 0) | 0.28, 0.51, 0.064 | 0.18 - 0.24, 0.26 - 0.59, 0.43 - 0.49 | recorded |
| `probe_offset_sweep` crawl-floor median irradiance | 0.0047 | 0 | 0.00014 | 0.0004 | item 12 |
| `cornell` red strip R / G, green strip G / R | 1.97, 1.97 | 2.05, 2.05 | 1.86, 1.86 | 1.94 | >= 1.2: pass / pass / pass |
| `emissive_only` panel floor / room median, 1.0 / 0.25 / 0.05 m | 28.8 / 1.06 / 0.029 | 158 / 5.8 / 0.28 | 34.4 / 5.1 / 0.25 | 6.8 - 7.0 / 4.7 - 4.8 / 0.49 - 0.51 | 1.0 and 0.25 m > 1: pass / pass / pass |
| `corridor` monotonic violations; max log second difference | 7; 1.96 | 0; undefined (0 beyond 8 m) | 0; undefined (0 from 13.8 m) | 0; 0.23 | 0 and <= DDGI: fail / fail / fail |
| `courtyard` shadowed wall mean irradiance | 0.145 | 0.148 | 0.174 | 0.173 | - |
| `dynamic` updates to settle, light move / door open | 43 / 47 | 39 / 47 | 40 / 48 | 90 / 258 | <= half: pass / pass / pass |
| `cornell` back wall noise, per-point std / mean | 0 | 0 | 0 | 0.0038 | below DDGI: pass / pass / pass |
| Cost (item 9): per update; full refresh, vs DDGI | 1.6 - 2.8 x; 1.6 - 9.7 x | 1.6 - 2.7 x; 1.6 - 9.5 x | 2.8 - 4.5 x; 3.5 - 15.6 x | 1 | <= DDGI: fail / fail / fail |
| Vulkan validation (item 10) | 0 errors | 0 errors | 0 errors | - | pass / pass / pass |

Accuracy (item 12), `mean_rel_err` per group, RC `interpolate` /
`visibility_masked` / `per_neighbour_trace` (DDGI, worst of the nine runs,
in parentheses):

| Group | RC | (DDGI) |
|---|---|---|
| `leak_pair` room A floor / shared wall | 0.27 / 0.24; 0.40 / 0.09; 0.09 / 0.09 | (0.01 / 0.08) |
| `leak_pair` room B floor / shared wall | 1.96 / 1.41; 0.66 / 0.00; 0.00 / 0.00 | (0.02 / 0.09) |
| `probe_offset_sweep` room A floor, offset 0.0 / 0.25 / 0.5 | 0.31 / 0.29 / 0.29; 0.41 / 0.45 / 0.44; 0.09 / 0.09 / 0.10 | (0.07 / 0.03 / 0.04) |
| `probe_offset_sweep` room B floor, offset 0.0 / 0.25 / 0.5 | 3.18 / 1.96 / 3.00; 2.39 / 0.50 / 1.18; 0.03 / 0.00 / 1.40 | (0.05 / 0.03 / 0.01) |
| `probe_offset_sweep` room A wall, offset 0.0 / 0.25 / 0.5 | 0.26 / 0.29 / 0.35; 0.11 / 0.22 / 0.20; 0.11 / 0.15 / 0.14 | (0.37 / 0.05 / 0.19) |
| `probe_offset_sweep` room B wall, offset 0.0 / 0.25 / 0.5 | 0.10 / 0.09 / 2.07; 0.06 / 0.00 / 0.22; 0.00 / 0.00 / 0.02 | (0.44 / 0.16 / 0.05) |
| `probe_offset_sweep` pillar faces / pillar base / crawl floor | 0.18 / 0.16 / 1.98; 0.11 / 0.07 / 0.08; 0.12 / 0.16 / 0.02 | (0.24 / 0.21 / 0.11) |
| `cornell` red strip / green strip / floor / back wall | 0.18 / 0.18 / 0.16 / 0.23; 0.21 / 0.19 / 0.18 / 0.33; 0.15 / 0.12 / 0.15 / 0.04 | (0.10 / 0.11 / 0.08 / 0.04) |
| `emissive_only` floor at 1.0 / 0.25 / 0.05 m panel | 0.07 / 0.84 / 0.97; 0.07 / 0.83 / 0.95; 0.28 / 0.21 / 0.76 | (0.65 / 0.06 / 0.38) |
| `emissive_only` room floor | 0.28; 0.33; 0.02 | (0.42) |
| `corridor` profile | 0.18; 0.49; 0.08 | (0.23) |
| `courtyard` shadowed wall / shadowed floor / sunlit floor | 0.17 / 0.03 / 0.03; 0.15 / 0.06 / 0.01; 0.01 / 0.15 / 0.30 | (0.01 / 0.03 / 0.04) |
| `dynamic` floor / side room floor | 0.13 / 0.85; 0.15 / 0.22; 0.19 / 0.02 | (0.11 / 0.01) |
| worst group except `floor_0.05`, over all stations | 3.18; 2.39; 1.40 | (0.65) |

Gate 12 fails in every mode. `per_neighbour_trace` is at or below DDGI's
error on both `leak_pair` room B groups, the room B walls and two of the
three room A walls and room B floors of `probe_offset_sweep`, its pillar
and crawl groups,
the `cornell` back wall, the `emissive_only` 1.0 m panel and room floors,
the `corridor` profile and the `courtyard` shadowed wall, and above it
elsewhere; its worst group per station is within 0.25 on `leak_pair`,
`cornell`, `corridor` and `dynamic`, and exceeds it on
`probe_offset_sweep` (1.40), `emissive_only` (0.28) and `courtyard`
(0.30).

#### Merge mode default

Rule: the default is the mode that passes the most of the section 10 gates
2, 4 - 10 and 12 (each as worded above; items 1, 3 and 11 are recorded or
backend checks), ties broken by the worst group `mean_rel_err` of item 12
over all stations (except `floor_0.05`), then by cost. Every mode is gated
against the same DDGI numbers, the worst over the nine DDGI runs of the
three comparisons: the DDGI light-move settle count alone varies 70 - 90
between comparisons, which would decide gate 7 for `per_neighbour_trace`
(40 updates) by DDGI's noise, not by the mode.

- Gates passed: `interpolate` 4, 5, 7, 8, 10; `visibility_masked` 4, 5, 7,
  8, 10; `per_neighbour_trace` 4, 5, 7, 8, 10 - five each.
  (`per_neighbour_trace` also passes the first two parts of gate 2, the
  `leak_pair` leak and the 2 % offset bound, which the others fail.)
- Tie-break, worst group: 3.18, 2.39, 1.40.

**`per_neighbour_trace` is the default** (`Radiance_cascades_config` and
the creation module's `RC_SETTINGS` runs).

#### What the numbers say

- **Leaks**: `per_neighbour_trace` removes the start-point parallax leak:
  room B of `leak_pair` reads 0 (texel level too: `rc_texel_verify.py`,
  mean merged luminance of the sampled room B texels 0.00000, against
  0.0024 `interpolate` and 0.0014 `visibility_masked`). The remaining
  `probe_offset_sweep` leak at offset 0.5 (0.017, gate 2) is the
  pre-averaged upper value: traced back from a room B cascade 0 texel
  (probe (1.75, 1.26, 8.56), direction (-0.41, -0.41, 0.82)), its segment
  ends in room B at (1.65, 0.66, 9.27), but one of the 2x2 child
  directions of the upper probe at x 2.0 starts its interval at
  (1.446, 0.455, 8.93), 4 mm on the room A side of the 0.1 m wall (face at
  x 1.45): the child interval starts sit up to `t_{i+1}` times the child
  offset angle (here 0.44 m) from the segment end. Tracing a segment per
  child direction (32 rays per texel instead of 8) ends every segment
  exactly at its child's interval start; it is not built.
- **Dark bias**: the merged cascade 0 texels of `per_neighbour_trace` are
  within -2 % .. +1 % of the full-range truth on `cornell`, `courtyard`
  and `leak_pair` (-17 % .. -21 % for the other modes;
  [../editor/radiance_cascades.md](../editor/radiance_cascades.md)
  "Verification"), -15 % on `emissive_only` and -27 % on `corridor`.
- **Far field**: in `corridor` every mode fails gate 6. The 2 x 2
  cascade 3 probes per cross-section sit outside the 1.5 m corridor (hidden
  upper weight 1.00 for cascade 3 from every sampled cascade 0 probe,
  `rc_texel_verify.py`); `interpolate` carries the far field through them
  by start-point parallax (7 monotonic violations), `visibility_masked`
  drops them (profile 0 beyond 8 m), and `per_neighbour_trace` traces its
  cascade 2 segments into the walls toward them (profile exactly 0 from
  13.8 m on, every run). Its profile error over the corridor is the
  smallest of all (0.08, DDGI 0.23), because the near and mid field carry
  the group mean.
- **Small near emitters**: the 0.25 m panel passes in every mode
  (`interpolate` read 0.62 before the upper grids were centred). With `per_neighbour_trace` the
  cascade 0 segments leave the probe toward upper interval starts up to
  1.3 m (0.75 of an upper spacing per axis) off the texel ray while `r0`
  is 0.87 m, so a texel that does not point at a small emitter can see
  it: the worst `emissive_only` texels read 0.83 - 0.87 where the truth is
  0 (p90 of the texel error 2.6 against 0.5 - 1.0 for the other modes),
  and the 1.0 m panel floor is 28 % off (7 % in the other modes).
- **Accuracy failures not isolated in this phase**: the `courtyard`
  sunlit floor (+30 % with `per_neighbour_trace`, 3 % otherwise) and the
  `cornell` / `dynamic` floors (15 - 19 %) have no measured cause yet.
- **Convergence and noise**: 39 - 43 updates to settle after the light
  move and 47 - 48 after the door opens in every mode, no per-update noise
  without jitter: the merge mode does not change either; both are set by
  the raw and segment hysteresis (0.9) and the budget.
- **Cost**: `per_neighbour_trace` adds its connecting segments (up to 8
  rays per texel below the top cascade; 4.6 - 7.2 x the rays of the other
  modes per update at the same texel budget) as the separately timed
  neighbour trace pass, and its merge reads 8 segment texels per texel
  (about twice the merge time); per update it costs 1.5 - 2.1 x
  `interpolate`. Phase 6 chooses the defaults against the section 8
  budget.

### Radiance cascades (phase 6)

Phase 6 changed the `per_neighbour_trace` construction and the reduce, so
the phase 5 tables above measured the earlier construction. All numbers
below: headless Vulkan editor, same machine, back to back with DDGI;
costs as ratios to DDGI's at its pinned station settings (absolute numbers
are machine-specific).

#### Phase 6: accuracy causes

Traced per stage on the failing groups with a chain diagnostic: field at the
surface vs reference at the surface; the field's probes read at their own
positions vs the reference at the probe positions; the trilinear
combination of the reference at the probes vs the surface reference
(probe-to-surface height); the reduce of CPU ground-truth footprint means vs
the reference at the probe (the reduce's discretization); the read-back
merged texels vs the ground truth per texel (the merge). At `s0` 0.5,
`q0` 4, before phase 6:

- **The connecting segment bent the texel's direction** (the old
  `per_neighbour_trace`). The segment from the probe (`t_0 = 0`) to
  `u_n + r0 d` deviates from `d` by up to `atan(1.5 / interval_scale)`
  (the upper probe offset reaches 1.5 `s0 sqrt(3)`, `r0` is
  `interval_scale sqrt(3) s0`). The `courtyard` floor probes 0.22 m above
  the floor have their upper probes 2 cm below it, so their horizon texels
  traced segments into the sunlit floor: merged / truth 1.8 - 3.0 (4.6 at
  `q0` 8) on those texels, +10 % on the probes' irradiance; with
  `interval_scale` 2 the overshoot vanished (0.65 - 1.0), confirming the
  mechanism, and it is the `emissive_only` texel tail (p90 2.6: segments
  reaching an emitter the texel does not point at). Fixed at the root: the
  segment is now a visibility test from the interval end, the interval
  stays the ray along `d` (merged / truth 0.83 - 1.17 on the same texels;
  `cornell` texel median 0.17 -> 0.09, p90 1.15 -> 0.66, mean bias -4 %).
- **The reduce convolved 45-degree footprint means.** Given exact
  footprint means, the cosine reduce over `q0` 4 texels overstated the
  irradiance of the `courtyard` and `cornell` floor probes by 15 %
  (bright wall parts near a texel's horizon take the cosine weight of the
  whole footprint); the `q0` 8 oracle gives +4 %. The merge already reads
  cascade 1 at twice the angular resolution and averaged it away: cascade
  0 is now merged at child resolution and the reduce runs over those
  texels (probe error 1.02 on `cornell`, 0.96 on `courtyard` with jitter,
  at no trace cost).
- **Centre-direction sampling** of the near interval: without jitter the
  `cornell` floor probes read 0.89 of the reference, with direction jitter
  1.01 (section "direction jitter" below).
- **Distance statistics**: the reduce took one centre-ray distance per
  texel; with jitter the footprint's distance spread would widen the
  Chebyshev variance, so the moments stay along the centre direction
  (extra unshaded ray) and only the backface fraction comes from the
  jittered traces; the reduce now normalizes backface traces correctly for
  fractional backface counts.
- **Probe-to-surface height** (the field is probe irradiance interpolated
  to the surface, as for DDGI): the `cornell` floor's nearest probe layer
  sits 0.27 m above it (the layer below lies inside the floor slab), where
  the reference is 19 % lower; the `courtyard` floor 3 %. DDGI relocates
  probes out of the slab to just above the floor; RC's grid is fixed.
  This remains, together with small near emitters (the 0.25 m panel's
  floor probes read 0.5 - 0.6 of the reference at `s0` 0.5) and, at
  coarse `s0`, leaks through thin walls in the field sampling (the
  texel-level room B of `leak_pair` stays exactly 0 at every setting;
  the leak is the consumer's interpolation between unrelocated probes).

#### Phase 6: direction jitter, history reset, bounces

- **Direction jitter** (`footprint`) improves the near field (`cornell`
  floor probes 0.89 -> 1.01 of the reference, `emissive_only` room floor
  0.78 -> 1.05, `courtyard` sunlit floor 1.08 -> 1.01 at `s0` 0.5) but
  leaks: the connecting segments test one point per trace while an upper
  texel's value averages its own jittered footprint, whose interval starts
  straddle a thin wall (`leak_pair` room B 2 % of room A with jittered
  segments, 1.3 % with centre-direction segments, 0 without jitter), and
  it brings per-update noise (`cornell` back wall 0.03 - 0.07 relative
  std against DDGI's 0.004 at the defaults, gate 8 fails) and slow
  settling (80 - 230 updates at hysteresis 0.97). Default `none`.
- **History reset**: DDGI's `dynamic` settle times drop from 90 / 246
  updates (phase 0) to 13 - 17 / 25 - 45 (one noise-limited 239 on the dim
  side room floor in nine runs), RC's from 40 / 48 (phase 5) to 20 / 16 -
  19 without jitter. New grids start with their first update for both.
- **Bounces** `multi` converges on every station (the feedback is the
  physical irradiance, intensity 1) and brightens the enclosed white rooms
  strongly - DDGI / RC mean field luminance multi over single: `cornell`
  3.6 / 3.3, `courtyard` 1.5 / 1.5, `emissive_only` 4.6 / 2.3,
  `corridor` 8.7 / 7.1 - at +14 % / +5 % update cost. The ground truth
  and every accuracy gate are single bounce, so the default stays
  `single`; `gi_verify.py --bounces multi` measures the brightness.

#### Defaults

The sweep below (one run each, direction jitter `none`, hysteresis 0.9;
cost = worst station ratio to DDGI, update / full refresh; accuracy =
worst group `mean_rel_err` except `floor_0.05`, groups above DDGI's of 34;
leaks = `leak_pair`, worst `probe_offset_sweep`; rows marked * before the
sparse reduce weights, whose reduce was 20 - 30 % slower) was run with
`texels_per_frame` covering each layout, so an update is a full refresh; a
smaller budget splits the trace, but the merge and the reduce run over
everything every update, so it only lengthens the full refresh.

| `s0` m | `q0` | `interval_scale` | cost | worst group | above DDGI | leaks |
|---|---|---|---|---|---|---|
| 0.5 | 4 | 1 | 8.5 / 6.3 * | 2.25 | 13 | 0 / 0.026 |
| 0.5 | 8 | 1 | 30.5 / 22.3 * | 0.67 | 13 | 0.0002 / 0.0003 |
| 0.5 | 8 | 2 | 30.5 / 22.3 * | 0.80 | 13 | 0.003 / 0.0000 |
| 0.75 | 4 | 2 | 3.2 / 2.5 * | 3.16 | 10 | 0.0001 / 0.034 |
| 0.75 | 8 | 1 | 11.1 / 8.5 * | 0.59 | 12 | 0 / 0.002 |
| 1.0 | 4 | 1 | 1.6 / 1.4 * | 3.63 | 21 | 0.033 / 0.007 |
| 1.0 | 4 | 2 | 1.40 / 1.19 | 0.95 | 19 | 0.010 / 0.005 |
| 1.0 | 8 | 2 | 5.2 / 4.3 * | 0.60 | 17 | 0.0008 / 0.002 |
| 1.25 | 4 | 2 | 0.87 / 0.84 | 14.9 | 25 | 0.019 / 0.022 |
| 1.25 | 4 | 3 | 0.86 / 0.86 | 21.2 | 27 | 0.019 / 0.016 |
| 1.5 | 4 | 2 | 0.68 / 0.68 | 3.44 | 21 | 0.036 / 0.029 |
| 1.5 | 4 | 3 | 0.63 / 0.63 | 3.44 | 22 | 0.036 / 0.029 |
| **1.5** | **8** | **2** | **1.54 / 1.49** | **0.91** | **19** | **0.002 / 0.008** |
| 2.0 | 8 | 2 | 1.13 / 1.13 | 1.85 | 19 | 0.0000 / 0.006 |

The settings that cost at most DDGI (`s0` 1.25 m and above with `q0` 4)
leave the leak and accuracy gates far off (worst group 3.4 - 21); `q0` 8
with `interval_scale` 2 removes most leaks and brings the worst group
below 1. The user chose accuracy at a bounded cost ("Budget", section 8):
**the defaults are `probe_spacing_m` 1.5, `cascade0_tile_texels` 8,
`interval_scale` 2, `texels_per_frame` 131072 (a full refresh per update on
every test station), `hysteresis` 0.9, `direction_jitter` `none`,
`bounces` `single`**.

#### Phase 6 gates at the defaults

Worst of three runs, `per_neighbour_trace`, single bounce, measured
2026-09-28:

| Metric | RC | DDGI | Gate |
|---|---|---|---|
| `leak_pair` leak | 0.0017 | 0.0008 | <= 0.01: pass |
| `probe_offset_sweep` leak at offset 0.0 / 0.25 / 0.5 | 0.0001 / 0.0082 / 0.0010 | 0.0000 / 0.0020 / 0.0006 | worst <= 0.02 and <= DDGI's: fail (0.0082 > 0.0020) |
| `probe_offset_sweep` placement, min / median: pillar faces, pillar base, crawl floor | 0.31, 0.41, 0.27 | 0.18, 0.26, 0.51 | recorded |
| `cornell` red strip R / G, green strip G / R | 2.07, 2.07 | 1.94 | >= 1.2: pass |
| `emissive_only` panel floor / room median, 1.0 / 0.25 / 0.05 m | 1.06 / 0.53 / 0.09 | 6.9 / 4.7 / 0.46 | 1.0 and 0.25 m > 1: fail (0.25 m) |
| `corridor` monotonic violations; max log second difference | 0; undefined (profile 0 beyond the cascades inside the corridor) | 0; 0.21 | fail |
| `dynamic` updates to settle, light move / door open | 19 / 16 | 29 / 258 (per run 17 - 29 / 12 - 258) | <= half: fail / pass |
| `cornell` back wall noise | 0 | 0.0043 | pass |
| Cost (item 9): update; full refresh, vs DDGI | 1.15 - 1.57 x; 0.79 - 1.54 x | 1 | about 1.5 x: pass |
| Vulkan validation (item 10) | 0 errors | 0 errors | pass |
| Accuracy (item 12): worst group except `floor_0.05` | 0.91 (`emissive_only` floor at the 1.0 m panel) | 0.65 | fail |

Accuracy, `mean_rel_err` RC (DDGI): `leak_pair` room A floor / wall 0.03 /
0.09 (0.01 / 0.08), room B floor / wall 0.02 / 0.18 (0.02 / 0.08);
`probe_offset_sweep` room A walls 0.44 / 0.01 / 0.21 (0.37 / 0.05 /
0.19), room B walls 0.01 / 0.62 / 0.08 (0.00 / 0.15 / 0.05), room A
floors 0.09 / 0.02 / 0.07 (0.07 / 0.03 / 0.04), room B floors 0.01 /
0.04 / 0.01 (0.00 / 0.03 / 0.01), pillar faces / base / crawl floor 0.25 /
0.07 / 0.06 (0.25 / 0.20 / 0.10); `cornell` red / green strip / floor /
back wall 0.04 / 0.01 / 0.05 / 0.09 (0.10 / 0.11 / 0.08 / 0.04);
`emissive_only` floor at 1.0 / 0.25 m, room floor 0.91 / 0.82 / 0.31
(0.65 / 0.05 / 0.41); `corridor` 0.41 (0.23); `courtyard` shadowed wall /
floor / sunlit floor 0.15 / 0.03 / 0.03 (0.01 / 0.02 / 0.04); `dynamic`
red / green strip / floor / side room floor 0.02 / 0.00 / 0.05 / 0.02
(0.12 / 0.11 / 0.11 / 0.01). At the defaults RC passes gates 4, 8, 9, 10,
the `leak_pair` part of 2 and the door half of 7, and is at or below
DDGI's error on 13 of 34 groups, including the `cornell` strips and floor
and the `dynamic` strips and floor (0.00 - 0.05 against DDGI's 0.08 -
0.12). What fails: the `probe_offset_sweep`
leak at offset 0.25, the small near emitters (the panels' floor probes see
a 1.0 m or 0.25 m panel through few cascade 0 texels, gate 5 and the
worst group), the corridor far field beyond the cascades whose probes sit
outside the corridor (gate 6) and the light move settle (gate 7). The
DDGI column matches the phase 0 baseline within its run-to-run noise,
except the convergence the history reset shortened.

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
