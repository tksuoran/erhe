# Shadow robustness

Status: in progress

Hardens the directional, spot and point shadow paths against self-shadowing,
leaks and precision ties, and holds them there with dedicated test scenes and
automated checks. The shadow pipeline itself is described in
[`shadows.md`](../erhe/shadows.md) and
[`point_light_shadows.md`](../erhe/point_light_shadows.md); this plan owns all
receiver-side and caster-side shadow bias work, including the deltas from the
RPDB reference (D2 to D4). Fit and performance follow-ups stay in
[`shadows.md`](shadows.md).

The tooling (T1 to T8) and the test stations (section 4) exist; section 9 is
the current gate table. The remaining work is phases 6 to 8.

## 1. Evidence: the head-on tie

Reproduction: `res/editor/assets/gi_test_rooms/gi_cornell.glb`, spot light at
(0, 2.9, 0) aimed straight down, Medium preset (`pcf_4x4`, `receiver_plane`,
`depth`, requested 24 depth bits, device format D32_SFLOAT), reverse-Z,
Vulkan on an AMD iGPU.

- The floor is exactly perpendicular to the light axis. A band of it reads
  shadow visibility 0; the band follows the light pose.
- In the band the shadow map stores 0.0104947509 and the reference depth is
  0.0104947500, one float ulp nearer the far plane, so the non-strict
  reverse-Z comparison fails. RenderDoc's shader debugger evaluates the same
  pixel as lit: last-bit rounding decides the verdict.
- For this receiver every bias term of the float PCF path is zero: all three
  committed presets use `cull_back`, so the lit floor face is in the map;
  `shadow_depth_bias_constant` is 0; the rasterizer slope bias and the
  receiver-plane bias scale with the light-space depth gradient `dz_dUV`,
  which is 0 for a head-on receiver. (The hard path's UNORM snap is a nonzero
  term, but only for 16 and 24 bit maps.)
- A rasterizer constant bias of -4 removes the band. For a float depth format
  Vulkan scales that bias by `2^(e - 23)`, `e` the largest exponent of the
  primitive's depth range: it is an ulp-scaled floor, which confirms the tie.
- Point lights compare radial distance with a world-space bias of
  `max(0.05, 0.02 * distance)`; that bias is what makes them leak through thin
  walls and detach contact shadows (section 9).

The standing rule the plan is built on: wherever the stored depth and the
reference depth come from the same surface, the comparison is a tie, and a tie
is resolved by a bias that is nonzero for every receiver orientation and is
derived from the error sources, not by a tuned constant.

## 2. Requirements

Each requirement holds for every supported configuration in the test matrix
(section 5) and is checked by a gate in section 6. The scope is opaque, rigid
casters; alpha-tested casters (the depth-only caster variant has no alpha
discard), skinned casters and spot casters inside the fixed 0.04 m spot near
plane are outside it.

- **R1 No self-shadowing.** A receiver point whose segment to the light is
  unobstructed has visibility 1, for every receiver orientation from head-on
  (`N . L = 1`) to grazing (`N . L = 0.05`), and for every light pose.
- **R2 Occlusion.** A receiver point whose segment to the light passes through
  a caster, farther than the filter footprint from the caster's shadow
  silhouette, has visibility 0.
- **R3 Contact.** A caster resting on a receiver shadows the receiver up to a
  bounded distance from the contact line (peter-panning bound, G3).
- **R4 No leaks.** A closed wall of at least the minimum thickness (G4) between
  light and receiver blocks the light completely, including at the wall-floor
  and wall-wall joins.
- **R5 Edge placement.** A hard shadow edge lies within a bounded distance of
  the analytic edge (G5).
- **R6 Pose independence.** R1 to R5 hold at every pose of the light pose
  sweep; no verdict depends on where last-bit rounding falls.
- **R7 Origin independence.** R1 to R5 hold with the whole station translated
  1 km and 10 km from the origin. The fp32 world position and matrix
  composition carry about 0.6 mm of error at 10 km against 2 to 4 mm shadow
  texels; the origin term of D1 is what absorbs it.
- **R8 Temporal stability.** For a static scene, a sub-texel camera
  translation changes directional shadow visibility only inside the edge band.
  Spot and point maps do not depend on the camera, so for them G6 detects only
  tie flicker.
- **R9 Cost.** The forward pass time with the hardened bias stays within the
  budget of G7.
- **R10 Both depth conventions and every depth format.** R1 to R5 hold for
  reverse-Z and forward-Z, and for every depth format the device offers.

## 3. Design

Correctness defects that precede any bias work:

- **D0 Depth format of the shader variant.** Landed: the `SHADOW_DEPTH_BITS`
  axis is `get_shadow_depth_bits_axis()` of the shadow map texture actually
  created (16 / 24 UNORM, 32 float), so a requested 24 that resolves to
  D32_SFLOAT compiles the float path (shadows.md "Shadow sampling").
- **D8 Forward-Z comparison.** Landed: `Shadow_renderer` treats the
  rasterizer depth bias parameters as signed toward the light in both
  conventions and negates them for forward-Z. Forward-Z keeps its receivers
  near depth 1.0, where the float step is 2^-24, so the head-on tie and
  derivative quantization are larger there than under reverse-Z; D1's format
  term and D2 / D3 cover that (R10).

The bias, in the order it is built:

- **D4 The 2.0 bias scale.** Landed: the factor is removed; every tap is
  offset to its own fetched texel's centre with scale 1 plus the caster vertex
  snap term (shadows.md "What erhe implements (RPDB)"). `grazing_fan` tiles
  read no acne for every filter and wide bias mode; the head-on receivers
  (directional `head_on_floor`, `grazing_fan` floor, spot `head_on_floor` 150
  pixels at Medium, and the T7 head-on cases) stay for D2 / D3 and D1.
- **D2 / D3 Plane-derived depth gradient.** Landed: `dz_dUV` is `-a / c`,
  `-b / c` of the receiver plane transformed to texture space by
  `transpose(world_from_texture)`, from the geometric normal of the world
  position derivatives; edge-on to the camera keeps the gradient, edge-on to
  the light tilts the plane to `N . L = 0.05` (shadows.md "Receiver depth
  gradient"). Medium directional and spot `head_on_floor` pass, the
  directional `grazing_fan` floor drops to 1 pixel next to a tile; T7's head-on cases pass at the
  identity pose for k >= 0 (D32). What stays for D1: spot `cornell`, the
  forward-Z head-on cells, the T7 rotated poses (reference / stored depth
  rounding of the matrix composition, identical for every filter, plus the
  gradient's fp32 noise on the wide `receiver_plane` path) and the T7 16-bit
  cases.
- **D1 Derived minimum bias.** Landed: every tap reference moves toward the
  light by the sum of derived error bounds, in texture depth units -
  projection (two fp32 evaluations of `(T_z . P) / (T_w . P)`, gamma_4 times
  the row magnitudes at `P`, plus the divide), position (the receiver
  point's rounding `sqrt(3) gamma_4 |P|` over `|c h.w|`), raster (`4u |z|`),
  gradient (the normal's error bound from the derivative rounding times the
  filter's tap reach, through `D_u`, `D_v`, `D`) and format (one UNORM
  quantum on the gather paths, one float ulp) - with the terms, their
  derivation and the undetermined-plane rule in shadows.md "Minimum bias" and
  "Undetermined receiver plane". The plan's single `|P| 2^-23` and texel terms
  became these separate bounds. Preset fields `shadow_bias_texel_scale`
  (gradient term) and `shadow_bias_origin_scale` (projection, position,
  raster), default 1, apply to the hard, 2x2 and wide paths alike;
  `shadow_depth_bias_constant` stays the caster-side control. T7's head-on
  cases pass at every pose, filter, bias mode and format (D16, D32) for
  k = -4 .. +4 (the bound is 15 to 95 ulps against at most about 5 measured),
  T8 passes. Core matrix: spot `cornell`, directional `head_on_floor` and all
  forward-Z head-on cells pass; `head_on_floor` `--poses full` passes for Low,
  Medium and High (directional and spot); `contact_blocks` G3 / G5 on Low and
  Medium are unchanged. The G1 / G2 pixels left after D1 (directional
  `grazing_fan` floor next to the 88 degree tile's shadow tip, and the 512
  resolution cells) are ideal-filter results: at every one of them the
  analytic occlusion of each tap's texel-centre ray reproduces the measured
  visibility (to one tap at a caster edge), and the analytic boundary is 0.5
  to 1.75 texels (L-inf) away, inside the band radius of 3 / 4 texels.
  The band sampling missed them (a shadow corner or a whole 10 cm caster's
  shadow between the footprint corners, hidden from the camera by its
  caster); section 6 now evaluates the band exactly.
- **Border and filter reach.** Landed: the 2D pass's empty scissor border is
  `max(1, ceil(reach))` texels for the filter's tap reach (hard 0.5, KxK
  K / 2), and the directional fit (stable and tight, snapped the same way)
  and the spot frustum keep every covered receiver `border + reach - 0.5`
  texels inside the map, so no tap of a covered receiver reads the border
  (shadows.md "Empty border and receiver coverage"). This fixed the pairwise
  `pw14` `pcf_6x6` / 512 `cube_seams` G2 pixel whose leftmost tap column read
  texel column 0.
- **D5 Cull mode default.** `cull_back` stores every lit front face, so the
  head-on tie is the common case; `cull_front` stores back faces of closed
  meshes and removes the tie structurally; single-sided geometry needs
  `cull_back`. With D1 in place the bias passes R1 in all three modes, and the
  preset default is chosen from the matrix: G2 / G4 (cull_front leaks, section
  9) against G1 / G3. Midpoint and second-depth maps need an extra depth layer
  per light and are outside this plan. Landed: `cull_back` is the default -
  the codegen default in `graphics_preset_entry.py`,
  `Shadow_renderer::Render_parameters::cull_mode`, the editor fallbacks and
  shadows.md "Shadow pass mechanics"; the three shipped presets already used
  it. Measured with the phase 4 code (`--config` Low, Medium, High and the
  two Medium cull rows, short sweep, 3 re-runs): `cull_back` and `cull_none`
  pass every directional and spot gate, with identical numbers in every cell
  (Medium G3 0.12 / 0.08 texels directional / spot, G5 0.34 / 0.94): on
  closed meshes `cull_none` stores the same nearest front surface and
  rasterizes twice the faces. `cull_front` fails G2 / G4 structurally (the
  section 9 row). Its failures were traced tap by tap: an ideal-filter model
  (per tap the texel-centre ray, the receiver plane point on it, and the
  nearest box exit as the stored back face) reproduces the measured
  visibility within one tap for at least 99.4 % of the failing pixels of
  every traced image at `shadow_depth_bias_slope` 0 (e.g. `contact_blocks` cube
  6148 / 6168 directional, 12239 / 12248 spot; `thin_walls` 10 cm 382 / 384;
  `cube_seams` ceiling 1597 / 1599). Every failing pixel lies 0 to 4.4 texels
  from a box it touches, and its lit taps read either a tie with a back face
  coplanar with the receiver (the bottom of a caster resting on the floor,
  of a wall on the floor, the roof underside at a wall) or a back face behind
  the receiver plane (the tap's plane point inside the touching box):
  inherent to storing back faces at contacts. Medium's rasterizer slope bias
  -1, meant for `cull_back`, moves the stored back faces away from the light
  and widens the leak: with slope 0 `cull_front` passes G3 (0.27 / 0.72) and
  G4 shrinks to 5 .. 12 pixels (directional) and 36 .. 417 (spot), but G2
  still fails (`contact_blocks` 6168 / 12541, `cube_seams` 1599 / 74,
  `spot_cones` 2 / 98) and G4 at 2 cm and up still fails.
- **D6 Point-light bias.** The cube pass always rasterizes both faces
  (`cull_none`), so the lit face is always stored and the tie is structural:
  the D1 formula applies with the cube texel's world footprint
  `2 * distance * tan(45 deg) / point_shadow_resolution` (the face-centre
  upper bound; corners are smaller by `cos^2`) in place of the fixed
  `max(0.05, 0.02 * distance)`, on the radial distance the cube stores.
- **D7 Distance technique for spot lights.** The `distance` technique extends
  to spot lights by storing the radial distance from the light, with the same
  fwidth caster bias; directional keeps its linear light-space depth, point
  lights keep their own cube path. The preset UI offers `distance` for
  directional and spot.

## 4. Test scenes

`scripts/creations/creation_25_shadow_test_rooms.py` builds the stations and
saves them as `res/editor/assets/shadow_test_rooms/shadow_<station>.glb`
(doc/agents/creations.md "25 - Shadow Test Rooms"). Every station holds one
shadow-casting light "Shadow Light" under a single root node, white Lambertian
materials and ambient 0, and is built only from boxes, so ground truth is
analytic (section 6). The module exports every box, the default light pose per
type, the views, the pose sweeps and the contact / wall regions as data for
`shadow_verify.py`. The light type and pose are set per measurement, so one
station serves all three light types.

| Station | Content | Requirements exercised |
|---|---|---|
| `head_on_floor` | Large floor, light on the axis above it | R1 at `dz_dUV = 0`, R6 |
| `grazing_fan` | Tiles at 0, 15, 30, 45, 60, 75, 85, 88 degrees to the light axis | R1 across orientations, D2 / D3, D4 |
| `contact_blocks` | Cube, 1 cm plate, thin post resting on the floor | R2, R3, R5 |
| `thin_walls` | Closed huts with 1, 2, 5, 10, 20 cm walls, viewed from inside | R4 |
| `depth_range` | Non-casting floor beyond the fitted far plane, caster near the light | R1, R2 at the clamp paths of shadows.md "Receivers outside the fitted depth range" |
| `cube_seams` | Point light in a closed room, casters on cube face boundaries | R1, R2 for the point cube |
| `spot_cones` | Spot aimed at a floor, 5, 45 and 80 degree cones | R1, R5 across projection widths |
| `cornell` | `gi_cornell.glb` at its saved light pose | the section 1 case |

Two measurement variants reuse the stations: R7 translates the root of
`head_on_floor` and `contact_blocks` by 1 km and 10 km, and the
`--extra-light` variant adds a non-shadow directional light ahead of the
shadow light in the light buckets, which exercises the
`shadow_index_packed.x` layer indirection of `sample_light_visibility()`.

## 5. Test matrix

The axes and their values:

- light type: directional, spot, point
- `shadow_filter`: hard, pcf_2x2, pcf_4x4, pcf_6x6
- `shadow_bias`: receiver_plane, slope_scaled (wide filters)
- `shadow_technique`: depth, distance (directional and spot, D7)
- `shadow_depth_bits`: each depth size the device offers (16 and 32 on the
  development iGPU)
- `shadow_cull_mode`: cull_front, cull_back, cull_none
- depth convention: reverse-Z, forward-Z
- `use_draw_lists`: on, off
- `shadow_resolution` / `point_shadow_resolution`: 512 and 2048

The **core matrix** is every committed preset plus one-axis-at-a-time
variations around Medium, run with the short pose sweep (5 poses per station)
and `--runs 1`; a failing cell is re-run three times before it counts. A phase
exit additionally runs `head_on_floor` with the full sweep (425 poses: 200
heights in 1 mm steps, 200 over 0.5 to 10 m, a 5 x 5 lateral grid) for the
three presets, because the short sweep can miss the tie at a given pose. The
**pairwise matrix** (`--matrix pairwise`) is every committed preset plus an
all-pairs covering array over the axes other than light type (`shadow_depth_bits`
requested as 16, 24 and 32; `point_shadow_resolution` equal to
`shadow_resolution`), each config measuring all three light types, with the
short pose sweep, `--runs 1` and the same re-run rule: every pair of values of
any two axes is in at least one config, so a failure that needs two settings
together shows up. `shadow_bias` only applies to wide filters, so its pairs
are covered by wide-filter configs. The product of the axes is 864 configs,
about 30 hours at the core matrix's rate, which is not a usable gate; the
covering array is 14 configs. It runs at the end of phase 4 and phase 8.

## 6. Measurement and gates

Ground truth: `shadow_verify.py` renders the receiver world position (mode 36,
fp32) and the per-light visibility (mode 30) with MSAA off, so each pixel has
one surface. It casts each pixel's segment to the light against the station's
boxes (slab test, own box excluded) and classifies the pixel `lit`,
`shadowed`, or `edge band`. The filter reads the texels whose centres lie
within the filter radius (L-inf, in shadow-map texels: hard 0.5, pcf_2x2 1,
pcf_4x4 2, pcf_6x6 3) of the sample point, and a texel stores the nearest
caster on its centre's light ray, so a tap sees a caster exactly where the
receiver plane point on its ray is analytically occluded. The edge band is
therefore every pixel whose footprint square - half-size filter radius plus
one texel (the caster's rasterization), around its texel coordinates (T3
matrices) - is not of one analytic class on the receiver's face plane. It is
evaluated exactly, per receiver face, against the convex texel-space shadow
polygon of each box caster: a lit pixel is in the band when its square meets
a polygon, a shadowed pixel when the polygons do not cover its square.
Sampling the square is not enough: at 512 texels the square spans 6 to 16 cm
and a whole 10 cm caster's shadow, or the tip of a nearly edge-on tile's
shadow, fits between its samples. Pixels on no box, facing away or past the grazing limit
(`N . L < 0.05`), outside the spot cone, the map border or the light range
are excluded. Edge-band pixels are excluded from G1 and G2 and are what G5
measures.

Every gate is the worst value over all poses and runs:

- **G1 Acne:** `lit` pixels with visibility < 0.999 = 0.
- **G2 Occlusion:** `shadowed` pixels with visibility > 0.001 = 0.
- **G3 Contact gap:** distance from a resting caster's footprint edge to the
  receiver's 0.5-visibility crossing <= 1.5 shadow texels (depth technique)
  and <= 2.5 texels (distance technique, whose fwidth bias is an L1 bound).
- **G4 Leaks:** walls of 2 cm and thicker at map resolution >= 2048: lit
  pixels inside the hut = 0. Thinner walls are reported, not gated.
- **G5 Edge placement:** mean signed offset of the measured 0.5-visibility
  edge from the analytic edge <= 1 texel; worst <= filter radius + 1 texel.
- **G6 Stability:** eight camera translations of 1/8 of the shadow texel's
  world size at the view target, along camera right and up: pixels outside
  the edge band whose visibility changes = 0.
- **G7 Cost:** forward pass GPU time on the `cornell` and `contact_blocks`
  views, median of at least 5 runs, within 10 percent of the median before the
  change on the same machine; absolute numbers go to `memory-bank/local/`.

## 7. Tooling

- **T1** `set_graphics_preset` (MCP): sets the shadow fields of the preset in
  effect and `use_draw_lists` for the session. D1 adds its two fields here.
- **T2** `Shader_debug::world_position` (36): the receiver world position.
- **T3** `render_scene_image` returns `shadow_lights` / `shadow_maps` from its
  own shadow pass, captures fp32 with `color_format: "rgba32f"`, and marks
  uncovered pixels NaN in linear debug renders.
- **T4** `Shader_debug::shadow_visibility` (30) shows the light named by
  `render_scene_image`'s `shadow_debug_light`, for all three light types.
- **T5** `ERHE_FORCE_DISABLE_REVERSE_DEPTH=1|0` selects the depth convention
  without editing `erhe_graphics.json`.
- **T6** `scripts/shadow_verify.py` runs section 5 and 6 (usage in its
  docstring); `--enforce` exits non-zero on a FAIL. It gains `--extra-light`
  (section 4) and the G7 timing in phase 8.
- **T7 Library GPU tests** in `erhe_scene_renderer_gpu_tests` (ctest label
  `gpu`): [shadows.md](../erhe/shadows.md) "Shadow sampling GPU tests",
  including the two head-on cases (`Shadow_tie` and `Shadow_head_on_plane`)
  with no rasterizer bias. The process-wide test device has
  one depth convention (reverse-Z on Vulkan), so forward-Z runs of these
  cases need the GPU test environment to create a forward-Z device. These
  run under a software Vulkan once [graphics_tests.md](graphics_tests.md)
  brings `gpu` tests to CI.
- **T8 MCP regression case.** `Mcp_test.shadow_head_on_receivers_have_no_acne`
  (label `editor`) asserts G1 on mode 30 renders of `shadow_head_on_floor.glb`
  (spot at 4.711 m, directional straight down) and `gi_cornell.glb` at its
  saved pose, with Medium's shadow fields pinned and no rasterizer bias. The
  control `shadow_head_on_receivers_have_no_acne_with_constant_depth_bias`
  runs the same measurement with rasterizer constant bias -4 and passes.

## 8. Phases

Each phase ends with the core matrix, one commit per logical change, and
section 9 rewritten to the new gate table.

- **Phase 6 - point lights (D6).** `cube_seams`, `thin_walls` and
  `contact_blocks` point rows to green.
- **Phase 7 - coverage.** D7 (spot distance technique), R7 origin runs, the
  `--extra-light` variant.
- **Phase 8 - cost and documentation.** G7; rewrite shadows.md "Shadow
  sampling" and "Bias technique" for the landed design, add the verify recipe
  to `doc/testing.md`, run the pairwise matrix, and delete this plan's
  finished items.

## 9. Gate table

`py -3 scripts/shadow_verify.py --matrix core --save-images failing` on the
current code: 16 configs (Low, Medium, High and the one-axis variations
around Medium; a requested `shadow_depth_bits` of 24 resolves to D32_SFLOAT
on this device, axis 32), `--poses short`, `--runs 1`, failing cells re-run
3 times (every listed cell failed all 3 re-runs); 10069 renders, 51.5 min
wall on the Debug headless Vulkan editor, AMD iGPU. Directional and spot pass
every gate in every `cull_back` and `cull_none` cell of the depth technique,
including forward-Z, 512 and 2048, except one 16-bit pixel (listed). Cells
not listed pass every gate
that applies to them. Values are the worst over poses and views: failing
pixel count and share of the gated pixels (G1, G2), texels (G3, G5 as
mean / worst), pixels (G4 per wall, G6).

| Config | Failing cells |
|---|---|
| Medium/shadow_cull_mode=cull_front (inherent to storing back faces, D5; not the default) | G4 on every `thin_walls` hut (directional 1 cm: 1115, 2 cm: 882 .. 20 cm: 598, spot 2 cm: 1920 .. 20 cm: 4360); `contact_blocks` G2 8983 (1.8 %) / 15915 (3.2 %), G3 2.28 / 2.25, G5 worst 8 (the spurious crossing at the contact line), directional G6 4; `cube_seams` G2 1683 / 113; `spot_cones` G2 210 / 490 (33 %) |
| Medium/shadow_technique=distance (D7) | spot `cornell` G1 56064 (9.7 %, the head-on tie); `grazing_fan` G1 898 (dir) / 676 (spot); `contact_blocks` G3 3.25 (dir) / 3.5 (spot), G2 20 / 89, G1 1 / 15, G5 0.29 / 3.25 (dir) and 0.24 / 3.75 (spot), directional G6 17; directional `thin_walls` G2 1, spot `thin_walls` G1 139, spot `cube_seams` G2 2, spot `spot_cones` G1 1 |
| Medium/shadow_depth_bits=16 | spot `contact_blocks` G2 1 pixel (`contact_post` view, poses 2 to 4): 0.37 texel from the post's contact line, visibility 1 / 16. The lit tap's stored entry face is 13.0 mm nearer than its receiver plane point (the next tap 18.2 mm), inside the 16-bit caster and receiver bias at 4.1 m: one UNORM quantum of the D1 format term (about 6.4 mm), the stored value's rounding (up to half a quantum) and Medium's rasterizer slope bias -1 on the post's 64 degree side face (about 5 mm). A contact gap G3 allows (the cell's G3 is 0.08 texel), counted by G2 because the band covers the analytic shadow boundary, not the contact line |
| every config, point (D6) | `contact_blocks` G2 337, G3 4.75, G5 0.15 / 3.25 (High and 2048: G2 2538, G3 9.5, G5 0.40 / 6.5; Low and 512: G2 3, G3 2.5, G5 2.75); `thin_walls` G2 1 (High and 2048: G2 2, G4 2 cm: 21002, 5 cm: 5519, 1 cm: 32581); `cube_seams` G2 4 (High and 2048: 10); Low and 512 `grazing_fan` G1 44 |

`py -3 scripts/shadow_verify.py --matrix pairwise` on the current code: 17
configs, 11680 renders, 67.1 min wall; no failing cells outside D5 / D6 / D7
(99 failing cells: 44 D7, 36 D6, 19 D5), directional G6 passes in every
depth-technique cell.

Point lights fail only through the constant world-space bias (D6): the
contact gap and the leak through 2 and 5 cm walls. The distance technique
keeps the head-on tie on spot `cornell`, which the depth technique's D1 bias
resolves.
