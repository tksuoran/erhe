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

The tooling (T1 to T6) and the test stations (section 4) exist; section 9 is
the current gate table. The remaining work is phases 2 to 8.

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

- **D4 The 2.0 bias scale.** The unexplained `2.0 *` factor in every RPDB
  slope term is derived or removed first, measured on `grazing_fan` per tile
  angle with no D1 floor, because it scales every term the later items build
  on.
- **D2 / D3 Plane-derived depth gradient.** `dz_dUV` is computed from the
  receiver plane instead of the screen-space Jacobian: the plane (N, d) of the
  receiver, N the geometric normal (`normalize(cross(dFdx(p), dFdy(p)))` of the
  world position, which is exact for planar triangles and independent of
  smooth vertex normals), is transformed by the inverse transpose of
  `texture_from_world` to texture space (a, b, c, e), and
  `dz/du = -a / c`, `dz/dv = -b / c` - exact for planes and projectively
  correct for spot lights. Two degenerate cases get explicit rules: a receiver
  edge-on to the camera no longer loses its bias (the Jacobian path's
  `detJ -> 0` leaves `dz_dUV = 0`), and a receiver edge-on to the light
  (`c -> 0`) clamps `|dz_dUV|` to the slope at the R1 grazing limit
  `N . L = 0.05`. `N_dot_L`, passed to `sample_light_visibility()` today and
  unused, is replaced by the plane.
- **D1 Derived minimum bias.** Every receiver gets a depth bias of at least
  `bias = (dz / dworld) * (k_texel * texel_world + k_origin * |P| * 2^-23) +
  q_format`, where `dz / dworld` is the light projection's depth derivative at
  the receiver (reverse-Z perspective: near / z^2; orthographic: constant),
  `texel_world` the shadow texel's world size at the receiver, `|P|` the
  receiver's distance from the origin (the fp32 error of `v_position` and of
  the composed `texture_from_world`), and `q_format` one quantum of the
  map's actual format (D0) at the reference depth: `2^-bits` for UNORM,
  the float step `ulp(z_ref)` for float (negligible near 0 under reverse-Z,
  2^-24 near 1.0 under forward-Z). The
  coefficients `k_texel` and `k_origin` are derived from the filter footprint
  and the matrix composition, stated with their derivation in shadows.md, and
  exposed as preset fields `shadow_bias_texel_scale` and
  `shadow_bias_origin_scale` (dimensionless, default 1) that apply to the
  hard, 2x2 and wide paths alike - orthogonal to the wide-only
  `Shadow_bias_mode` axis. The receiver-side floor is used rather than the
  rasterizer constant bias because the rasterizer unit depends on each
  primitive's depth extent and on the format; `shadow_depth_bias_constant`
  stays as the caster-side control.
- **D5 Cull mode default.** `cull_back` stores every lit front face, so the
  head-on tie is the common case; `cull_front` stores back faces of closed
  meshes and removes the tie structurally; single-sided geometry needs
  `cull_back`. With D1 in place the bias passes R1 in all three modes, and the
  preset default is chosen from the matrix: G2 / G4 (cull_front leaks, section
  9) against G1 / G3. The decision names the codegen default in
  `graphics_preset_entry.py`, the three shipped presets and the shadows.md
  statement, which today disagree (codegen and shadows.md say `cull_front`,
  the presets use `cull_back`). Midpoint and second-depth maps need an extra
  depth layer per light and are outside this plan.
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
**full matrix** is the product of the axes with the short sweep; it runs at
the end of phase 4 and phase 8.

## 6. Measurement and gates

Ground truth: `shadow_verify.py` renders the receiver world position (mode 36,
fp32) and the per-light visibility (mode 30) with MSAA off, so each pixel has
one surface. It casts each pixel's segment to the light against the station's
boxes (slab test, own box excluded) and classifies the pixel `lit`,
`shadowed`, or `edge band` - within the filter footprint plus one texel of the
analytic shadow boundary in shadow-map texel space (T3 matrices), found by
sampling the footprint corners in the receiver plane and dilating in image
space. Pixels on no box, facing away or past the grazing limit
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
  `gpu`), in the pattern of `test_content_line_width_gpu.cpp`, built on a
  test fixture that sets up what the shadow and forward passes need beyond
  the `Device` (`Program_interface`, `Shader_variant_cache`, `Mesh_memory`,
  `Light_set`, `Material_set`):
  - `Shadow_tie`: `Shadow_renderer` rasterizes a head-on plane into the map;
    a fullscreen fragment pass that includes `erhe_light.glsl` evaluates
    `sample_light_visibility()` at receiver points on that plane with the
    reference depth perturbed by -4 to +4 ulps (fragment stage, because the
    sampling uses screen-space derivatives); every result is 1. Runs per
    filter, bias and depth format.
  - `Shadow_head_on_plane`: `Shadow_renderer` plus `Forward_renderer` with
    `Shader_debug::shadow_visibility` on a plane under a spot and a
    directional light; every pixel reads 1.
  These run under a software Vulkan once
  [graphics_tests.md](graphics_tests.md) brings `gpu` tests to CI.
- **T8 MCP regression case.** One `Mcp_test` case in `mcp_server_tests`
  (label `editor`) loads `shadow_head_on_floor.glb` and the `cornell` pose and
  asserts G1 on a mode 30 render for spot and directional.

## 8. Phases

Each phase ends with the core matrix, one commit per logical change, and
section 9 rewritten to the new gate table.

- **Phase 2 - correctness defects.** D0, then D8. Exit: the depth-bits and
  forward-Z rows of section 9 match their reverse-Z / 32-bit counterparts
  apart from the bias failures D1 addresses.
- **Phase 3 - library and MCP tests.** T7 and T8; both fail on the head-on
  cases before phase 4, which confirms they detect the tie.
- **Phase 4 - bias.** D4, then D2 / D3, then D1, each measured on its own.
  Exit: G1 to G5 pass for directional and spot in every `cull_back` and
  `cull_none` cell, T7 and T8 pass, `head_on_floor` full sweep passes, then
  the full matrix.
- **Phase 5 - cull mode (D5).** Decide the default from the matrix with D1 in
  place; update the codegen default, the presets and shadows.md together.
- **Phase 6 - point lights (D6).** `cube_seams`, `thin_walls` and
  `contact_blocks` point rows to green.
- **Phase 7 - coverage.** D7 (spot distance technique), R7 origin runs, the
  `--extra-light` variant.
- **Phase 8 - cost and documentation.** G7; rewrite shadows.md "Shadow
  sampling" and "Bias technique" for the landed design, add the verify recipe
  to `doc/testing.md`, and delete this plan's finished items.

## 9. Baseline

Baseline on the current code: `py -3 scripts/shadow_verify.py --matrix core
--save-images failing` (15 configs: Low, Medium, High and the one-axis
variations around Medium; a requested
`shadow_depth_bits` of 24 resolves to D32_SFLOAT on this device (axis 32); `--poses short`, `--runs 1`;
5464 renders, 34.9 min wall on the Debug headless Vulkan editor, AMD iGPU).
Cells not listed pass every gate that applies to them. Values are the worst
over poses and views: failing pixel count and share of the gated pixels
(G1, G2), texels (G3, G5 as mean / worst), pixels (G4 per wall, G6).

| Config | Failing cells |
|---|---|
| Medium (and its filter, bias, cull_none, draw-list, 2048 variations) | spot `cornell` G1 56064 (9.7 %, the section 1 tie); directional `head_on_floor` G1 55296 (9.4 %); directional `grazing_fan` G1 4.1 %, spot 0.17 %; point `contact_blocks` G2 117, G3 4.75, G5 0.15 / 3.25; directional `contact_blocks` G6 14 and a few G1 pixels at the cube's lit top edges |
| Low | directional `contact_blocks` G1 2404, G5 0.02 / 1.81, G6 3304; directional `spot_cones` G1 3062; `thin_walls` G1 1733 (dir) / 776 (spot); spot `cornell` G1 729; point `contact_blocks` G3 2.5, G5 2.75 |
| High, Medium/resolution=2048 | point `contact_blocks` G2 1126, G3 9.5, G5 0.40 / 6.5; point `thin_walls` G4 2 cm: 21002, 5 cm: 5519 (1 cm: 32581) |
| Medium/shadow_depth_bits=16 | G1 on every directional / spot station: `spot_cones` 74 %, `grazing_fan` 94 % / 92 %, `head_on_floor` 67 % / 100 %, `cornell` 59 % |
| Medium/shadow_cull_mode=cull_front | G4 on every `thin_walls` wall (directional 2 cm: 1012 .. 20 cm: 394, spot 2 cm: 1797 .. 20 cm: 4738); G2 on `contact_blocks` (1.8 % / 3.3 %), `cube_seams`, `spot_cones` (spot 62 %); G5 worst 8 (no edge found) |
| Medium/shadow_technique=distance | `contact_blocks` G3 3.25 (dir) / 3.5 (spot), spot G5 3.75; small G1 on `grazing_fan`, `thin_walls` |
| Medium/resolution=512 | `head_on_floor` G1 15 % (dir); `grazing_fan` G1 7 % (dir); spot `contact_blocks` G1 679 |
| Medium/forward_z | directional matches reverse-Z except `grazing_fan` G1 48 % (one pose) and `head_on_floor` passing; spot G1: `head_on_floor` 100 %, `cornell` 30 %, `spot_cones` 12 %, `contact_blocks` 1.4 %, `depth_range` 243 - float ties near depth 1.0 (D1, D2 / D3); point matches reverse-Z |

Point lights fail only through the constant world-space bias (D6): the
contact gap and the leak through 2 and 5 cm walls. The head-on tie shows in
the short sweep only for `cornell` and directional `head_on_floor`; spot
`head_on_floor` fails on 123 of the 425 poses of `--poses full` (G1 33 %) and
passes all of them with `--set shadow_depth_bias_constant=-4`.
