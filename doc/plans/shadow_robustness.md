# Shadow robustness

Status: proposed

Hardens the directional, spot and point shadow paths against self-shadowing,
leaks and precision ties, and adds the dedicated test scenes and automated
checks that hold them there. The shadow pipeline itself is described in
[`shadows.md`](../erhe/shadows.md) and
[`point_light_shadows.md`](../erhe/point_light_shadows.md); this plan owns all
receiver-side and caster-side shadow bias work, including the deltas from the
RPDB reference (D2 to D4). Fit and performance follow-ups stay in
[`shadows.md`](shadows.md).

## 1. Evidence: the head-on tie

The defect that motivates the plan, measured in a RenderDoc capture of
`res/editor/assets/gi_test_rooms/gi_cornell.glb` (Vulkan, AMD iGPU, Medium
preset: `pcf_4x4`, `receiver_plane`, `depth`, D32 shadow map, reverse-Z):

- The spot light sits at (0, 2.9, 0) and points straight down, so the floor is
  exactly perpendicular to the light axis. A band of the floor renders black
  (ambient 0, DDGI off, so black = shadow visibility 0). The band moves when
  the light moves, and vanishes when the light becomes a point light.
- At a pixel in the band the shadow map stores 0.0104947509 and the shader's
  reference depth is 0.0104947500 - one float ulp nearer the far plane - so the
  non-strict reverse-Z comparison fails. RenderDoc's shader debugger replays
  the same pixel as lit: the verdict is decided by last-bit rounding.
- Every bias term is zero for this receiver. All three committed presets use
  `shadow_cull_mode: cull_back`, so the lit floor face itself is in the shadow
  map; `shadow_depth_bias_constant` is 0; the rasterizer slope bias and the
  receiver-plane bias both scale with the light-space depth gradient `dz_dUV`,
  which is exactly 0 on a surface facing the light head-on.
- Setting "Shadow Depth Bias (constant)" to -4 removes the band live.
- Point lights are immune because `sample_point_light_visibility()` applies a
  world-space bias of `max(0.05, 0.02 * distance)`; that bias has its own
  contact-gap cost (R3).

The general lesson the plan is built on: any receiver whose stored depth and
reference depth are computed from the same surface is a tie, and a tie must be
resolved by a bias that is nonzero for every orientation, not only for sloped
ones.

## 2. Requirements

Each requirement holds for every supported configuration in the test matrix
(section 5) and is checked by a gate in section 6.

- **R1 No self-shadowing.** A receiver point whose segment to the light is
  unobstructed has visibility 1, for every receiver orientation from head-on
  (`N . L = 1`, `dz_dUV = 0`) to grazing (`N . L = 0.05`), and for every light
  pose.
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
- **R6 Pose independence.** R1 to R5 hold at every pose of a fine light pose
  sweep; no verdict depends on where last-bit rounding falls.
- **R7 Origin independence.** R1 to R5 hold with the whole station translated
  1 km and 10 km from the origin.
- **R8 Temporal stability.** For a static scene, a sub-texel camera
  translation changes directional shadow visibility only inside the edge band.
- **R9 Cost.** The forward pass time with the hardened bias stays within the
  budget of G7.

## 3. Bias design

- **D1 Nonzero bias for every orientation.** The receiver-side bias gets a
  term that does not vanish with `dz_dUV`. Two defensible forms are built and
  compared, both selectable through `Shadow_bias_mode`:
  - `normal_offset`: offset the receiver's world position along its geometric
    normal before projecting into light space, scaled by the shadow texel's
    world-space size at that point and by `(1 - N . L)` plus a floor. Handles
    head-on ties and grazing acne with one mechanism and is the common
    production choice.
  - `depth_floor`: keep the receiver-plane bias and add a constant depth-space
    floor of a few quanta of the stored format at the reference depth (UNORM
    quantum for D16 / D24, ulp-scaled for D32F).
  The matrix (section 5) and the gates choose the preset defaults; the loser
  stays as an option only if it wins on some axis.
- **D2 Use the surface normal.** `sample_light_visibility()` receives
  `N_dot_L` and ignores it. The geometric slope from the normal feeds D1 and
  replaces the screen-space Jacobian where the Jacobian is unreliable (D3).
- **D3 Degenerate and discontinuous derivatives.** When `detJ` is near zero
  (grazing, silhouette texels) or the 2x2 quad straddles two surfaces,
  `dz_dUV` comes from the normal (D2) instead of `ddx` / `ddy`.
- **D4 The 2.0 bias scale.** The unexplained `2.0 *` factor in the RPDB bias
  terms (absent from the reference article) is derived or removed; the gates
  decide.
- **D5 Cull mode defaults.** `cull_back` puts every lit front face in the map,
  which makes the head-on tie the common case; `cull_front` stores back faces
  of closed meshes; single-sided geometry needs `cull_back`. The bias passes
  R1 in all three cull modes, and the preset default is then chosen from the
  matrix numbers (R3 contact gap versus R4 leaks).
- **D6 Point-light bias.** The constant `max(0.05, 0.02 * distance)` becomes a
  bias derived from the cube texel's world footprint
  (`2 * distance * tan(45 deg) / point_shadow_resolution`) plus the D1 normal
  offset, so the contact gap scales with resolution instead of being a fixed
  5 cm.
- **D7 Distance technique coverage.** The matrix states which light types the
  `distance` technique supports; supported combinations meet the same gates,
  unsupported combinations are rejected by the preset UI.

## 4. Test scenes

Built by a new `scripts/creations/creation_25_shadow_test_rooms.py` in the
pattern of `creation_24_gi_test_rooms.py` (stations, views as data,
`--save-assets` writes `res/editor/assets/shadow_test_rooms/shadow_<station>.glb`
and joins the `.gitignore` allow-list). Every station holds exactly one
shadow-casting light, white Lambertian materials, ambient 0, and is built only
from boxes and planes, so ground truth is analytic (section 6). The light type
and pose are set per measurement with `edit_light`, so one station serves all
three light types.

| Station | Content | Requirements exercised |
|---|---|---|
| `head_on_floor` | Large floor, light on the axis above it | R1 at `dz_dUV = 0`, R6 (pose sweep) |
| `grazing_fan` | Tiles at 0, 15, 30, 45, 60, 75, 85, 88 degrees to the light axis | R1 across orientations, D3 |
| `contact_blocks` | Cube, 1 cm plate, tall thin post resting on the floor | R2, R3, R5 |
| `thin_walls` | Walls 1, 2, 5, 10, 20 cm thick, closed corners, light on one side | R4 |
| `depth_range` | Receiver below `fit_to_casters` far plane, caster near the near plane | R1, R2 at the clamp paths of shadows.md "Receivers outside the fitted depth range" |
| `cube_seams` | Point light in a ring room, casters straddling cube face boundaries | R1, R2 for the point cube |
| `spot_cones` | One spot aimed at a floor with outer angles 5, 45 and 80 degrees | R1, R5 across projection widths |
| `cornell` | `gi_cornell.glb` at its saved light pose | the section 1 case as a regression |

R7 runs `head_on_floor` and `contact_blocks` with the station root translated
by 1 km and 10 km.

## 5. Test matrix

The axes and their values:

- light type: directional, spot, point
- `shadow_filter`: hard, pcf_2x2, pcf_4x4, pcf_6x6
- `shadow_bias`: receiver_plane, slope_scaled (wide filters), plus the D1 modes
- `shadow_technique`: depth, distance (where D7 supports it)
- `shadow_depth_bits`: 16, 24, 32
- `shadow_cull_mode`: cull_front, cull_back, cull_none
- depth convention: reverse-Z, forward-Z
- `use_draw_lists`: on, off
- `shadow_resolution` / `point_shadow_resolution`: 512 and 2048

The **core matrix** is every committed preset plus a one-axis-at-a-time
variation around it (the sum of the axis sizes, not their product); it runs
every station with the full light pose sweep. The **full matrix** is the
product, run with a short pose sweep. The pose sweep for `head_on_floor` moves
the light height over 200 steps of 1 mm and 200 steps spread over 0.5 to 10 m,
and offsets it laterally over a 5 x 5 grid; other stations use 25 poses.

## 6. Measurement and gates

Ground truth: the script knows every box and plane of the station. For each
image pixel it takes the receiver world position (T2), casts the segment to
the light against the station's boxes (slab test), and classifies the pixel as
`lit`, `shadowed`, or `edge band` - within the filter footprint plus one texel
of the analytic shadow boundary, measured in shadow-map texels through the
light's texture matrix (T3). Edge-band pixels are excluded from G1 and G2 and
are what G5 measures.

Every gate is evaluated as the worst value over all poses and over `--runs`
full runs (default 3):

- **G1 Acne:** shadowed pixels among `lit` pixels = 0.
- **G2 Occlusion:** lit pixels among `shadowed` pixels = 0.
- **G3 Contact gap:** distance from a resting caster's footprint edge to the
  first fully shadowed receiver pixel <= 1.5 shadow texels (depth technique)
  and <= 2.5 texels (distance technique, whose fwidth bias is an L1 bound).
- **G4 Leaks:** walls of 2 cm and thicker at 2048 resolution: lit pixels
  behind the wall = 0. Thinner walls are reported, not gated.
- **G5 Edge placement:** mean signed offset of the measured 0.5-visibility
  edge from the analytic edge <= 1 texel; worst <= filter radius + 1 texel.
- **G6 Stability:** eight camera translations of 1/8 texel: pixels whose
  visibility changes outside the edge band = 0.
- **G7 Cost:** forward pass GPU time on the `cornell` and `contact_blocks`
  views within 3 percent of the pre-change baseline measured by the same
  script on the same machine; absolute numbers go to `memory-bank/local/`.

## 7. Tooling

- **T1 `set_graphics_preset` MCP tool.** Sets any shadow field of the current
  graphics preset by explicit argument (`shadow_filter`, `shadow_bias`,
  `shadow_technique`, `shadow_depth_bits`, `shadow_resolution`,
  `shadow_cull_mode`, `shadow_depth_bias_constant`, `shadow_depth_bias_slope`,
  `point_shadow_resolution`, the D1 parameters) plus `use_draw_lists`,
  session-only in the manner of `set_graphics_settings`, and returns the values
  in effect. The tool goes through the same change site as the Settings
  window's `on_graphics_preset_edited()`.
- **T2 World-position shader debug mode.** A new `Shader_debug` value writes
  `v_position.xyz` to the linear output, read back with `render_scene_image`
  `output: "linear"` (PFM) as the per-pixel receiver position.
- **T3 Shadow projection in the render result.** `render_scene_image` returns,
  for the shadow pass it rendered, each shadow light's type,
  `texture_from_world`, map resolution and layer, so texel-space distances are
  computed from the matrices actually used (the directional fit is
  per-camera).
- **T4 Per-light shadow visibility.** `shadow_visibility` (mode 30) reads the
  light selected by a `shadow_debug_light_index` field in the light block
  instead of the first shadow-mapped light, and covers point lights through
  `sample_point_light_visibility()`. `render_scene_image` takes the index as an
  argument.
- **T5 Forward-Z without editing config.** An environment override for
  `force_disable_reverse_depth` (read where `erhe_graphics.json` is applied), so
  the verify script launches a forward-Z editor without rewriting a config
  file.
- **T6 `scripts/shadow_verify.py`.** In the pattern of `scripts/gi_verify.py`:
  launches or reuses the headless editor, loads each station asset, walks the
  matrix and pose sweep through T1 and `edit_light`, renders mode 30 and T2
  views with `render_scene_image`, applies section 6, prints a PASS / FAIL
  table, writes `logs/shadow_verify/<timestamp>.json`, and exits non-zero on a
  FAIL with `--enforce`. `--matrix core|full`, `--station`, `--light`,
  `--runs`, `--reuse`, `--port`.
- **T7 Library GPU tests** in `erhe_scene_renderer_gpu_tests` (ctest label
  `gpu`), in the pattern of `test_content_line_width_gpu.cpp`:
  - `Shadow_tie`: a compute shader includes `erhe_light.glsl`, builds the
    stored depth by rasterizing a head-on plane through `Shadow_renderer`,
    evaluates `sample_light_visibility()` for receiver points on that plane
    with the reference perturbed by -4 to +4 ulps, and requires visibility 1
    everywhere. Runs per filter, bias and depth format.
  - `Shadow_head_on_plane`: `Shadow_renderer` plus `Forward_renderer` with
    `Shader_debug::shadow_visibility` on a plane under a spot and a
    directional light; every pixel reads 1.
  These are the part that runs under a software Vulkan once
  [graphics_tests.md](graphics_tests.md) brings `gpu` tests to CI.
- **T8 MCP regression case.** One `Mcp_test` case in `mcp_server_tests`
  (label `editor`) loads `shadow_head_on_floor.glb` and the `cornell` pose and
  asserts G1 on a mode 30 render for spot and directional.

## 8. Phases

Each phase ends with its verification run and one commit per logical change.

- **Phase 0 - tooling.** T1 to T5. Verified by reproducing section 1 over MCP:
  `gi_cornell.glb`, the saved light pose, Medium preset, mode 30 render shows
  the band; constant bias -4 through T1 removes it.
- **Phase 1 - scenes and baseline.** Section 4 stations and assets, T6 with
  the section 6 gates. Run the core matrix on the current code and record the
  baseline table (which gates fail where) in section 9 of this plan.
- **Phase 2 - library tests.** T7 and T8; both fail on the current code for
  the head-on cases, which confirms they detect the defect.
- **Phase 3 - D1 to D3.** Both D1 modes, the normal-based slope and the
  degenerate-Jacobian fallback. Core matrix, then full matrix; choose the
  defaults from G1 to G5 and G7.
- **Phase 4 - D4 and D5.** Resolve the 2.0 factor; re-decide the preset cull
  mode from the matrix.
- **Phase 5 - point lights (D6).** `cube_seams` and the point rows of the
  matrix to green.
- **Phase 6 - distance technique (D7)** and R7 origin independence.
- **Phase 7 - documentation.** Rewrite shadows.md "Shadow sampling" and "Bias
  technique" in the present tense for the landed design, add the verify recipe
  to `doc/testing.md`, list the station assets in `doc/agents/creations.md`,
  and delete this plan's finished items.

## 9. Baseline

Baseline on the current code: `py -3 scripts/shadow_verify.py --matrix core
--save-images failing` (15 configs: Low, Medium, High and the one-axis
variations around Medium; `shadow_depth_bits` 24 is skipped, the device
supports 16 and 32 only, and Medium runs at 32; `--poses short`, `--runs 1`;
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
| Medium/forward_z | G1 on nearly every directional / spot cell (lit surfaces read 0 or 0.25; `head_on_floor`, `contact_blocks`, `thin_walls`, `spot_cones` ~100 %): the forward-Z shadow comparison is broken; point cells match reverse-Z |

Point lights fail only through the constant world-space bias (D6): the
contact gap and the leak through 2 and 5 cm walls. The head-on tie shows in
the short sweep only for `cornell` and directional `head_on_floor`; spot
`head_on_floor` fails on 123 of the 425 poses of `--poses full` (G1 33 %) and
passes all of them with `--set shadow_depth_bias_constant=-4`.
