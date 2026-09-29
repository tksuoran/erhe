# Shadow Mapping

Stability: stable

All three light types cast shadows. Directional and spot lights use 2D depth
shadow maps (a depth `texture_2d_array`); point lights use an omnidirectional
cube-map shadow on a separate path. This document covers the directional light
shadow frustum fitting (the stable baseline fit and the modular tight fit), the
shadow pass mechanics that support it, the shadow sampling shader, the
point-light cube-map path, and the editor integration (settings, debug
visualizations, fit target camera override). The fit's cost model and the
optimizations that shape it are in
[`shadow_tight_fit.md`](shadow_tight_fit.md).

For the wider rendering architecture (rendergraph, composer, forward pass,
winding variants) see [`editor_rendering.md`](../editor/rendering.md); its
"Shadow rendering" section is the high level summary of the shadow pass
itself.

## Data flow

Per frame, for each scene view:

1. `Shadow_render_node::execute_rendergraph_node()` (editor) refreshes
   `Shadow_frustum_fit_settings` from the live editor settings, resolves the
   fit target camera (the view camera, or the per-scene-view override), and
   calls `Shadow_renderer::render()`.
2. `Shadow_renderer::render()` (erhe::scene_renderer) gathers one world-space
   AABB per shadow caster when `fit_to_casters` is enabled, then calls
   `Light_projections::apply()`, which invokes
   `Light::projection_transforms()` per light to compute the light camera
   pose, projection, `clip_from_world` and `texture_from_world`. The per-light
   filtering of those casters happens inside the fit (see fit_to_casters),
   because which casters can contribute depends on the light direction.
3. The shadow pass rasterizes each shadow casting light into its own layer of
   a depth texture array, using the FITTED pose and projection. This is a hard
   requirement: `light_buffer.cpp` writes `texture_from_world` from the same
   fitted `clip_from_world`, and the shading pass samples shadows through that
   matrix. Rasterizing from any other pose would make every shadow lookup
   miss.
4. The forward pass samples the shadow map in
   `sample_light_visibility()` (`res/shaders/erhe_light.glsl`) using
   `texture_from_world` from the light UBO/SSBO.

Steps 1-4 above describe the **2D depth array** path used by directional and
spot lights. Spot lights use a single perspective shadow map built directly
from the light node pose (`Light::spot_light_projection_transforms()`); its
frustum is the outer cone widened so the cone maps inside the coverage margin
(see [Empty border and receiver coverage](#empty-border-and-receiver-coverage)).
**Point lights take a parallel cube-map path** (`Light::point_light_projection_transforms()`
+ the `Shadow_renderer` point-cube pass): six perspective faces storing radial
distance, sampled by direction, never touching the 2D array, the fit, or
`texture_from_world`. See [Point-light cube-map shadows](#point-light-cube-map-shadows)
below and [`point_light_shadows.md`](point_light_shadows.md). All of the frustum
fitting below applies to directional lights only; spot and point use fixed
light-pose projections.

## Directional light frustum fitting

### Light space conventions

The fit works in a light-aligned frame: x / y span the light plane
(perpendicular to the light direction), and `s = dot(p_in_light,
light_direction)` increases TOWARD the light (the light direction is the
light node +Z, pointing from the scene toward the light). The fitted box's
`s_max` face (light-most) becomes the near plane of the orthographic light
projection (`orthographic_z_near = 0`); the `s_min` face becomes the far plane
(`orthographic_z_far = s_max - s_min`). The light camera sits centered on the `s_max`
face, looking along the negative light direction.

### Stable fit (baseline)

`Light::stable_directional_light_projection_transforms()` (`light.cpp`):

- A cube of half-extent `r = Camera::get_shadow_range()` (default 22 m)
  centered on the view camera position; orthographic projection `2r` deep.
  Laterally the `2r` square maps to the map minus the coverage margin and one
  texel of snap slack on each side, so the projection is
  `2r * N / (N - 2 * (margin + 1))` wide for an `N` texel map (slack 0 with
  `texel_snap` off).
- The view camera position is snapped to shadow map texel increments in the
  light plane, so camera translation does not make shadow edges shimmer
  (`texel_snap` also controls this path). The snap moves the centre by less
  than one texel either way, which the snap slack absorbs.
- Content-agnostic: casters and receivers are ignored, so much of the map
  area and depth range is typically wasted, and texels are large.

This path is used when no tightening step is enabled, and as the fallback
whenever the tight fit cannot produce a usable box.

### Tight modular fit

`Light::tight_directional_light_projection_transforms()`
(`light_frustum_fit.cpp`) runs when `fit_to_view_frustum` or
`fit_to_casters` is enabled. It is a pipeline of independent steps, each
gated by a `Shadow_frustum_fit_settings` field:

| Setting | Default | Effect |
|---|---|---|
| `fit_to_view_frustum` | off | Constrain the light space box to the main camera view frustum corner box |
| `fit_to_casters` | off | Fit to the surviving caster bounds, each clipped to the shadow caster volume F_shadow |
| `fit_to_receivers` | off | Cull casters against the receiver volume (view frustum intersected with receiver bounds) extruded toward the light; refines `fit_to_casters` |
| `fit_to_receivers_hull` | off | Use the tight convex receiver hull (clipped to the frustum) instead of a bounding box for the receiver cull volume |
| `optimize_rotation` | off | Rotating calipers roll around the light direction for minimum covered area |
| `near_from_main_frustum` | off | Pull the near plane down to the view frustum extent (needs `depth_clamp`) |
| `depth_clamp` | off | Depth-clamp rasterization in the shadow pass (caster "pancaking") |
| `texel_snap` | on | Snap the light space box to shadow map texels |
| `quantize_extents` | off | Round the box size up to multiples of `quantize_step` |
| `quantize_step` | 0 | World units; 0 derives `2 * shadow_range / 16` |
| `cap_by_shadow_range` | on | Never exceed the stable shadow-range box extents |
| `collect_debug` | off | Record per-step intermediates for the debug visualization; off = the fit does no debug-collection work at all (use for profiling) |

Pipeline order, with the box recorded per step for debugging
(`Shadow_fit_step`): fit points -> frustum constraint -> shadow range cap ->
stabilization.

**fit_to_casters.** `Shadow_renderer::render()` gathers one world-space AABB
per visible shadow-casting mesh. The fit then, per light:

1. Builds the shadow caster volume F_shadow: the main camera view frustum,
   *truncated to the maximum shadow distance* (`Camera::get_shadow_range()`),
   extruded toward the light. Two plane sets are derived from it because the
   two consumers (cull vs box clip) have different needs - reusing one set for
   both is what made the volume collapse to a single plane:
   - `build_shadow_caster_volume_planes` keeps the F_main planes whose inward
     normals do not face away from the light; planes facing the light are
     dropped, leaving an **open** volume (no lateral closure). This collapses
     to a single plane when the light is near-antiparallel to the view (e.g. a
     top-down camera under a straight-up light), since only the far face then
     survives. Used only to clip the surviving caster boxes.
   - `build_shadow_caster_silhouette` adds the **silhouette side planes**: for
     each frustum edge between a kept and a dropped face, the plane through
     that edge swept toward the light. The kept planes plus the silhouette
     planes are the *exact* bounding planes of the extruded frustum - the
     `filter_planes` set used for per-caster culling.
   Truncating the far plane is safe: a caster whose shadow only lands beyond
   the shadow range produces no visible shadow (the fit is capped to that
   range and the map does not cover it), and a caster past the range whose
   shadow falls *back* toward the camera is still kept, because the far cap is
   dropped exactly when its inward normal opposes the light direction.
2. Culls every caster AABB that lies fully outside the `filter_planes` volume
   (`aabb_in_convex_volume`). This is a conservative half-space rejection: it
   never drops a caster that could shadow a visible receiver (no false
   negative). The silhouette side planes are what make this effective - with
   the open set alone almost nothing is culled.
3. Clips each surviving AABB to the **open** F_shadow individually
   (`clip_convex_hull_points_by_planes` over the box's fixed 12-triangle
   topology, Sutherland-Hodgman per triangle) - trimming the parts of
   straddling casters that stick out below the volume - and fits to the union
   of the per-box clipped point sets. Each box is bounded laterally by
   itself, so the open set (not `filter_planes`) is used here. Boxes entirely
   inside the volume skip the clip and contribute their corners directly.
   Because clipping only produces points on the box surface, F_main corners
   that lie inside a box are added back (a single large caster enclosing the
   whole view frustum would otherwise contribute no points at all). No global
   hull is built: the union of per-box clips is a subset of the previously
   used clipped whole-set hull - which also covered the empty bridge regions
   between separated casters - so the fit is equal or tighter at the same
   coverage. The caster hull survives only as a debug visualization, built
   when `collect_debug` is on.

If no caster survives the cull (or the clip produces no points) and
`fit_to_view_frustum` is off, the fit falls back to the stable path.

**fit_to_receivers.** Refines the caster cull (step 2 above) with the actual
receiver geometry. `Shadow_renderer::render()` additionally gathers one
world-space AABB per visible receiver (every visible content mesh; casters are a
subset). The fit builds a second caster-cull volume from the receiver bounds that
touch the view frustum, extruded toward the light, and requires each caster to
pass it *in addition to* F_shadow. A caster whose light-space footprint lies
outside the receiver region shadows only empty space, so it is rejected; this
shrinks the surviving caster set, which in turn tightens the fit (the lateral
extent follows the smaller caster footprint) and cuts shadow-pass draw work. The
volume is built conservatively (it always contains every visible receiver), so
the cull never drops a needed caster, and it is AND-ed with F_shadow so it can
only ever reject more casters than F_shadow alone. With `fit_to_receivers_hull`
the receiver body is the convex hull of the receiver corners intersected with the
view frustum (the complete convex intersection - both surfaces clipped against each
other - via `clip_convex_hull_points_to_frustum`, so near receivers are kept even
when the frustum apex is inside the hull) and extruded via
`build_shadow_caster_cull_planes_from_hull`: the clipped
hull points are projected onto a plane perpendicular to the light, their 2D convex
hull is the receiver silhouette as seen along the light, each silhouette edge swept
along the light becomes a lateral side plane, and a single flat far cap (through the
hull point furthest from the light) closes the away-from-light side - leaving the
volume open toward the light. This follows the receiver silhouette far more tightly
than the default light-space bounding box. The extra rejections over the
frustum-only cull are off-frustum casters whose shadows miss all receivers - largest
when receivers are clustered, small when a ground plane fills the frustum. Has no
effect without `fit_to_casters`; both `fit_to_receivers` and `fit_to_receivers_hull`
default on.

**fit_to_view_frustum.** Intersects the box laterally (and from below) with
the view frustum corner box: receivers only exist inside the view frustum,
so the fit never needs to extend beyond the caster / frustum intersection in
those directions. Without casters this becomes the primary fit point set.

**optimize_rotation.** Projects the fit points onto the light plane, builds
their 2D convex hull, and runs rotating calipers
(`calculate_min_area_obb_2d`) to find the hull edge alignment minimizing the
bounding rectangle area. The light camera is rolled around the light
direction accordingly. This trades a little rotational stability for fewer
wasted texels; `texel_snap` still suppresses translation shimmer in the
rolled frame.

**near_from_main_frustum + depth_clamp.** The near plane only needs to reach
the view frustum: casters between the near plane and the light are preserved
by depth clamping in the shadow pass (classic "pancaking" - their depth
clamps to the near plane instead of being clipped away). The two settings
pair: enabling `near_from_main_frustum` without `depth_clamp` loses shadows
from casters above the near plane.

**cap_by_shadow_range.** Safety net: the box never exceeds the stable fit's
shadow-range cube around the view camera, so a stray huge caster cannot
explode the fit. If the constraints leave a disjoint (inverted) box, the fit
falls back to the stable path rather than producing a degenerate projection.

**Stabilization.** Tight fits change every frame, which makes shadow edges
crawl. Two mechanisms reduce that:

- `quantize_extents` rounds the box SIZE up to discrete steps so it stays
  constant while the content moves only a little.
- The box maps to the map minus the coverage margin on each side (see
  [Empty border and receiver coverage](#empty-border-and-receiver-coverage)):
  the texel size is `box_size / (N - 2 * margin - 1)` with `texel_snap` and
  `box_size / (N - 2 * margin)` without, and the box min corner moves out by
  the margin.
- `texel_snap` snaps that min corner down onto the texel grid; the extra
  texel of the covered size absorbs the snap, so the fitted box stays inside
  the margin at both edges. The snap is only effective while the box size
  (and thus the texel grid) stays constant, which is what `quantize_extents`
  provides.

A minimum box extent (1 cm) keeps degenerate (flat) fits renderable.

## Shadow pass mechanics

`Shadow_renderer` (`erhe_scene_renderer/shadow_renderer.cpp`):

- **Caster bounds gathering** - when `fit_to_casters` is on, one world-space
  AABB per mesh passing the shadow filter (visible + shadow_cast) is collected
  before `Light_projections::apply()` and copied into frame-lifetime storage
  inside `Light_projections`. The per-light contribution cull and the
  per-box clipping happen inside the fit, not here (the gather is
  light-independent; the cull is not).
- **Cull mode** - the active graphics preset's `Shadow_cull_mode` selects the
  caster pipeline. `cull_back` (the default, and every shipped preset) writes
  only front faces: a lit receiver's own face is in the map, a tie the
  receiver's minimum bias resolves ("Minimum bias"), and single-sided geometry
  casts from the side facing the light. `cull_front` writes only back faces,
  which leaks light wherever a caster touches a receiver: the caster's back
  face meets the receiver at the contact line (a wall's far face at a
  wall-floor or wall-wall join) or is coplanar with it (the bottom face of a
  box resting on a floor), so a filter tap whose texel-centre ray lands on the
  receiver plane there, or inside the touching box, reads a tie or a stored
  surface behind the reference, and reads lit. Within the filter reach plus
  the depth gap the bias covers, a receiver inside a closed hut or behind a
  resting caster is partly lit (G2 / G4 of
  [plans/shadow_robustness.md](../plans/shadow_robustness.md)). This is
  inherent to storing back faces, not a bias defect; closing it needs a second
  depth layer (midpoint or second-depth maps). A negative rasterizer slope
  bias (`shadow_depth_bias_slope`, 0 in every committed preset) moves the stored back
  faces farther from the light and widens the leak. `cull_none` writes both
  sides; on closed meshes it stores the same nearest (front) surface as
  `cull_back`, rasterizing twice the faces.
  `Shadow_renderer` pre-builds one pipeline per cull mode (`m_pipelines[]`).
  Mirrored (negative-determinant) buckets use the front-face-flipped pipeline
  variant; see "Mirrored (negative-determinant) geometry" in
  `editor_rendering.md`.
- **Depth clamp pipeline** - `Shadow_frustum_fit_settings::depth_clamp`
  selects the depth-clamp sibling of the active cull mode
  (`m_pipelines_depth_clamp[]`), e.g.
  `Rasterization_state::cull_mode_front_ccw_depth_clamp` for `cull_front`: the
  same culling and y-flip winding compensation as the regular shadow pipeline,
  plus depth clamping. The sibling exists so that toggling `depth_clamp`
  changes ONLY depth clamping, never the culling behavior (and so the
  per-bucket winding flip stays effective).
- **Empty border** - the pass sets a scissor rectangle inset by the border
  width on each side, keeping the outermost texel rings at the clear value
  (see [Empty border and receiver coverage](#empty-border-and-receiver-coverage)).
- **Prewarm** - `prewarm_pipelines()` warms the active cull mode's two base
  pipelines (regular and depth clamp) times both winding variants, so neither
  toggling `depth_clamp` nor mirroring a mesh hitches at first draw. Changing
  the cull mode is a rare settings action and compiles the other modes'
  pipelines once, on demand.

### Empty border and receiver coverage

The receiver filter reads, per lookup, the texels whose centres lie within
its tap reach of the sample point (L-inf, in texels): hard 0.5, `pcf_2x2` 1,
`pcf_4x4` 2, `pcf_6x6` 3, for the depth and the distance technique alike.
`erhe::scene::Shadow_map_footprint` carries the reach of the active filter
(`Shadow_render_node` derives it from the preset's `shadow_filter` through
`Shadow_map_footprint::from_kernel_width()`) into `Shadow_renderer` and
`Light_projection_parameters`, and two widths follow from it:

- **Border width** `B = max(1, ceil(reach))` (1, 1, 2, 3 texels). The 2D pass
  scissor keeps the outermost `B` texel rings at the far clear value. A lookup
  whose sample point lies outside the map reads texel centres less than
  `reach` inside it, which are all border texels, and with the samplers'
  `clamp_to_edge` address mode it resolves fully lit. Out-of-XY receivers need
  no range check in the shader.
- **Coverage margin** `M = B + reach - 0.5` (1, 1.5, 3.5, 5.5 texels). Every
  receiver the projection is meant to cover maps at least `M` texels inside
  the map, so every tap of its lookup reads a texel inside the border: the
  directional fit (stable and tight) maps its fitted region, and the spot
  projection its outer cone, to `[M, N - M]` in texels.

The directional stable fit and tight fit size and snap their light-space
box for the margin (see "Stable fit" and "Stabilization" above). The spot
frustum is the outer cone (full angle `outer_spot_angle`) widened to
`2 atan(tan(outer_spot_angle / 2) * N / (N - 2M))`, which puts the cone's
inscribed circle `M` texels inside the map edges; receivers outside the cone
receive no light from it. The point-light cube has no border: each face is
rasterized full-face, the lookup is a single compare along the direction, and
cube sampling continues across faces.

## View-relative positions

Every position the shadow casters and receivers compute with is relative to
a view origin near what the pass sees, never an absolute fp32 world position,
so each fp32 rounding bound of the bias ("Minimum bias", "The distance
technique", [`point_light_shadows.md`](point_light_shadows.md) "Receiver
bias") scales with the distance from the camera or from the light camera, not
with the distance from the world origin.

- **View origin of a pass.** `get_view_origin(world_from_camera)`
  (`camera_buffer.hpp`) is the position of the pass's (first) camera, taken
  from the fp32 matrix as is, so it is exactly representable: the view camera
  for a forward pass, the light camera `world_from_light_camera` for a 2D
  shadow pass, the light position for a cube face pass (`create_look_at()`
  puts the eye in column 3). A multiview pass shares its first view's origin.
- **Camera block.** Per view, written with the camera block (constant cost
  per pass): `view_origin` and `clip_from_view_relative`
  (`get_clip_from_view_relative()`): `clip_from_camera *
  inverse(world_from_camera) * translate(view_origin)`, composed and inverted
  in double from the fp32 camera matrices, so the translation cancels in
  double.
- **Vertex stage (relative to eye).** The primitive records are unchanged:
  `world_from_node` is the node's fp32 world transform, written when the
  node's transform changes (the draw-list records) or with the bucket's
  primitive range, never per camera. `standard.vert` subtracts the view
  origin from its translation, `precise`: both are exact fp32 values, so the
  one subtraction is correctly rounded - the value a double subtraction
  rounded once gives, so no low-order split of either is needed - and small
  near the view origin. With the upper 3x3 of `world_from_node` it forms
  `view_relative_from_node`; `v_view_relative_position` (location 17) and
  `gl_Position = clip_from_view_relative * v_view_relative_position` come
  from it (also `precise`, so the compiler cannot fold the subtraction into
  the sum). The vertex's fp32 error is then `5u` times its distance from the
  view origin (a four-term row sum plus the rounded translation).
  `v_position` stays the absolute world position for lighting, DDGI, texgen
  and `Shader_debug::world_position`.
- **Light block.** Per light, composed once per frame by
  `Light_projections::apply()` (`light_view_relative_transforms`, parallel to
  the slots) and copied into the light block per pass: `view_origin` (the light camera position,
  `world_from_light_camera[3]`; the light position for spot and point
  lights), `texture_from_view_relative` (`get_texture_from_clip()` times
  `get_clip_from_view_relative()` of the light camera, in double) and its
  double inverse `view_relative_from_texture`. The shadow pass camera block is
  composed from the same fp32 inputs with the same function, so the caster
  pass and the receiver use the same rows. `texture_from_world` and
  `world_from_texture` remain for the other consumers (the shadow texel debug
  view, the editor's shadow debug lines).
- **Receiver.** `get_light_relative_position()` (`erhe_light.glsl`) moves the
  receiver's view-relative position into a light's view-relative space by
  adding the offset between the two exact origins, `precise`, so the offset
  is formed first; `get_light_relative_receiver_rounding()` bounds the result
  (the interpolated point, `9u` times its distance from the camera, plus one
  rounding each of the offset and the sum). The receiver normal
  (`get_receiver_geometric_normal()`) takes the derivatives of
  `v_view_relative_position`.
- **Casters.** The 2D distance caster and the cube caster work on their
  fragment's `v_view_relative_position`, relative to the light camera; the
  cube caster's position is its offset from the light directly.

Subtracting the view origin from an fp32 world position after the vertex
transform would not remove the error: the world position already carries
`u |P|` of rounding. The subtraction happens on the translation, before the
vertex transform adds anything to it. Skinned primitives are the exception:
their joint matrices (`world_from_bind`, written when the joints move) are
absolute and blended per vertex in fp32, so their blended translation
carries the blend's rounding of the absolute joint translations before the
view origin is subtracted; making it view-relative would need the joint
translations relative to each pass's view origin. Skinned casters are outside the requirements of
[`plans/shadow_robustness.md`](../plans/shadow_robustness.md).

What stays absolute is the scene itself: a node's world transform is
composed in fp32, so far from the origin each node lands within half an ulp
of its world coordinate (0.49 mm at 10 km) of where its parent chain puts it,
independently of its neighbours. That is the geometry both passes see; at
10 km it opens 0.16 to 0.27 mm slits and 0.2 mm steps at some joins of the
`thin_walls` huts, which the shadow maps reproduce as light through the slit
and a step's shadow (`shadow_verify.py` takes its ground truth from the same
fp32 placement).

## Shadow sampling

`sample_light_visibility()` in `res/shaders/erhe_light.glsl`:

- The fragment's view-relative position, moved into the light's view-relative
  space ("View-relative positions"), is transformed by the light's
  `texture_from_view_relative` into [0,1] texture space plus depth.
- The comparison sampler (`s_shadow_compare`) bakes a NON-STRICT comparison
  at engine init from the reverse-depth convention: `greater_or_equal` for
  reverse-Z, `less_or_equal` for forward-Z (`light_buffer.cpp`). 1.0 = lit.
- A slope bias is derived from the receiver's light-space depth gradient
  dz/dUV, taken from the receiver plane (see "Receiver depth gradient"
  below), based on
  https://renderdiagrams.org/2024/12/18/shadowmap-bias/ . For a fixed-point
  (UNORM) shadow map the reference depth is then rounded direction-aware to the
  format's depth precision (toward the near plane) before the hardware
  comparison; a floating-point (D32_SFLOAT) map skips the snap. The format is
  carried by the `ERHE_SHADOW_DEPTH_BITS` compile-time variant axis, whose
  value is `get_shadow_depth_bits_axis()` (`shader_key.hpp`) of the shadow map
  texture's actual format: 16 or 24 for a UNORM map, 32 for a float map
  (D32_SFLOAT, D32_SFLOAT_S8_UINT), 0 for no shadow map. The bit count alone
  identifies the encoding, because every float depth format has 32 bits and
  there is no 32-bit UNORM depth format. The preset's `shadow_depth_bits` is
  only a request: `choose_shadow_depth_format()` (editor
  `shadow_render_node.hpp`) resolves it to the nearest supported format,
  preferring more bits (24 resolves to D32_SFLOAT on a device without a 24-bit
  format), `Shadow_render_node::reconfigure()` creates the map in that format
  and logs `requested depth bits -> format`, and the composition pass reads the
  axis from the view's shadow map texture; the init-time prewarm predicts it
  with the same `choose_shadow_depth_format()`.
- The filter is a compile-time variant (`ERHE_SHADOW_FILTER`, set from the
  graphics preset's `Shadow_filter_mode`): `hard` does a single hardware
  comparison-sampler fetch against the snapped reference; `pcf_2x2` does one
  `textureGather` on the non-comparison sampler with bilinear weighting; the
  wide `pcf_4x4` / `pcf_6x6` paths do `(K/2)^2` gathers and average. The gather
  paths use the same non-strict `gequal` / `lequal` semantics as the hardware
  sampler, so all three variants agree.
- With the `distance` shadow technique (`ERHE_SHADOW_TECHNIQUE`, from the
  preset's `shadow_technique`), the same filters read the R32F distance map
  `s_shadow_distance` and compare light distances on the texel rays instead of
  depths (see "The distance technique" below).

### Shadow visibility debug view

`Shader_debug::shadow_visibility` (30) replaces the fragment color with the
visibility of one light (1 = lit, 0 = shadowed), sampled regardless of N.L.
The light is `light_block.shadow_debug_light_index`, an index into
`light_block.lights` (the bucketed slot order: directional, spot, point, each
shadow-mapped first; `Light_projection_transforms::index`), carried from
`Base_render_parameters::shadow_debug_light_index` through
`Light_buffer::update()` into the std140 padding after
`brdf_phi_incident_phi`. `standard.frag` finds the slot's bucket from the
`ERHE_LIGHT_COUNT_*` counts and calls `sample_light_visibility()` for a
shadow-mapped directional or spot light and `sample_point_light_visibility()`
(cube layer `shadow_index_packed.y`) for a shadow-mapped point light; any
other slot reads 1. Viewports use slot 0; `render_scene_image` takes the light
by name or id (`doc/editor/rendergraph.md` "Scene image capture").

### Receivers outside the fitted depth range

The sampler clamps the comparison reference depth to [0, 1] after biasing and
takes no early-out on the light texture depth. **Never add one**: with
reverse-Z, light texture depth 0.0 is the far plane, so an
`if (position_in_light_texture.z <= 0.0) return 1.0;` forces any receiver
beyond the light-space far plane to fully lit before the depth comparison runs.
With `fit_to_casters` the far plane hugs the caster bounds, so every visible
receiver below the lowest caster (the classic floor under floating objects)
would take that branch and lose its shadow.

The clamp is geometrically correct, not a workaround:

- **Beyond the far plane**: the reference clamps to the far value. Every
  caster in the map is nearer the light than the far plane, and the F_shadow
  clip guarantees that every occluder of a visible receiver IS in the map.
  So caster texels compare shadowed (correct - the caster is between the
  light and the receiver), and empty texels compare lit because the clamped
  reference equals the far clear value and the comparison is non-strict
  (`gequal` / `lequal`).
- **Nearer than the near plane**: the reference clamps to the near value and
  compares lit - nothing in the map is nearer than the near plane (casters
  pancaked exactly onto it compare lit by the non-strict comparison, which
  is the standard resolution of that inherent ambiguity).
- The explicit clamp in the shader is required because GL and Vulkan clamp
  the comparison reference to [0, 1] only for unorm depth formats, not for
  float ones; the shadow map format is chosen at runtime from the supported
  depth formats.
- Out-of-range XY needs no check: the empty scissor border plus
  `clamp_to_edge` (see "Empty border and receiver coverage") resolves those
  lookups to lit.

This also means the depth range of the fit is purely a precision budget for
casters: the far plane does not need to be extended to cover receivers, which
would waste depth precision and was never the design intent (the near side
has the same property via `near_from_main_frustum` + `depth_clamp`).

### Shadow sampling GPU tests

`erhe_scene_renderer_gpu_tests` (ctest label `gpu`) checks the directional
and spot sampling paths without the editor. The fixture
(`src/erhe/scene_renderer/test/shadow_gpu_test_fixture.hpp`) sets up
`Mesh_memory`, `Program_interface` (shader paths `res/shaders` and the test
directory's `shaders/`), `Shader_variant_cache`, `Material_set`,
`Scene_pass_resources`, `Forward_renderer`, `Shadow_renderer`, `Light_set` and
`Light_projections`, and builds a station of boxes: a floor whose top face is
the plane y = 0, optional caster boxes, one shadow-casting light straight
above the origin, and a top-down orthographic view of x, z in [-1, 1] whose
depth range shows the plane only. A `Shadow_pose` places the whole station
in the world by a rigid frame; the first pose is the identity (every matrix
exact), the others rotate and translate it so the matrices carry the
last-bit rounding of a real scene. It renders the shadow map through
`Shadow_renderer`, the forward pass with `Shader_debug::shadow_visibility`
through `Forward_renderer`, and the Shadow_tie pass: `shaders/shadow_tie.frag`
through `Forward_renderer::draw_primitives()`, a fragment pass that evaluates
`sample_light_visibility()` at receiver points on the plane with the reference
depth offset by -4 to +4 float ulps, one vertical band per offset. The offset
enters through `ERHE_SHADOW_TEST_REFERENCE_DEPTH_ULPS`, a macro only the test
shader defines; `sample_light_visibility()` adds that many ulps to the light
texture depth before any bias, and production shaders compile it out.

The cases (`test_shadow_gpu.cpp`), each per light type (spot, directional)
and over every pose, depth format the device offers (16, 24, 32 depth bits)
and filter (`hard`, `pcf_2x2`, `pcf_4x4` and `pcf_6x6` with each
`Shadow_bias_mode`), with `cull_back`:

- `shadow_tie_head_on_plane_reads_lit` and `shadow_head_on_plane_reads_lit`
  require visibility 1 on the head-on plane for every offset / pixel with no
  rasterizer bias (the head-on tie of
  [plans/shadow_robustness.md](../plans/shadow_robustness.md) section 1):
  the minimum bias ("Minimum bias" below) alone separates the tie, with
  4 ulps of reference offset to spare in either direction.
- `shadow_tie_exact_pose_with_rasterizer_bias_reads_lit` and
  `shadow_head_on_plane_exact_pose_with_rasterizer_bias_reads_lit`: at the
  identity pose a rasterizer constant bias of -4 makes the plane read 1.
- `shadow_caster_box_occludes_plane`: a box above the plane reads 0 at least
  5 cm inside its analytic shadow at every pose, and at the identity pose 1
  at least 10 cm outside it.

## Bias technique: RPDB reference, and the distance technique

erhe's receiver-side bias is the receiver-plane depth bias (RPDB) method from
https://renderdiagrams.org/2024/12/18/shadowmap-bias/ (cited in
`sample_light_visibility()`, `res/shaders/erhe_light.glsl`). This section records how the implementation
maps onto that reference, where it goes beyond it, and the `distance` shadow
technique, which compares plane distances on the texel rays instead. The known
deltas from the reference are listed in
[`plans/shadow_robustness.md`](../plans/shadow_robustness.md).

### What erhe implements (RPDB)

`sample_light_visibility()` derives the receiver's light-space depth gradient
`dz/dUV` from the receiver plane, then offsets the comparison reference of
every tap to the depth the receiver plane has at that tap's texel centre.

#### Receiver depth gradient

The caller passes the receiver's unit geometric normal `N`:
`get_receiver_geometric_normal()` (`erhe_light.glsl`, fragment stage only)
returns `normalize(cross(dFdxFine(p), dFdyFine(p)))` of the view-relative
position `p` ("View-relative positions"), which lies in the triangle's plane
for a planar triangle and ignores smooth vertex normals and normal maps. `standard.frag` takes it once, in
uniform control flow ahead of the per-light branches, and passes it to every
shadow-mapped directional and spot light and to the mode 30 debug view.

With `P` the receiver point (relative to the light's view origin, as are
all light-space quantities below; directions are the same in every
translated frame), the plane is the row vector
`pi = (N, -dot(N, P))`, which is 0 on homogeneous points `(x, 1)` of the
plane. With `W = view_relative_from_texture` (the inverse of
`texture_from_view_relative`, both in the light block), a homogeneous texture
point `h` maps to the point `W h`, so the plane in homogeneous texture coordinates is
`pi' = transpose(W) * pi = (a, b, c, e)`: `dot(pi', h) = 0`. The light
texture coordinates the shader compares are the post-divide
`(u, v, z) = h.xyz / h.w`, and `h = h.w * (u, v, z, 1)`; since `h.w != 0` for
any receiver in front of the light, `a u + b v + c z + e = 0` holds in
post-divide texture space too. A projective map takes planes to planes, and
the plane equation is homogeneous, so the same four coefficients describe the
receiver after the perspective divide. Solving for `z`:

    dz/du = -a / c,    dz/dv = -b / c

exact for any plane, for the orthographic directional projection and the
perspective spot projection alike, with no per-fragment matrix inverse.
Negating `N` negates `pi'`, so the ratios do not depend on the normal's
orientation.

`c` has a direct reading: `c = dot(N, D)` with
`D = W[2].xyz - P * W[2].w`, the world direction along which the texture
depth changes at `P` (the derivative of the world point of
`(u, v, z + s, 1)` with respect to `s`, up to a positive scale), which is the
light ray through `P`. So `|c| / |D|` is `|N . L|`. Two degenerate cases:

- **Edge-on to the camera.** The gradient does not depend on the camera, so
  a receiver seen edge-on keeps its full gradient (the screen-space Jacobian
  the RPDB article inverts is singular there and left `dz/dUV = 0`).
- **Edge-on to the light.** As `N . L -> 0`, `c -> 0` and the gradient is
  unbounded. Below the R1 grazing limit `|N . L| = 0.05` the plane is tilted
  about its line through `P` perpendicular to `L`, on the side it already
  faces, to exactly `|N . L| = 0.05`: `N' = sign(N . L) 0.05 L +
  sqrt(1 - 0.05^2) T`, `T` the normalized component of `N` perpendicular to
  `L`. That clamps `|dz/dUV|` to the slope at the grazing limit in the
  direction the receiver actually tilts, and keeps `c` bounded away from 0.

The remaining error of the gradient is the fp32 rounding of the
position: each derivative carries a few ulps of `|p|`, the distance from the
camera, so the normal tilts by about `ulp(|p|) / pixel_world_size`. On a head-on receiver (exact gradient 0)
this leaves a gradient of that order, which the per-tap offsets of the wide
`receiver_plane` path multiply by up to `K / 2` texels.
`get_receiver_geometric_normal()` therefore returns a bound on that tilt next
to the normal (`vec4`: xyz the unit normal, w the bound in radians), and the
gradient term of the minimum bias (below) covers it.

#### Tap offsets

The offsets follow from one identity. For a planar receiver, depth in light
texture space is an affine function of (u, v) on the plane (see the plane
equation above). The depth the caster pass stored at a texel centre
`c` of that same plane therefore differs from the reference `z` at the sample
point `s` by exactly `dot(c - s, dz/dUV)`, and each tap needs exactly that
offset - with a scale factor of 1. What remains are two error sources:

- **Texel selection.** `c` must be the centre of the texel the hardware
  actually fetches. The hardware rounds texel coordinates to its sub-texel
  precision (8 bits) before selecting texels, so a coordinate on or near a
  rounding boundary selects a texel the shader cannot predict: which way a
  coordinate exactly 1/512 texel below a boundary rounds is the device's
  choice, and a wide kernel's per-gather coordinate `uv + offset / resolution`
  rounds in fp32 differently from the sample point it was derived from. The
  shader therefore picks the texels itself from the sample point
  `t = uv * resolution` - the nearest fetch's texel `floor(t)`, the 2x2 set
  with lower-left texel `gather_base = floor(t - 0.5)` - and fetches them at
  coordinates half a texel from every boundary: the nearest fetch at the
  texel's centre, each `textureGather` at the corner its set's four texels
  share (`(gather_base + gather_texel + 1) / resolution`, `gather_texel` the
  whole-texel offset of the gather in a KxK kernel). The offsets are measured
  from the sample point: `texel_offset_nearest = floor(t) + 0.5 - t`, and
  `gather_fraction = (t - 0.5) - gather_base` in [0, 1), the sample point
  relative to the set's lower-left texel centre. The four gather components
  are the texels at (0, 1), (1, 1), (1, 0), (0, 0) of the set, so a tap's
  offset is `(gather_texel + corner - gather_fraction) / resolution`. Taps
  selected from the rounded coordinate instead misread a whole texel when
  the rounding disagrees: on `contact_blocks` (directional, `pcf_6x6`, 2048)
  one gather column of a pixel whose per-gather coordinate fraction was
  exactly 511/512 read the set one texel over, 12 of 36 taps compared against
  a texel one gradient step (3e-4 in depth) nearer the light, and the pixel
  flickered under a sub-texel camera move (G6).
- **Caster vertex snap.** The rasterizer snaps the caster's vertices to its
  sub-pixel grid (`subPixelPrecisionBits`, 8 on current devices) before it
  interpolates depth. The stored plane is the caster plane displaced by the
  barycentric blend of the vertex displacements, at most one sub-texel step
  `2^-8` along each map axis, so its depth at a texel centre is off by at most
  `2^-8 * (|dz/du| + |dz/dv|) / resolution`. `snap_bias` moves every
  reference toward the light by that bound. Measured on `grazing_fan` with the
  Low preset (hard filter, no rasterizer slope bias): half the bound
  (`2^-9`, the round-to-nearest case) passes G1, a quarter fails it.

Per path:

- Hard (`ERHE_SHADOW_FILTER == 0`): one-sided offset to the fetched texel's
  centre (toward the light only - a centre farther from the light already
  compares lit), plus `snap_bias` and the minimum bias, a direction-aware
  precision snap, then the hardware comparison sampler.
- 2x2 (`== 2`): one `textureGather`; each of the four references gets the
  one-sided offset to its own texel centre plus `snap_bias` and the minimum
  bias; the results are bilinearly weighted by `gather_fraction`.
- Wide KxK (`>= 4`): `(K/2)^2` gathers; the `ERHE_SHADOW_BIAS` axis picks
  either `receiver_plane` (signed per-tap `dot(texel_uv_offset, dz/dUV)` plus
  `snap_bias` and the minimum bias: the reference is the receiver plane's
  depth at the tap, with no slope-scaled net bias, so the contact shadow stays
  attached for any kernel size) or the legacy one-sided `slope_scaled` offset
  plus `snap_bias` and the minimum bias.

The RPDB article's code uses the signed offset for its single tap and
`min(0, ...)` (one-sided) for its 2x2 gather, with no scale factor; erhe
formerly scaled every slope term by 2.0. That factor compensated two defects
that the offsets above remove: the wide paths assumed the sample point sits on
a texel corner (tap offsets of +-0.5 texel), up to half a texel off, and the
snap error was unaccounted for. With the exact offsets and `snap_bias`, the
`grazing_fan` tiles (0 to 88 degrees) read no acne for every filter and both
wide bias modes, for directional and spot lights. On the head-on receiver of
[`plans/shadow_robustness.md`](../plans/shadow_robustness.md) section 1 the
exact gradient is 0, so no bias can come from the gradient there; the
minimum bias below is what separates that tie.

#### Minimum bias

The tap offsets are exact for the receiver plane in real arithmetic. Where the
stored depth and the reference come from the same surface - a lit face stored
under `cull_back` or `cull_none`, most visibly the head-on receiver - the
comparison is a tie that fp32 rounding decides. Every tap reference therefore
also moves toward the light by a minimum bias: the sum of bounds on the error
sources, in texture depth units, computed per fragment in
`sample_light_visibility()`. Notation: `u = 2^-24` the fp32 unit roundoff,
`gamma_n = n u` (first order), `T = texture_from_view_relative` with depth
row `T_z` and w row `T_w`, `P` the receiver point relative to the light's
view origin ("View-relative positions"), `P_c` the receiver relative to the
camera, `h = T (P, 1)`, `z = h.z / h.w`, `|x|` elementwise absolute values,
`D_u`, `D_v`, `D` the columns 0, 1, 2 of `view_relative_from_texture` applied
at `P` (`W[i].xyz - P W[i].w`, see "Receiver depth gradient"), and `c` the
depth coefficient of the receiver plane in texture space.

- **Projection.** The reference is `(T_z . P) / (T_w . P)`. The shadow pass
  evaluates the stored depth of each caster vertex, relative to the same view
  origin, from the same rows (for a [0, 1] depth range the z and w rows of
  `texture_from_view_relative` are the entries of the shadow pass camera's
  `clip_from_view_relative`, both rounded once from the same double
  composition; for [-1, 1] the composed depth row carries one rounding more)
  and divides. A four-term fp32 dot product is
  off by at most `gamma_4` times the sum of the magnitudes it adds,
  `S_z = |T_z| . (|P|, 1)` and `S_w = |T_w| . (|P|, 1)`, and the divide adds
  `u |z|`, so one evaluation is off by at most
  `E = (5u S_z + 4u |z| S_w) / |h.w| + u |z|`. The reference and the stored
  depth are two evaluations: `2 E`. The row magnitudes carry the
  projection's scale and the light camera's offset from its view origin (0
  for a spot light), `|P|` the receiver's distance from the light camera. The
  caster vertices are evaluated at `P`, as their stand-in.
- **Position.** The shadow pass computes the caster's vertices relative to
  the light camera, the forward pass the receiver's relative to the camera
  (`standard.vert`), so the two roundings are independent. The caster
  vertices move the stored plane by at most
  `get_vertex_position_rounding(P) = sqrt(3) 5u |P|` (a four-term row sum
  plus the rounded view-relative translation; `|P|` stands in for the
  magnitudes summed). The receiver point is interpolated from its own
  vertices and moved into the light's frame, which puts it off the surface by
  at most `get_light_relative_receiver_rounding()`:
  `get_position_rounding(P_c) = sqrt(3) 9u |P_c|` (vertices and
  interpolation) plus `sqrt(3) u` times the origin offset and `|P|` (the T7
  tie pass computes the view-relative point from a matrix). At fixed (u, v),
  an offset `eta` along the unit plane normal changes the depth by
  `eta / (c h.w)`, since the texture-space plane evaluates to `eta` at `h`:
  `E_position` is the sum of the two bounds over `|c| |h.w|`, with
  `|c| >= 0.05 |D|` after the grazing clamp.
- **Raster.** The rasterizer evaluates the caster primitive's depth at the
  texel centre as a combination of its post-clip vertex depths `z_i` with
  barycentric weights `l_i` in [0, 1] summing to 1: each weight and each
  product rounds once, and the two sums once each, so the stored depth is off
  by at most `2u sum(l_i |z_i|) + 2u max|z_i| <= 4u max|z_i|`. The vertex
  depths are those of the caster, not of the receiver: clipping keeps every
  one inside the clip volume's [0, 1], and a primitive clipped at the depth-1
  plane (the near plane under reverse-Z, the far plane under forward-Z) has
  vertices at depth 1 however small the texel's own depth is. The term is
  therefore `4u`, with the vertex depth bound 1. This is the error of a large
  receiver that extends behind the light: on `cube_seams` the 6.2 m floor's
  triangles have a corner behind the spot light, are clipped at the near
  plane, and the stored depth of each triangle is the exact plane depth plus
  an affine offset of 0.1 to 7.7 `u` (plane fit residual at most 0.05 `u`),
  hundreds of ulps of the texel's own depth 0.02; a bound taken with the
  receiver's `|z|` in place of the vertex depths (`4u |z|`, 3 ulps there)
  left the hard filter at 2048 texels and `pcf_4x4` at 512 with moire acne
  on the floor below the light at no rasterizer bias.
- **Gradient.** `get_receiver_geometric_normal()` bounds the error of its
  normal: each position derivative is the difference of two rounded
  positions, off by at most `e = 2 get_position_rounding(P_c)` (the vertex
  rounding included: the stored plane comes from vertices rounded in the
  light camera's frame, so the receiver's triangle is bounded against the
  true surface), so the cross
  product moves by at most `e (|dp/dx| + |dp/dy| + e)` and the unit normal
  tilts by at most `theta`, that over `|dp/dx x dp/dy|`. The texture-space
  plane is linear in the normal, `(a, b, c) = (N . D_u, N . D_v, N . D)`, so
  a tilt `dN` with `|dN| <= theta` moves the plane depth
  `z - (a o_u + b o_v) / c` at a tap offset `o` (uv units) by at most
  `theta (|o_u| |D_u| + |o_v| |D_v| + |dz/dUV . o| |D|) / (|c| - theta |D|)`.
  The tap reach bounds `|o|` per map axis: 1/2 texel for `hard`, 1 for
  `pcf_2x2`, K/2 for `pcf_KxK` (the fetched texels are chosen from the
  sample point, "Tap offsets"), each plus the 1/256 caster snap step
  (`snap_bias` also reads the gradient). This
  is the term that keeps the head-on receiver's wide `receiver_plane` taps
  from reading the gradient's rounding noise as slope.
- **Format.** The map stores the depth rounded to its format (D0): at most
  one quantum `1 / (2^bits - 1)` for UNORM, whatever the rounding mode, and
  one float ulp of `z`, at most `2u |z|`, for a float map (negligible near 0
  under reverse-Z, about `2^-24` near 1.0 under forward-Z). The hard path's
  UNORM reference is rounded up onto the stored grid instead (the precision
  snap), which covers the quantum without adding it.

The minimum bias is

    shadow_bias_texel_scale * gradient
      + shadow_bias_origin_scale * (2 E + position + raster)
      + format

added toward the light (`(-cdd)` sign) to every tap reference of the hard,
2x2 and wide paths alike, orthogonal to the wide-only `ERHE_SHADOW_BIAS`
axis; the tap offsets and `snap_bias` stay as they are. The two scales are
graphics preset fields (dimensionless, default 1 = the derived bound;
Settings window rows "Shadow Bias Texel Scale" / "Shadow Bias Origin Scale",
`set_graphics_preset`), carried by `Shadow_renderer::Render_parameters` into
`Light_projections` and written to `light_block.shadow_bias_scales`
(x texel, y origin). The receiver-side bound is used rather than the
rasterizer constant bias because the rasterizer's unit depends on each
primitive's depth extent and on the format; `shadow_depth_bias_constant`
stays as the caster-side control.

Magnitude. The raster term puts a floor of `4u = 2^-22` under the bound in
texture depth, 128 float ulps of a reference depth in [1/64, 1/32) (a
reverse-Z receiver a few metres from a spot light); the other terms are 15
to 95 ulps on the T7 station (receivers within about 5 m of the origin, a D32
map) over the poses and filters, against at most about 5 ulps of measured
stored / reference difference there. In world units that is micrometres to
a fraction of a millimetre (the raster floor along a spot light's ray is
about `2^-22 d^2 / n`, `n` the 0.04 m spot near plane: 0.05 mm at 3 m, 0.6 mm
at 10 m, against texels of millimetres to centimetres), so the
`contact_blocks` contact gap (G3) and edge placement (G5) read the same with
and without it on Low and Medium. The projection, position and gradient
terms grow with the receiver's distance from the camera and from the light
camera, not with its distance from the world origin ("View-relative
positions"), so a station translated 10 km from the origin reads the gates
it reads at the origin.

#### Undetermined receiver plane

The receiver plane is undetermined when the world-position derivatives do not
span a plane (their cross product is exactly 0: the quad's rounded positions
are equal or collinear, which needs a pixel footprint at or below the fp32
resolution of `P`; `get_receiver_geometric_normal()` then returns
`vec4(0.0)`), or when the normal's error bound reaches half the receiver's own
`|N . L|` (taken at least at the 0.05 grazing limit), beyond which the
first-order gradient bound does not hold. `sample_light_visibility()` then
takes the receiver head-on to the light, `N = D / |D|` (the light ray at
`P`), for the gradient and the tap offsets, and makes the bias cover every
plane R1 admits:

- Gradient term. In the homogeneous plane equation a plane tilted from the
  head-on normal by `alpha` has normal `N + tan(alpha) T`, `T` a unit vector
  perpendicular to `D`, which leaves `c = N . D` unchanged, so the plane
  depth at a tap moves by at most
  `tan(alpha) (|o_u| |D_u| + |o_v| |D_v|) / |D|`. `tan(alpha)` is taken at
  the grazing limit, `sqrt(1 - 0.05^2) / 0.05 = 19.97`.
- Position term. `|c|` is taken at the grazing limit, `0.05 |D|`.

This matches the article's core idea: a signed receiver-plane gradient extended
to PCF with a per-texel bias. The hard and 2x2 paths always use their own fixed
bias and ignore the `ERHE_SHADOW_BIAS` axis; only the wide paths switch on it.

erhe also goes beyond the article:

- Reverse-Z aware throughout (`cdd = clip_depth_direction` drives the bias sign,
  the rounding direction, and the non-strict `gequal` / `lequal` comparison;
  the caster-side rasterizer bias below is converted the same way).
- Receivers outside the fitted depth range are handled by clamping the reference
  to [0, 1] after biasing instead of an early-out (see "Receivers outside the
  fitted depth range" above).
- Filter, bias, technique, and shadow depth format are compile-time shader
  variants (`SHADOW_FILTER`, `SHADOW_BIAS`, `SHADOW_TECHNIQUE`,
  `SHADOW_DEPTH_BITS` in `shader_key.hpp`), selected from the graphics preset
  (`SHADOW_DEPTH_BITS` from the created shadow map's format) rather than
  branched at runtime. The hard path's precision snap is derived
  from `SHADOW_DEPTH_BITS` -- the format's `2^bits - 1` levels for a UNORM map,
  skipped for D32_SFLOAT -- so it matches the actual shadow map format.
- A complementary caster-side rasterizer depth bias (constant + slope) exists
  (`shadow_depth_bias_constant` / `_slope`, applied in `shadow_renderer.cpp` via
  `set_depth_bias`). Its field default is 0 and every committed preset sets
  both to 0: neither technique needs it. With the receiver's minimum bias
  ("Minimum bias") and the texel selection of "Tap offsets", every
  `cull_back` and `cull_none` depth technique cell of the core matrix of
  [plans/shadow_robustness.md](../plans/shadow_robustness.md) reads the same
  gates at slope 0 as at -1, with the same contact gap (G3); the distance
  technique's stored value does not contain the rasterized depth at all, so
  the rasterizer bias only changes which of two nearly coincident casters
  wins the depth test ("The distance technique"). Under `cull_front` a
  negative slope bias moves the stored back faces away from the light and
  widens that mode's leaks. Both values are signed toward the light
  under either depth convention: negative moves the stored caster depth away
  from the light. `Shadow_renderer::render()` passes them to the device as is
  for reverse-Z (the light side is the larger depth) and negated for forward-Z
  (the light side is the smaller depth), so one preset value means the same
  bias in both conventions. The article criticizes rasterizer slope
  bias as an over-estimate, so erhe relies on the receiver-side bounds.

### The distance technique

The `distance` value of the `shadow_technique` graphics preset field (see
"Editor integration") stores light distances in a parallel R32F map instead
of comparing hardware depth, for directional and spot lights; `depth` (the
RPDB path above) remains the default. Point lights use their own distance
cube with either value (see [Point-light cube-map shadows](#point-light-cube-map-shadows)).
It is the 2D form of the point-light cube's stored distance and receiver bias
([`point_light_shadows.md`](point_light_shadows.md) "Stored distance" and
"Receiver bias"): the stored value and the reference are both the distance of
a plane on the same ray, so they agree exactly in real arithmetic and the
bias is the sum of derived fp32 error bounds.

**The ray of a texel.** `get_shadow_distance_ray()`
(`res/shaders/erhe_shadow_distance.glsl`) builds the ray through the centre
`(i + 0.5) / N` of texel `i` (`get_shadow_distance_texel_centre()`) from the
light's `view_relative_from_texture`, in the light's view-relative space (the
light camera at the origin; "View-relative positions"): the point `X` of
texture coordinates (centre, z = 0.5). For a spot light the ray starts at the
light position, which is that origin, and points at `X`, and a distance is
the radial distance from the light. For a directional light the ray starts at
`X` and follows the projection's depth axis
(`view_relative_from_texture[2]`, oriented away from the light), and a
distance is the linear light-space depth, in world units from the map's
mid-depth plane (negative nearer the light). Both passes evaluate these expressions as
`precise` (no contraction or reassociation) from the same inputs, so the
caster and the receiver get the same ray bit for bit, and the ray's own
rounding is not an error term.

**Stored distance.** The caster (`standard.frag` under
`VARIANT_SHADOW_DISTANCE`) runs at its pixel centre, the texel centre. It
finds its texel from the texture coordinates (`texture_from_view_relative`)
of its interpolated view-relative position `p`, relative to the light camera,
which lie a small fraction of a texel from that centre, and
reads the map resolution from `light_control_block.shadow_map_resolution`.
Its plane is the geometric normal `N` of the derivatives of `p`, through `p`,
and it stores the farther of the plane's distance on the ray,
`N . (p - O) / (N . d)` (`O`, `d` the ray origin and direction), and the
distance of `p` itself (radial `|p - O|` for a spot light, `d . (p - O)` for
a directional one). The plane distance does not depend on where the
rasterizer puts `p` within the primitive's plane or on the rasterized depth,
so neither barycentric precision on large near-clipped triangles nor the
rasterizer depth bias reaches the stored value; the rasterized depth only
picks which caster is nearest. The farther-of-two rule is for coverage: the
rasterizer snaps vertices to 1/256 pixel, so a primitive can cover a texel
centre up to that step past its true edge, and there its plane on the ray is
an extension. At a convex crease the extension of the steeper face is nearer
the light than the neighbour face on the ray by the step times the steep
face's own slope, which no receiver-side quantity bounds (on `grazing_fan`,
Medium, the 85 and 88 degree tiles' steep faces covered their 2 cm edge
faces 0.6 to 2.8 mm from the crease, one `pcf_4x4` tap each: 2 directional
and 1 spot pixel with the plane distance alone). `p` lies inside the
primitive, on the far side of the neighbour's plane and within one snap step
of the ray, which the receiver's coverage snap term below covers. The
receiver's own surface stores its plane distance on the ray, and taking the
farther value can only raise it, so the rule never shadows a lit receiver;
an occluder's value moves away from the light by at most its slope times the
offset of `p` from the ray. A caster within
`erhe_shadow_distance_plane_cos_min` (0.01) of edge-on to its ray, or with no
plane at this footprint, stores the distance of `p`. The map is cleared to
`1e30`, which every reference compares lit.

**Receiver.** `sample_light_visibility()` takes the same receiver plane as
the depth technique (the geometric normal, tilted to the R1 grazing limit,
and the "Undetermined receiver plane" rule), picks the texels from the sample
point and fetches them at their centres / shared gather corners ("Tap
offsets"), and per tap (`get_shadow_distance_tap()`) compares the receiver
plane's distance on that tap's ray: the hard path and `pcf_2x2` one-sided
(capped at the receiver point's own distance, like the depth technique's
one-sided offsets), the wide paths signed for `receiver_plane` and one-sided
for `slope_scaled`. An undetermined plane takes the nearest plane point on
the ray over the planes within the grazing limit of head-on: for a
directional light the receiver's distance less `tan(alpha_max)` times its
lateral distance from the ray, for a spot light the point-light cube's
closed form. Every reference then moves toward the light by the sum of these
bounds (world units along the ray, `u = 2^-24`, `P` the receiver point
relative to the light camera, `e = 2 get_position_rounding(P)`):

- **Coverage snap.** The receiver plane's distance slope per texel,
  `|N . l_u| / |N . d|` plus the same for `v`, times 1/256, where `l_u`,
  `l_v` are one texel step at the receiver point perpendicular to its ray
  (`D_u h.w / N` less its component along the ray); an undetermined plane
  takes the grazing-limit slope. Unscaled, like `snap_bias`.
- **Receiver gradient.** The normal's error bound from
  `get_receiver_geometric_normal()` times the distance from `P` to the plane
  point on the tap's ray, over `|N . d|` less the bound.
- **Caster gradient.** The caster normal's error bound
  `e (|l_u| + |l_v| + e) / |l_u x l_v|` (its derivatives span one map pixel;
  the caster taken at the receiver's distance, where the tie is), times half
  the texel diagonal on the plane, over `|N . d|` less the bound but at least
  the caster's threshold 0.01.
- **Position.** The rounding of the receiver point
  (`get_light_relative_receiver_rounding()`) and of the caster's interpolated
  point (`get_position_rounding(P)`), over `|N . d|`.
- **Evaluation.** Each side's `N . (p - O) / (N . d)`: the subtraction and
  the three-term dot product `4u |p - O|`, the dot `N . d` `3u |s| / |N . d|`
  and the divide `u |s|` (`s` the distance), plus the receiver's own length
  or dot product, `13u |P - O|`.

The gradient terms scale with `shadow_bias_texel_scale` and the position and
evaluation terms with `shadow_bias_origin_scale`, as for the depth technique;
R32F stores the caster's value exactly, so there is no format or raster term.
The reference needs no [0, 1] clamp: a receiver beyond the fitted far plane
has a larger distance than every caster above it, one nearer than the near
plane a smaller distance than every caster, and empty texels hold `1e30`.

Validity. For a directional light the taps' rays are parallel, so the
receiver meets each at its own clamped `|N . d| >= 0.05`. A spot light's rays
fan out by about `2 tan(fov / 2) / N` rad per texel, 0.004 at 512 texels and
90 degrees, so over the widest reach (`pcf_6x6`, 3 texels plus the snap) the
receiver keeps `|N . d| >= 0.038` there, above the largest normal error the
determined-plane rule admits (0.025) and the caster's threshold 0.01; a spot
map that is coarser for its cone than that narrows the margin.

The technique replaced a caster-side "bias-free" form ("bias-free shadow
mapping", Avelina9X, r/GraphicsProgramming) that stored
`gl_FragCoord.z + fwidth(z) (1 + K / 2)` and compared unbiased. `fwidth` is
an L1 over-estimate of the caster's slope, zero for a head-on caster, and
`gl_FragCoord.z` carries the rasterizer depth bias; the plane comparison on
the texel's own ray needs no slope term at all. Measured with
[`plans/shadow_robustness.md`](../plans/shadow_robustness.md) section 6 (the
Medium distance config, the core matrix and the pairwise matrix), the
distance technique reads the depth technique's gates, with its contact gap
(G3) and edge placement (G5).

## Point-light cube-map shadows

Point lights cast **omnidirectional** shadows through a separate cube-map path.
Directional and spot lights use the 2D depth array above; point lights never
touch the fit, the 2D array, or `texture_from_world`. The full design, including
the per-face coordinate flip, is in
[`point_light_shadows.md`](point_light_shadows.md); this is the summary.

- **Storage.** One R32F `texture_cube_map_array` (labelled `Point shadow cube
  array`), `6 * point_shadow_light_count` layers (layer `6*cube + face`, Vulkan
  face order +X,-X,+Y,-Y,+Z,-Z). Each texel stores the **radial distance**
  from the light, along its centre ray, of the nearest caster's plane -- not
  projected/non-linear depth, and not normalized by far -- so neither caster
  nor receiver needs the far plane; unrendered texels hold a large clear
  (`1e30`) that reads as "lit". `point_shadow_resolution` /
  `point_shadow_light_count` are graphics-preset fields (defaults 512 / 2;
  the resolution is at least `c_min_point_shadow_resolution`, 64).
- **Caster** (`Shadow_renderer` point-cube pass, `shadow_renderer.cpp`). For each
  shadow-casting point light, six render passes -- one per face -- rasterize the
  scene into that cube layer from a `create_look_at(light_pos, light_pos +
  look[f], up[f])` camera with a 90-degree perspective
  (`Light::point_light_projection_transforms()`, `perspective_z_far = light->range`). The
  caster fragment (`standard.frag` under `VARIANT_SHADOW_CUBE`) writes its
  primitive plane's distance on the texel centre ray (from the geometric
  normal of the interpolated position relative to the light, the face pass's
  view origin, and that position) to the R32F face (`cull_none`; the face
  resolution comes from `light_control_block.shadow_map_resolution`), so the
  rasterizer's placement of the
  interpolated point does not reach the stored value
  ([`point_light_shadows.md`](point_light_shadows.md) "Stored distance"). A
  shared 2D depth scratch is reused for every face, for rasterization only
  (store `DONT_CARE`).
- **Coordinate flip (convention-driven).** A cube face is sampled by direction
  through the fixed cube-map (s,t) convention, which is vertically inverted
  relative to the framebuffer row order, so the caster needs its clip-space Y
  flipped opposite to the screen pass. This is expressed through the coordinate
  conventions -- the cube pass gets `clip_space_y_flip` enabled iff
  `framebuffer_origin == top_left`, mirroring `Light::get_texture_from_clip` for
  the 2D map. Without it every stored face is mirrored in t and the shadows are
  displaced. A shader-side `gl_Position.y` negate is deliberately not used: it
  is unconditional, and so wrong on bottom_left OpenGL.
- **Receiver.** `sample_point_light_visibility()` (`erhe_light.glsl`) fetches,
  at the light's cube layer (`shadow_index_packed.y`), the texel that contains
  the receiver direction at its centre (`get_point_shadow_texel_centre()`,
  `erhe_point_shadow.glsl`, shared with the caster) and compares the receiver
  plane's distance on that centre ray - one-sided, like the 2D tap offsets -
  against the stored distance. The plane comes from the receiver's geometric
  normal (`get_receiver_geometric_normal()`, the same argument
  `sample_light_visibility()` takes). The reference moves toward the light by
  the D1 error bounds restated for radial distances - the coverage snap of a
  crease neighbour, the receiver's and the caster's normal errors, position
  rounding and fp32 evaluation - with the same two preset scales and no
  constant world floor
  ([`point_light_shadows.md`](point_light_shadows.md) "Receiver bias"). The
  forward pass multiplies the point light's contribution by this visibility
  (`standard.frag`). 1.0 = lit.
- **Indexing.** Shadow-casting point lights get a dense `point_shadow_index`
  (cube-array base) in `Light_projections::apply()`, written to
  `shadow_index_packed.y`; their 2D `shadow_index` stays `max()` so the 2D shadow
  loop and 2D receiver sampling skip them for free. Point is the last
  shadow-bearing bucket, so it does not shift directional/spot 2D layer indices.
  A fallback 1x1 cube (cleared to `1e30`) is bound when no real cube exists.
- **No frustum fitting.** The six faces are fixed 90-degree perspectives centered
  on the light; the only tuning is `range` (the cube far plane). The directional tight-fit pipeline does not apply, and lights exceeding
  `point_shadow_light_count` are dropped from the cube (same implicit cap as the
  2D `shadow_light_count`).

## Editor integration

- **Settings** - `Shadow_frustum_fit_config`
  (`src/editor/config/definitions/shadow_frustum_fit_config.py`, generated
  via erhe_codegen) mirrors `Shadow_frustum_fit_settings` 1:1 and is edited
  in the Settings window; it persists in `editor_settings.json`
  (`Editor_settings_config` version 4). `Shadow_render_node` re-reads it
  every frame, so changes apply live.
- **Shadow technique** - the graphics preset's `Shadow_technique_mode`
  (`shadow_technique`, Settings window combo, `set_graphics_preset`) selects
  `depth` (the RPDB path, default) or `distance` ("The distance technique")
  for every directional and spot light of the preset. For `distance`,
  `Shadow_render_node` allocates a parallel R32F `texture_2d_array` as a color
  attachment of the 2D shadow passes, cleared to `1e30`; the caster
  (`standard.frag` under `VARIANT_SHADOW_DISTANCE`) writes its plane's light
  distance on each texel's centre ray into it (the map resolution on the
  per-pass `light_control_block`), and the receiver (`erhe_light.glsl`,
  `ERHE_SHADOW_TECHNIQUE == DISTANCE`) samples `s_shadow_distance`. No
  committed preset enables it. Point lights are unaffected by it -- they
  always use the omnidirectional radial-distance cube path (see [Point-light
  cube-map shadows](#point-light-cube-map-shadows)).
- **MCP** - `set_graphics_preset` sets the preset's shadow fields for the
  rest of the run without writing `graphics_presets.json`
  ([mcp_server_usage.md](../agents/mcp_server_usage.md)).
- **Point shadow resolution / count** - the graphics preset's
  `point_shadow_resolution` and `point_shadow_light_count` (Settings window
  sliders) size the cube array; changing either reallocates it in
  `Shadow_render_node::reconfigure()`.
- **Fit target camera override** - the Scene View Config window (also
  openable from the viewport toolbar popup) has "Override Shadow Fit Target
  Camera": when set, the fit (and its debug data) targets that camera
  instead of the viewport camera, so the fit can be observed from outside
  its own frustum while flying around. Ignored if the camera is deleted or
  belongs to another scene.
- **Debug visualization** - with the `collect_debug` setting on (toggleable
  both in the Settings window and as "Collect Debug Data" at the top of the
  Shadow Fit group), the fit records per-light intermediates
  (`Shadow_frustum_fit_debug_data`): caster hull, the `filter_planes` the
  caster AABBs are tested against (the half-space planes in
  `shadow_volume_planes`), receiver (view frustum) corners, fit point set,
  light plane 2D hull, rotating calipers OBB, and the light space box after
  each pipeline step. With the setting off, every `debug_out` branch in the
  fit is skipped and no debug data structures are maintained anywhere in the
  fit path, so the algorithm can be profiled on its own. The "Volume Planes" element reconstructs the convex
  polyhedron those planes bound (`erhe::math::convex_polyhedron_from_planes`)
  and draws its intersection edges and vertices plus an
  `add_plane_indicator()` orientation marker at each plane's face center. Since
  the filter volume is open toward the light, a synthetic cap one shadow range
  beyond the frustum is added only to bound it for display (its own indicator
  is not drawn). The "Far Plane Hull" element instead draws the receiver
  silhouette directly - the 2D hull of the clipped receiver hull projected onto
  the flat far cap, lifted to world space - so it shows the polygon the receiver
  volume side planes are swept from, independent of the polyhedron
  reconstruction. `Debug_visualizations` draws them under its "Shadow Fit"
  toggle (independent of the "Shadow Debug" shadow texel visualization), with
  per-element toggles, colors and line widths (negative widths are in pixels
  and do not scale with distance). The fit is per light, so the group has a
  "Fit Light" selector; everything is drawn for that one light (defaulting to
  the first light with valid fit data). The "Casters" element classifies every
  visible shadow caster against the selected light's F_shadow with
  `erhe::math::aabb_in_convex_volume` - the same test the fit applies - drawing
  affecting casters in the "Casters" color and culled (non-affecting) ones in
  the "Casters Culled" color, and tagging each mesh with the transient
  `Item_flags::affects_shadow` bit so the classification is visible elsewhere
  (e.g. the item tree flag display).
- **XR note** - the tight fit needs the main camera viewport only for the
  view frustum aspect ratio; headset cameras use fov sides instead, so
  non-viewport scene views pass an empty viewport.

## Key files

| File | Contents |
|---|---|
| `src/erhe/scene/erhe_scene/light.cpp` | Stable fit, fit dispatch, transform assembly; point-light cube projection (`point_light_projection_transforms`); 2D texture-space matrices (`get_texture_from_clip`, framebuffer_origin-derived y-flip) |
| `src/erhe/scene/erhe_scene/light.hpp` | `Shadow_frustum_fit_settings`, `Light_projection_parameters` |
| `src/erhe/scene/erhe_scene/light_frustum_fit.cpp` | Tight modular fit pipeline |
| `src/erhe/scene/erhe_scene/light_frustum_fit.hpp` | `Shadow_frustum_fit_debug_data`, `Shadow_fit_step` |
| `src/erhe/math/erhe_math/math_util.cpp` | Convex hull clip, point-in-hull, rotating calipers, F_shadow open planes (`build_shadow_caster_volume_planes`) + silhouette side planes (`build_shadow_caster_silhouette`), AABB-vs-convex-volume cull (`aabb_in_convex_volume`), half-space intersection to polyhedron (`convex_polyhedron_from_planes`) |
| `src/erhe/scene_renderer/erhe_scene_renderer/shadow_renderer.cpp` | Shadow pass, caster gathering, depth clamp pipeline, scissor border; distance-technique variant (color writes + `VARIANT_SHADOW_DISTANCE` + map resolution); point-light cube pass (6 faces, `VARIANT_SHADOW_CUBE`, framebuffer_origin-gated `clip_space_y_flip`) |
| `src/erhe/scene_renderer/erhe_scene_renderer/light_buffer.cpp` | `Light_projections::apply()`, light UBO (per light `view_origin`, `texture_from_view_relative`, `view_relative_from_texture`), shadow samplers; `s_shadow_distance` + distance fallback, `shadow_map_resolution` control field (2D map or cube face resolution); `point_shadow_index` assignment, `shadow_cube_texture` + 1x1 fallback cube |
| `src/erhe/scene_renderer/erhe_scene_renderer/camera_buffer.cpp` | `get_view_origin()`, `get_clip_from_view_relative()`; camera block `view_origin`, `clip_from_view_relative` |
| `src/erhe/scene_renderer/erhe_scene_renderer/program_interface.cpp` | Bind group layout: `s_shadow_compare` / `s_shadow_no_compare` (depth) + `s_shadow_distance` (color) + `s_shadow_cube` (R32F cube array) sampler bindings |
| `res/shaders/erhe_light.glsl` | Shadow sampling: RPDB depth path + distance path (`get_shadow_distance_tap`), reference depth clamp; `sample_point_light_visibility` (texel-centre fetch, receiver-plane reference, derived radial bias) |
| `res/shaders/erhe_point_shadow.glsl` | Point cube texel-centre selection shared by the cube caster and the receiver |
| `res/shaders/erhe_shadow_distance.glsl` | Distance technique texel centre and ray (`precise`) shared by the distance caster and the receiver |
| `src/erhe/scene_renderer/test/shadow_gpu_test_fixture.hpp` | Shadow GPU test fixture: station, poses, shadow map / forward / Shadow_tie renders |
| `src/erhe/scene_renderer/test/test_shadow_gpu.cpp` | Shadow sampling GPU test cases |
| `src/erhe/scene_renderer/test/shaders/shadow_tie.frag` | Shadow_tie fragment pass (reference depth offset per band) |
| `res/shaders/standard.frag` | Caster: `VARIANT_SHADOW_DISTANCE` writes the caster plane's light distance on the texel centre ray to the distance map; `VARIANT_SHADOW_CUBE` writes the caster plane's radial distance on the texel centre ray to the cube face; point receiver multiplies `sample_point_light_visibility` |
| `res/shaders/standard.vert` | `view_relative_from_node`, `gl_Position` from `clip_from_view_relative`; `v_view_relative_position` to the receiver and to the `VARIANT_SHADOW_CUBE` / `VARIANT_SHADOW_DISTANCE` caster fragment |
| `src/editor/rendergraph/shadow_render_node.cpp` | Editor wiring: settings refresh, fit camera override, technique-aware distance-map allocation + color attachment; point cube array + per-face render passes (`reconfigure` on `point_shadow_resolution` / `point_shadow_light_count`) |
| `src/editor/tools/debug_visualizations.cpp` | Shadow fit debug visualization |
| `src/editor/config/definitions/shadow_frustum_fit_config.py` | Frustum fit settings codegen definition |
| `src/editor/config/definitions/shadow_technique_mode.py` | Shadow technique enum codegen definition (depth / distance) |
| `doc/erhe/point_light_shadows.md` | Point-light cube shadow design, per-face coordinate flip, risks and tuning knobs |
| `doc/erhe/shadow_tight_fit.md` | Cost model and standing optimizations of the directional fit |

## Future work

- [plans/shadow_robustness.md](../plans/shadow_robustness.md) - bias hardening, shadow test scenes and automated shadow verification.
- [plans/shadows.md](../plans/shadows.md) - remaining fit and point-shadow performance candidates.
