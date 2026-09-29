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
the current gate table. The remaining work is phase 8.

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
- Point lights compared radial distance with a world-space bias of
  `max(0.05, 0.02 * distance)`, which leaked through thin walls and detached
  contact shadows; D6 replaces it.

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
  1 km and 10 km from the origin. Every bias term's error bound scales with
  a distance that stays small for what the camera sees (distance from the
  camera or the light camera), not with the distance from the world origin
  (D12). The station measured is the one the scene holds: its boxes' world
  transforms are composed in fp32, which far from the origin moves each box
  by up to half an ulp (section 6).
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
  point's rounding `sqrt(3) gamma_4 |P|` over `|c h.w|`), raster (`4u`: the caster primitive's post-clip vertex depths, up to 1,
  interpolated at the texel centre),
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
  Medium are unchanged. The raster term first took the receiver's `|z|` in
  place of the caster's vertex depths; a traced gap showed why that is not a
  bound: the `cube_seams` floor's triangles have a corner behind the spot
  light and are clipped at the near plane (depth 1 under reverse-Z), and
  each stored triangle is the exact plane plus an affine offset of 0.1 to
  7.7 `u` (at a texel depth of 0.02, up to 250 ulps against a 240 ulp bound),
  which read as moire acne below the light on `Medium/shadow_filter=hard`
  (5761 pixels) and `Medium/resolution=512` (187) at rasterizer slope 0. With
  it, the wide paths' gathers are placed on their sets' shared corners and
  the nearest fetch on its texel centre (shadows.md "Tap offsets"): a
  `pcf_6x6` gather whose own coordinate rounded to the neighbouring set had
  flickered one `contact_blocks` pixel under G6. The core matrix at slope 0
  now fails only D5 / D6 / D7 cells, with the same G3 (Low 0.05 / 0.13,
  Medium 0.12 / 0.08, High 0.18 / 0.23 texels, directional / spot). The
  presets' rasterizer bias is 0 since D7 (the distance technique's former
  caster stored `gl_FragCoord.z`, rasterizer bias included).
  The G1 / G2 pixels left after D1 (directional
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
  inherent to storing back faces at contacts. A rasterizer slope bias of -1
  (Medium's until D7) moves the stored back faces away from the light and
  widens the leak: with slope 0 `cull_front` passes G3 (0.27 / 0.72) and
  G4 shrinks to 5 .. 12 pixels (directional) and 36 .. 417 (spot), but G2
  still fails (`contact_blocks` 6168 / 12541, `cube_seams` 1599 / 74,
  `spot_cones` 2 / 98) and G4 at 2 cm and up still fails.
- **D6 Point-light bias.** Landed: the cube pass rasterizes both faces
  (`cull_none`), so the tie is structural, and it is resolved the way the 2D
  paths resolve theirs - an exact per-texel offset plus derived error bounds,
  with no constant world floor (point_light_shadows.md "Stored distance" and
  "Receiver bias"). The receiver fetches the texel containing its direction
  at the texel's centre (`get_point_shadow_texel_centre()`, shared with the
  caster) and compares the receiver plane's distance on that centre ray,
  one-sided; the bounds are the coverage snap (the receiver's radial-distance
  slope times 1/256 pixel), the receiver's and the caster's normal errors
  (`shadow_bias_texel_scale`), and position rounding and fp32 evaluation
  (`shadow_bias_origin_scale`). The caster stores its primitive plane's
  distance on the texel centre ray instead of `length(p - L)`: tracing the
  first version, which bounded the interpolated point by the snap, showed the
  `head_on_floor` floor's near-clipped triangles (tens of thousands of face
  pixels wide) put the interpolated point up to 0.0064 texel off the pixel
  centre ray (G1 10569 pixels at the floor corners, 47110 on
  `contact_blocks`), an error the rasterizer's precision sets and no shader
  quantity bounds; the plane distance does not depend on where on the plane
  the point lands. `point_shadow_resolution` has a minimum of 64
  (`c_min_point_shadow_resolution`) so the centre ray meets every plane R1
  admits. Measured with the core matrix: `cube_seams`, `thin_walls` and
  `contact_blocks` pass in every config (`contact_blocks` G3 4.75 to 1.25
  texels at Medium, 9.5 to 0.06 at High, 2.5 to 1.0 at Low; G5 worst 3.25 to
  1.25, 6.5 to 1.13; G4 at 2048 0 on every hut, 1 cm reported 0; 1 cm at 512
  reported 3864). The Low / 512 `grazing_fan` G1 goes from 44 to 6 pixels,
  all on the 15 degree tile's top face within one texel of its edge with the
  side face, which faces away from the light: the side face's snapped
  coverage reaches the texel centre and its extended plane is 0.18 mm nearer
  than the top face's, against a 0.08 mm bias from the top face's own slope.
  A crease neighbour steeper than the receiver is outside the snap term;
  bounding it by the steepest plane the caster stores would cost up to 0.75
  texel of bias at 512 (D10 resolves it in the caster instead).
- **D7 Distance technique for spot lights.** The `distance` technique extends
  to spot lights by storing the radial distance from the light, with the same
  fwidth caster bias; directional keeps its linear light-space depth, point
  lights keep their own cube path. The preset UI offers `distance` for
  directional and spot. The distance caster pass stores `gl_FragCoord.z`,
  which includes the rasterizer bias, so its cells depend on the presets'
  slope bias -1; D7 gives the distance technique a derived caster bias of its
  own, after which the presets' rasterizer bias goes to 0 (the depth
  technique reads the same gates at 0 as at -1).
  Landed, with the D6 structure in place of the fwidth caster bias: each
  texel stores its caster plane's light distance on the texel's centre ray
  (spot: radial from the light; directional: linear depth along the light
  axis in world units), and each tap compares the receiver plane's distance
  on the same ray, minus derived bounds - coverage snap, receiver and caster
  normal errors, position, fp32 evaluation (shadows.md "The distance
  technique"). The ray is built `precise` from `world_from_texture` in both
  passes, so it is identical bit for bit. The caster stores the farther of
  its plane's distance and its interpolated point's own distance: with the
  plane alone, a steep face's snapped coverage past a convex crease stored
  its extension, nearer than the neighbouring edge face (`grazing_fan`
  Medium, 85 / 88 degree tiles: G1 2 directional / 1 spot pixel, one
  `pcf_4x4` tap each), a slope no receiver quantity bounds (D10's case for
  the 2D maps). `shadow_distance_bias_coeff` is gone (the control block
  carries `shadow_map_resolution`); the fwidth term is redundant because
  the plane comparison is exact per texel. Medium distance, before / after:
  spot `cornell` G1 56064 -> 0, `grazing_fan` G1 898 / 676 -> 0,
  `contact_blocks` G3 3.25 / 3.5 -> 0.12 / 0.08 texels (the depth
  technique's values), G5 worst 3.25 / 3.75 -> 0.94 / 0.94, spot G2 8 -> 0,
  G1 1 / 15 -> 0, directional G6 17 -> 0, `thin_walls` G2 1 / G1 139 -> 0,
  `spot_cones` G1 1 -> 0; identical at rasterizer slope -1 and 0. G3's
  distance bound is 1.5 texels, as for depth. Medium and High now use
  `shadow_depth_bias_slope` 0: the core matrix at slope 0 fails only the D5
  cells and the D10 point cell, with every other cell's G3 / G5 equal to
  slope -1's, and the pairwise matrix adds three 512 point D10 cells; the
  D10 caster rule removes all four (section 9).
- **D9 Minimum bias under depth clamp.** With
  `Shadow_frustum_fit_settings::depth_clamp` on, near / far clipping is off,
  so a caster's vertex depths are not bounded by 1 and D1's 4u raster term
  does not hold. The raster term is bounded by the clamped primitive's actual
  vertex depth range under depth clamp, and a `depth_range` station view with
  `depth_clamp` on joins the matrix.
  Landed: under depth clamp the rasterizer clips only at x / y, interpolates
  the unclamped vertex depths and clamps the result, so D1's raster term is
  `4u Z` with `Z` the largest `|z_i|` the clamped primitive can have. For a
  directional light (orthographic, affine depth) `Light_projections::apply()`
  takes `Z` as the largest `|depth|` over the caster bounds' corners, at least
  1, once per light (`Light_shadow_limits::raster_vertex_depth`, light block
  `view_origin.w`): a per-light constant, because each directional light has
  its own fit. `Shadow_renderer` gathers the caster bounds whenever
  `depth_clamp` is on. A spot light has no finite bound under depth clamp (a
  caster passing near the light reaches unbounded vertex depths, and a caster
  AABB that contains the light bounds nothing), so depth clamp now applies to
  the directional passes only: `depth_clamp` is a setting of the directional
  fit, pairs with `near_from_main_frustum`, and spot passes clip at their
  0.04 m near plane (`Z = 1`, in the scope of section 2) and their range (a
  caster beyond it was pancaked onto the far clear value, which reads the
  same). The distance technique and the point cube store plane distances;
  depth clamp only changes which of two pancaked casters wins the depth
  test, and either shadows every receiver the fit covers. The committed
  `editor_settings.json` has had `depth_clamp` (and
  `near_from_main_frustum`) on, so every earlier directional measurement
  already ran depth-clamped; `shadow_verify.py` now pins the fit explicitly
  per config (a session-only per-scene override through
  `set_scene_settings`), with `depth_clamp` on, and the core matrix gains a
  `Medium/depth_clamp=false` row (both settings off). `depth_range` gains
  the `under_block` view (camera 3 m up, below the Near Block): the block
  lies between the light and the view frustum, the fit's near plane is below
  it, and directional `Z` reads 2.65 there; every gate passes in both
  configs.

- **D10 Crease neighbour steeper than the receiver.** The coverage-snap
  terms (2D `snap_bias`, point snap term) use the receiver's own slope, but
  within a texel of a crease the map can store the neighbouring face's
  extended plane, which may be steeper and nearer the light (Low / 512 point
  `grazing_fan`: 15 degree tile's top face next to its away-facing side, 0.18
  mm nearer against 0.08 mm of bias, 6 px). The bound covers the steepest
  plane the texel can hold without charging that slope to every receiver
  (the uniform steepest-plane bound costs up to 0.75 texel at 512).
  Landed with D7, in the casters rather than the bound: the 2D distance
  caster and the cube caster store the farther of their plane's distance on
  the texel's ray and their interpolated point's own distance; the point
  lies inside the primitive, on the far side of the neighbour's plane, and
  the farther value never exceeds the reference of the receiver's own
  surface (point_light_shadows.md "Stored distance"). The 2D depth
  technique needs no rule: its stored plane and its coverage both come from
  the snapped vertices. At rasterizer slope 0 without the rule the same six
  point pixels also failed in the 512 pairwise configs `pw01`, `pw08` and
  `pw11`, which passed at Medium's former slope -1 (the slope bias moves the
  steep side face's rasterized depth back, so the top face wins the depth
  test at the crease); with the rule every point cell of the core and
  pairwise matrices passes at slope 0.

- **D11 Spot distance-technique resolution floor.** The spot distance
  path's validity margin assumes about 512 texels across a 90 degree cone; a
  coarser spot map narrows it. The margin's condition is stated as a
  minimum spot map resolution per cone angle and enforced where the preset's
  shadow resolution is clamped, the way D6 enforces
  `c_min_point_shadow_resolution`.
  Landed as a per-light condition instead of a preset clamp (shadows.md "The
  distance technique", validity): the tap rays deviate from the receiver's
  ray by at most `sqrt(2) (r + 1/256) 2 tan(outer / 2) / (N - 2M)`, which has
  to stay below 0.025 (the grazing limit 0.05 less the normal error the
  determined-plane rule admits), so `N >= 2M + 2 sqrt(2) (r + 1/256)
  tan(outer / 2) / 0.025` (`get_spot_distance_min_resolution()`; 90 degrees:
  59 / 117 / 234 / 351 texels for hard / 2x2 / 4x4 / 6x6, 80 degrees 6x6:
  297). The resolution is shared by all 2D shadow lights and the cone is per
  light, and no resolution serves a cone near 180 degrees, so a preset clamp
  cannot make the condition hold for every light, and clamping it on light
  edits would reallocate the map on a cone drag. `Light_projections::apply()`
  evaluates the condition per spot light where it computes the spot
  projection (`Light_shadow_limits::distance_rays_valid`, light block
  `shadow_index_packed.z`), and the distance variant samples a light that
  fails it with the depth technique from the depth map the same caster pass
  writes; `Shadow_renderer` logs each change. Measured on `spot_cones`
  (Medium, distance, short sweep with the 45 / 5 / 45 / 80 / 80 degree
  poses): 256 `pcf_6x6` falls back in the two 80 degree poses (4 of 10
  renders; log "outer cone 80.0 deg with a 3.0 texel filter reach needs a
  297 texel shadow map ..., the map has 256") and passes G1 / G2 / G5
  (0.09 / 1.56 texels); 256 `pcf_4x4`, 512 `pcf_4x4` and 512 `pcf_6x6` keep
  the distance technique and pass (G5 0.44 / 1.06, 0.51 / 1.00, 0.33 / 1.50).
  With the fallback disabled the 256 `pcf_6x6` cell reads the same gates:
  the condition is a sufficient bound, and no receiver of the station sits
  at the grazing limit next to the 80 degree rim.

- **D12 Camera-relative receiver normal.** The receiver geometric normal
  (D2 / D3) comes from screen-space derivatives of the absolute world
  position, whose fp32 error `e = 8.3e-7 |P|` grows with the distance from the
  world origin while the pixel footprint `f` stays small on close views: the
  normal bound `theta_r ~ 2 e / f` reaches the undetermined-plane case at
  about `|P| = 1e4 f` (10 m for a 1 mm footprint at the grazing limit), and
  the gradient term then over-biases - measured: Medium spot `thin_walls` 5 cm
  hut leaks 272 px at 10 m and 1424 px at 20 m from the origin; at 1 km and 10
  km R2 to R5 fail on every station except `head_on_floor` (section 9). The
  receiver position used for the normal and for the gradient bound is
  camera-relative (the vertex stage outputs the position relative to the
  camera, composed on the CPU in double precision), so `e` scales with the
  distance from the camera; the point-light caster normal (D6) is
  light-relative in the same way. R7 is met when the 1 km and 10 km runs pass.
  Landed as relative-to-eye positions (shadows.md "View-relative positions"):
  every pass has a view origin, the fp32 position of its (first) camera (the
  light camera for a 2D shadow pass, the light for a cube face);
  `standard.vert` subtracts it from the node's fp32 world translation
  (`precise`; both are exact fp32 values, so the difference is correctly
  rounded and no hi / lo split is needed) and computes `gl_Position` with
  `clip_from_view_relative` (camera block, composed in double). The light
  block carries each light's view origin and `texture_from_view_relative` /
  `view_relative_from_texture` (composed and inverted in double once per
  `Light_projections::apply()`); the receiver moves its camera-relative
  position into the light's frame by the exact origin offset. The primitive
  records and the draw-list records are unchanged (moving only the camera
  rewrites no record: `transform_update_count` stays 0 over 30 camera moves);
  per frame the cost is one double composition per camera view and per light.
  The rounding bounds take the relative magnitudes, with the caster and
  receiver vertices now rounded in different frames: vertex `5u`,
  interpolated point `9u` (`get_vertex_position_rounding()`,
  `get_position_rounding()`), the frame change `u` each for the offset and
  the sum. Skinned primitives keep the fp32 blend of absolute joint
  matrices. At 10 km the remaining failures traced to the scene geometry, not
  the bias: the fp32 box placement opens 0.16 mm (2 cm hut) and 0.20 mm
  (10 cm hut) slits at wall joins, through which High directional lit a line
  along the floor (G4 65 / 67 px), and 0.2 mm steps at the 5 cm hut's +Z
  wall, whose protruding edge shadowed pcf_4x4 taps within one filter reach
  (Medium G1 9 / 12 px); `shadow_verify.py` now takes its ground truth from
  that fp32 placement (section 6), after which every cell passes (section 9).

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
| `depth_range` | Non-casting floor beyond the fitted far plane, caster near the light; views from above and from below the near caster (`under_block`, D9) | R1, R2 at the clamp paths of shadows.md "Receivers outside the fitted depth range", the depth-clamped raster bound |
| `cube_seams` | Point light in a closed room, casters on cube face boundaries | R1, R2 for the point cube |
| `spot_cones` | Spot aimed at a floor, 5, 45 and 80 degree cones | R1, R5 across projection widths |
| `cornell` | `gi_cornell.glb` at its saved light pose | the section 1 case |

Two measurement variants reuse the stations. R7 (`--root-offset`)
translates the root of `head_on_floor`, `contact_blocks`, `thin_walls` and
`cube_seams`. `--extra-light` puts lights ahead of the station light in the
scene's light order: `unshadowed` a non-shadow directional light, which
takes a slot of the directional bucket, so a spot or point station light's
UBO slot differs from its shadow layer and `sample_light_visibility()` /
`sample_point_light_visibility()` read the layer through
`shadow_index_packed.x` / `.y` (a directional station light keeps slot =
layer = 0: its bucket puts shadow-mapped lights first); `shadowed` adds a
second shadow-casting light of the measured type, so the station light's
layer is 1. Each render's `shadow_lights` slot and layer are checked
against that.

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
- directional fit `depth_clamp` (with `near_from_main_frustum`): on, off (D9;
  the core matrix only, the pairwise and full matrices keep it on)

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
one surface. The boxes are placed as the scene holds them: the scene composes
each box's world translation from the root's and its own fp32 translation in
fp32, which at a root offset moves it by up to half an ulp (0.49 mm at 10 km)
and can open slits or steps at joins; off the origin the hut interiors get an
edge band too, and G4 does not count interior pixels the placed geometry
lights through such a slit. It casts each pixel's segment to the light against the station's
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
- **G2 Occlusion:** `shadowed` pixels with visibility > 0.001 = 0. Shadowed
  pixels inside the contact gap G3 allows belong to G3, not G2: a pixel on
  either box of a touching pair (two station boxes with coincident opposite
  faces - a resting caster on its floor, hut and room walls on the floor and
  under the roof or ceiling, wall-wall joins, blocks under a ceiling), in
  their contact plane, within the G3 bound (1.5 texels) of an edge of their
  contact rectangle, the distance measured in
  shadow texels through the texel Jacobian at the pixel as G3 measures it.
  The edge band covers the analytic shadow boundary, not the contact line,
  so without this G2 would count the gap G3 allows.
- **G3 Contact gap:** distance from a resting caster's footprint edge to the
  receiver's 0.5-visibility crossing <= 1.5 shadow texels, for the depth and
  the distance technique.
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
  docstring); `--enforce` exits non-zero on a FAIL. `--root-offset` and
  `--extra-light` run the section 4 variants. Every config sets the
  directional fit (`depth_clamp` on unless the config turns it off, D9) as a
  session-only per-scene override with the MCP tool `set_scene_settings`. It
  gains the G7 timing in phase 8.
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

- **Phase 8 - backends, cost and documentation.** Build the shadow
  changes on the OpenGL tree and run T7 there (the `precise` ray
  construction of D7, D12's `precise` view-relative translation and every
  shader change of phases 2 to 7 are verified on
  Vulkan only), and on Metal where a macOS machine is available. G7; rewrite shadows.md "Shadow
  sampling" and "Bias technique" for the landed design, add the verify recipe
  to `doc/testing.md`, run the final gate (Low, Medium, High,
  Medium/shadow_technique=distance and Medium/depth_clamp=false, short
  sweep, about 15 min - the pairwise matrix last ran at D12 and the core
  matrix at D11), and delete this plan's
  finished items.

## 9. Gate table

`py -3 scripts/shadow_verify.py --matrix core --save-images failing` on the
current code with the committed presets (rasterizer bias 0 in every preset):
16 configs (Low, Medium, High and the one-axis variations around Medium; a
requested `shadow_depth_bits` of 24 resolves to D32_SFLOAT on this device,
axis 32), `--poses short`, `--runs 1`, failing cells re-run 3 times (every
listed cell failed all 3 re-runs); 6367 renders, 38.3 min wall on the Debug
headless Vulkan editor, AMD iGPU (the point cells re-measured after the D10
caster rule: 1771 renders, 10.9 min). Directional and spot pass every gate
in every `cull_back` and `cull_none` cell of both techniques, including
forward-Z, 512 and 2048; point passes every gate in every cell. Cells not
listed pass every gate that applies to them. Values are the worst over
poses and views: failing pixel count and share of the gated pixels (G1,
G2), texels (G3, G5 as mean / worst), pixels (G4 per wall, G6).

| Config | Failing cells |
|---|---|
| Medium/shadow_cull_mode=cull_front (inherent to storing back faces, D5; not the default) | G4 on every `thin_walls` hut (directional 1 cm: 8, 2 cm: 9 .. 20 cm: 4, spot 1 cm: 91, 2 cm: 39 .. 20 cm: 428); `contact_blocks` G2 1070 (0.22 %) / 4441 (0.9 %), G5 worst 8 (the spurious crossing at the contact line), directional G6 4; `cube_seams` G2 1590 (1.1 %) / 15 (0.17 %) |

`contact_blocks` G3 (directional / spot, texels): Low 0.05 / 0.13, Medium
0.13 / 0.08, High 0.18 / 0.24, 512 0.65 / 0.75; point Low 1.0, Medium 0.03,
High 0.06. Re-measured on the D12 code (6322 renders, 35.2 min): the same
failing cells, and the same G3 but Medium directional 0.12 -> 0.13 and High
spot 0.23 -> 0.24 (the larger rounding bounds of the separately rounded
caster and receiver vertices).
Re-measured on the D9 / D11 code with the directional fit pinned per config
(17 configs, the new `Medium/depth_clamp=false` row and the `depth_range`
`under_block` view; 6937 renders, 38.8 min): the same failing cells with the
same values, every cell of `Medium/depth_clamp=false` passes (G3 0.13 / 0.08
/ 0.03, as Medium), and the G3 values above hold (forward-Z 0.12 / 0.08).
The directional raster vertex depth bound under depth clamp reads 1 on
`head_on_floor` and `grazing_fan`, 1.06 to 1.41 on `spot_cones`, `cornell`,
`cube_seams` and `thin_walls`, 2.65 on `depth_range` and 10.2 on
`contact_blocks` (reverse-Z; forward-Z 1 to 9.2); no core cell falls back
from the distance technique (the committed presets' resolutions exceed
every station cone's minimum).

The distance technique on every core axis (`--matrix core --set
shadow_technique=distance --light directional,spot`, 4597 renders, 29.1
min) fails only the `cull_front` cells above (G2 1054 / 4319, G4 up to
408 spot, `cube_seams` G2 1583 / 15); `contact_blocks` G3 matches the
depth technique's (Low 0.05 / 0.04, Medium 0.12 / 0.08, High 0.18 / 0.23,
512 0.65 / 0.75, forward-Z 0.13 / 0.08).

`py -3 scripts/shadow_verify.py --matrix pairwise` on the current code: 17
configs, 7372 renders, 45.3 min wall (point cells re-measured after the D10
caster rule: 1426 renders, 8.9 min); 18 failing cells, all in the
`cull_front` configs (D5): `pw01`, `pw10`, `pw07` (depth) and `pw06`
(distance, forward-Z). Every other cell of both techniques passes,
including every point cell and directional G6 in every non-`cull_front`
cell. Re-run on the D12 code (7192 renders): the same 18 failing cells.
`--extra-light unshadowed shadowed` on Medium on the D12 code (769 renders):
every cell passes, G3 as the plain placement.

`--extra-light unshadowed shadowed` on Low, Medium and High, every station
and light type (144 cells, 2253 renders, 14.4 min): every cell passes every
gate, with the plain placement's G3 and G5 values. The measured light's
slot / layer: `unshadowed` directional 0 / 0, spot 1 / 0, point 1 / cube 0;
`shadowed` directional 1 / 1, spot 2 / 1, point 2 / cube 1.

R7, `--config Low,Medium,High,Medium/shadow_technique=distance --station
head_on_floor,contact_blocks,thin_walls,cube_seams --root-offset 10,0,10
50,0,50 1000,0,1000 10000,0,10000`, short sweep, on the D12 code (3887
renders, 26.3 min): every cell passes every gate at every offset, all three
light types. `contact_blocks` G3 (texels, directional / spot / point): at
10 m and 50 m Low 0.05 / 0.13 / 1.0, Medium 0.12 / 0.08 / 0.03, High 0.17 /
0.23 (0.24 at 50 m) / 0.06; at 1 km Medium 0.15 / 0.09 / 0.03, High 0.24 /
0.23 / 0.06; at 10 km Low 0.17 / 0.20 / 0.25, Medium 0.17 / 0.23 / 0.20, High
0.19 / 0.24 / 0.23 (distance technique as depth). At 10 km G4 does not
count the light the placed geometry lets through its slits (section 6).
