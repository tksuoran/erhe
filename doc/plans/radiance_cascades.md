# Radiance cascades

Status: proposed

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
probes with world-space intervals) are follow-ups in section 9; erhe's forward
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

Sizing example, a 20 x 10 x 20 m scene, `s0 = 0.5 m`, `q0 = 4`: 32000 cascade 0
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
   `merged = raw.rgb + raw.a * upper`.
3. **`rc_reduce.comp`** - one workgroup per cascade 0 probe, writing the three
   DDGI atlases of section 2, borders included (the `ddgi_blend.comp` border
   copy is reused).

The merge and reduce passes run whenever a trace dispatch ran; they touch
about `2 * M0` texels, far below the trace cost.

Merge-quality option `merge_mode`:

- `interpolate` (default) - the merge above. Upper intervals start at the
  upper probe, not at the lower probe, so parallax across one upper spacing
  can show as ringing near high-contrast emitters.
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

## 7. Phases

Each phase is one commit (or a small series), builds the editor, `src/example`,
`src/hello_swap`, `src/hextiles`, and keeps Vulkan validation clean.

1. **Selection and skeleton.** `Indirect_diffuse_source` with the DDGI config
   migration; `Radiance_cascades_config`; the renderer with grid / cascade
   fit and texture allocation; a developer `Radiance_cascades_window` reporting
   per-cascade probe counts, tile size, interval, texels and memory; MCP tools.
   Also a C++ unit test of the pure math: cascade fit, interval bounds,
   octahedral 2x2 nesting, trilinear upper-probe indices and weights.
2. **Trace.** `rc_trace.comp` with the texel budget and hysteresis; the window
   previews raw atlases per cascade.
3. **Merge.** `rc_merge.comp`, `interpolate` mode; preview merged atlases;
   `debug_cascade_mask` zeroes chosen cascades' radiance (beta kept) to show
   each interval band as in the paper's figure 3.
4. **Reduce and render.** `rc_reduce.comp`; the editor binds the RC field
   through `set_ddgi` when the source is `radiance_cascades`. First visible
   result.
5. **Temporal and multi-bounce.** Direction jitter, `multi_bounce`,
   change-driven hysteresis reset.
6. **Debug.** Probe overlay for a chosen cascade (CPU-phase debug lines only,
   see the DDGI traps), GPU timings per pass in the window.
7. **`per_neighbour_trace` merge mode.**

## 8. Verification

Acceptance numbers, checked by a committed `scripts/radiance_cascades_verify.py`
on the headless Vulkan build (MCP scene building, `capture_screenshot`, pixel
statistics over fixed screen rectangles), worst value over three runs:

1. **Disabled regression** - source `ambient`: screenshot identical to the
   pre-change build. Source `ddgi`: identical to the pre-change DDGI output.
2. **Leak test** - two closed rooms sharing a 0.1 m wall, a point light in
   room A. Room B mean luminance <= 1 % of room A with RC, and not above DDGI's
   value at default settings.
3. **Bounce present** - a white room with one red wall lit by a spot light:
   the floor next to the red wall has red / green ratio >= 1.2 with RC, ~1.0
   with `ambient`.
4. **Convergence** - after moving the light, frames until room mean luminance
   is within 5 % of its settled value: RC at most half of DDGI's.
5. **Noise** - with the light static, per-pixel luminance standard deviation
   over 30 frames on a flat wall: RC below DDGI.
6. **Budget** - default settings on Sponza: RC GPU time per frame (window
   timings) <= 1.5 ms, the lightmap baker's budget.
7. **Validation** - Vulkan validation on for one run of the script: zero
   errors.
8. OpenGL and Metal builds compile and run with the source forced to
   `ambient` (no ray query there).

## 9. Follow-ups (not in the phases)

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
