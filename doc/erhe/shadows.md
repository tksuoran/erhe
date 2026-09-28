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
from the light node pose (`Light::spot_light_projection_transforms()`).
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
  centered on the view camera position; orthographic projection
  `2r x 2r x 2r`.
- The view camera position is snapped to shadow map texel increments in the
  light plane, so camera translation does not make shadow edges shimmer
  (`texel_snap` also controls this path).
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
- `texel_snap` pads the box by two texels and snaps its min corner down onto
  the texel grid. The padding guarantees that snapping never drops coverage
  at the max edge; the snap is only effective while the box size (and thus
  the texel grid) stays constant, which is what `quantize_extents` provides.

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
  caster pipeline: `cull_front` (default) writes only back faces, which reduces
  peter-panning for closed meshes (open / single-sided meshes do not cast from
  their front side); `cull_back` writes only front faces, letting single-sided
  geometry cast from the side facing the light; `cull_none` writes both sides.
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
- **One texel empty border** - the pass sets a scissor rectangle inset by one
  pixel on each side, keeping the outermost texel ring at the clear value.
  Combined with the shadow samplers' `clamp_to_edge` address mode, any
  shadow lookup outside the map's XY range compares against the far clear
  value and resolves to fully lit. This is what makes out-of-XY receivers
  safe without any range check in the shader.
- **Prewarm** - `prewarm_pipelines()` warms the active cull mode's two base
  pipelines (regular and depth clamp) times both winding variants, so neither
  toggling `depth_clamp` nor mirroring a mesh hitches at first draw. Changing
  the cull mode is a rare settings action and compiles the other modes'
  pipelines once, on demand.

## Shadow sampling

`sample_light_visibility()` in `res/shaders/erhe_light.glsl`:

- The fragment's world position is transformed by the light's
  `texture_from_world` into [0,1] texture space plus depth.
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
- Out-of-range XY needs no check: the one texel empty scissor border plus
  `clamp_to_edge` (see above) resolves those lookups to lit.

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
  require visibility 1 on the head-on plane for every offset / pixel. They
  fail on the current bias (the head-on tie of
  [plans/shadow_robustness.md](../plans/shadow_robustness.md) section 1) and
  are `DISABLED_` until its phase 4.
- `shadow_tie_exact_pose_with_rasterizer_bias_reads_lit` and
  `shadow_head_on_plane_exact_pose_with_rasterizer_bias_reads_lit`: at the
  identity pose a rasterizer constant bias of -4 makes the plane read 1.
- `shadow_caster_box_occludes_plane`: a box above the plane reads 0 at least
  5 cm inside its analytic shadow at every pose, and at the identity pose 1
  at least 10 cm outside it.

## Bias technique: RPDB reference, and the distance/fwidth alternative

erhe's receiver-side bias is the receiver-plane depth bias (RPDB) method from
https://renderdiagrams.org/2024/12/18/shadowmap-bias/ (cited in
`sample_light_visibility()`, `res/shaders/erhe_light.glsl`). This section records how the implementation
maps onto that reference, where it goes beyond it, and the alternative
"bias-free" technique exposed as the `distance` shadow technique. The known
deltas from the reference are listed in
[`plans/shadow_robustness.md`](../plans/shadow_robustness.md).

### What erhe implements (RPDB)

`sample_light_visibility()` derives the receiver's light-space depth gradient
`dz/dUV` from the receiver plane, then offsets the comparison reference of
every tap to the depth the receiver plane has at that tap's texel centre.

#### Receiver depth gradient

The caller passes the receiver's unit geometric normal `N` in world space:
`get_receiver_geometric_normal()` (`erhe_light.glsl`, fragment stage only)
returns `normalize(cross(dFdxFine(p), dFdyFine(p)))` of the world position
`p`, which lies in the triangle's plane for a planar triangle and ignores
smooth vertex normals and normal maps. `standard.frag` takes it once, in
uniform control flow ahead of the per-light branches, and passes it to every
shadow-mapped directional and spot light and to the mode 30 debug view.

With `P` the receiver point, the world plane is the row vector
`pi = (N, -dot(N, P))`, which is 0 on homogeneous points `(x, 1)` of the
plane. With `W = world_from_texture` (the inverse of `texture_from_world`,
already in the light block), a homogeneous texture point `h` maps to the
world point `W h`, so the plane in homogeneous texture coordinates is
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

The remaining error of the gradient is the fp32 rounding of the interpolated
world position: each derivative carries up to about one ulp of `|p|`, so the
normal tilts by about `ulp(|p|) / pixel_world_size`. On a head-on receiver
(exact gradient 0) this leaves a gradient of that order, which the per-tap
offsets of the wide `receiver_plane` path multiply by up to `K / 2` texels;
the minimum bias of D1 in
[`plans/shadow_robustness.md`](../plans/shadow_robustness.md) is what
absorbs it.

#### Tap offsets

The offsets follow from one identity. For a planar receiver, depth in light
texture space is an affine function of (u, v) on the plane (see the plane
equation above). The depth the caster pass stored at a texel centre
`c` of that same plane therefore differs from the reference `z` at the sample
point `s` by exactly `dot(c - s, dz/dUV)`, and each tap needs exactly that
offset - with a scale factor of 1. What remains are two error sources:

- **Texel selection.** `c` must be the centre of the texel the hardware
  actually fetches. The hardware rounds texel coordinates to its sub-texel
  precision (8 bits) before selecting texels, so a coordinate within 1/512
  texel below a boundary selects the next texel. The shader reproduces the
  selection with `floor(t + 1/512)` and measures the offset from the
  unrounded `t` (`texel_offset_nearest` for the nearest fetch,
  `gather_fraction` for `textureGather`, whose texel centres sit at integer
  `t = uv * resolution - 0.5`). The four gather components are the texels at
  (0, 1), (1, 1), (1, 0), (0, 0) of the selected 2x2 set, so a tap's offset is
  `(gather_texel + corner - gather_fraction) / resolution`, `gather_texel` the
  whole-texel offset of its gather in a KxK kernel.
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
  compares lit), plus `snap_bias`, a direction-aware precision snap, then the
  hardware comparison sampler.
- 2x2 (`== 2`): one `textureGather`; each of the four references gets the
  one-sided offset to its own texel centre plus `snap_bias`; the results are
  bilinearly weighted by `gather_fraction`.
- Wide KxK (`>= 4`): `(K/2)^2` gathers; the `ERHE_SHADOW_BIAS` axis picks
  either `receiver_plane` (signed per-tap `dot(texel_uv_offset, dz/dUV)` plus
  `snap_bias`: the reference is the receiver plane's depth at the tap, with no
  net bias, so the contact shadow stays attached for any kernel size) or the
  legacy one-sided `slope_scaled` offset plus `snap_bias`.

The RPDB article's code uses the signed offset for its single tap and
`min(0, ...)` (one-sided) for its 2x2 gather, with no scale factor; erhe
formerly scaled every slope term by 2.0. That factor compensated two defects
that the offsets above remove: the wide paths assumed the sample point sits on
a texel corner (tap offsets of +-0.5 texel), up to half a texel off, and the
snap error was unaccounted for. With the exact offsets and `snap_bias`, the
`grazing_fan` tiles (0 to 88 degrees) read no acne for every filter and both
wide bias modes, for directional and spot lights. The tie that remains is the
head-on receiver of
[`plans/shadow_robustness.md`](../plans/shadow_robustness.md) section 1,
whose exact gradient is 0: its bias cannot come from the gradient at all,
and what is left there is the reference / stored depth rounding of the matrix
composition and the gradient rounding above.

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
  `set_depth_bias`) but defaults to 0. Both values are signed toward the light
  under either depth convention: negative moves the stored caster depth away
  from the light. `Shadow_renderer::render()` passes them to the device as is
  for reverse-Z (the light side is the larger depth) and negated for forward-Z
  (the light side is the smaller depth), so one preset value means the same
  bias in both conventions. The article criticizes rasterizer slope
  bias as an over-estimate, so erhe relies on RPDB by default.

### The distance / fwidth alternative

The same derivative-slope idea can be moved to the caster pass instead
("bias-free shadow mapping", Avelina9X, r/GraphicsProgramming). Store a linear
distance in the shadow map and have the shadow-pass fragment shader return
`d + fwidth(d) * (1 + pcfRadius)` (`fwidth = |ddx| + |ddy|`). The bias is then
baked per shadow texel and the receiver compares with no bias and no `dz_dUV`
work. Trade-offs versus erhe's receiver-side RPDB:

- The bias is computed once per shadow texel, in the cheap (under-utilized)
  shadow pass, and is shared by every receiver and every PCF tap -- the hot
  forward shader stays bias-free and register-light. RPDB recomputes a per-pixel
  bias in the expensive pass every frame.
- `fwidth` is an L1 (`|ddx| + |ddy|`) upper bound -- the very over-estimate RPDB
  avoids with its signed gradient -- so it slightly over-biases (mild haloing
  around texel-width features), which can be tuned down by scaling the
  multiplier.
- It needs a color render target plus a fragment shader in the shadow pass (the
  erhe pass is depth-only today) and, for spot / point lights, storing radial
  distance rather than projected depth.

erhe exposes this as the `distance` value of the `shadow_technique` graphics
preset field (see "Editor integration"); `depth` (the RPDB path above) remains
the default. The first implementation covers directional lights, where the
stored radial distance specializes to the linear light-space depth the
orthographic light projection already produces. Point lights independently use a
true-radial-distance cube map (see [Point-light cube-map shadows](#point-light-cube-map-shadows)
below); that is its own subsystem, not this 2D `shadow_technique` knob.

## Point-light cube-map shadows

Point lights cast **omnidirectional** shadows through a separate cube-map path.
Directional and spot lights use the 2D depth array above; point lights never
touch the fit, the 2D array, or `texture_from_world`. The full design, including
the per-face coordinate flip, is in
[`point_light_shadows.md`](point_light_shadows.md); this is the summary.

- **Storage.** One R32F `texture_cube_map_array` (labelled `Point shadow cube
  array`), `6 * point_shadow_light_count` layers (layer `6*cube + face`, Vulkan
  face order +X,-X,+Y,-Y,+Z,-Z). Each face stores the **raw radial distance**
  from the light -- not projected/non-linear depth, and not normalized by far --
  so neither caster nor receiver needs the far plane; unrendered texels hold a
  large clear (`1e30`) that reads as "lit". `point_shadow_resolution` /
  `point_shadow_light_count` are graphics-preset fields (defaults 512 / 2).
- **Caster** (`Shadow_renderer` point-cube pass, `shadow_renderer.cpp`). For each
  shadow-casting point light, six render passes -- one per face -- rasterize the
  scene into that cube layer from a `create_look_at(light_pos, light_pos +
  look[f], up[f])` camera with a 90-degree perspective
  (`Light::point_light_projection_transforms()`, `perspective_z_far = light->range`). The
  caster fragment (`standard.frag` under `VARIANT_SHADOW_CUBE`) writes
  `length(world_pos - light_position)` to the R32F face (`cull_none`; the light
  world position and far come from `light_control_block`). A shared 2D depth
  scratch is reused for every face, for rasterization only (store `DONT_CARE`).
- **Coordinate flip (convention-driven).** A cube face is sampled by direction
  through the fixed cube-map (s,t) convention, which is vertically inverted
  relative to the framebuffer row order, so the caster needs its clip-space Y
  flipped opposite to the screen pass. This is expressed through the coordinate
  conventions -- the cube pass gets `clip_space_y_flip` enabled iff
  `framebuffer_origin == top_left`, mirroring `Light::get_texture_from_clip` for
  the 2D map. Without it every stored face is mirrored in t and the shadows are
  displaced. A shader-side `gl_Position.y` negate is deliberately not used: it
  is unconditional, and so wrong on bottom_left OpenGL.
- **Receiver.** `sample_point_light_visibility()` (`erhe_light.glsl`) samples
  `s_shadow_cube` with the direction `world_pos - light_pos` at the light's cube
  layer (`shadow_index_packed.y`) and compares the fragment's radial distance
  against the stored nearest-occluder distance with a world-space slope+constant
  bias (`max(0.05, 0.02 * current)`). The forward pass multiplies the point
  light's contribution by this visibility (`standard.frag`). 1.0 = lit.
- **Indexing.** Shadow-casting point lights get a dense `point_shadow_index`
  (cube-array base) in `Light_projections::apply()`, written to
  `shadow_index_packed.y`; their 2D `shadow_index` stays `max()` so the 2D shadow
  loop and 2D receiver sampling skip them for free. Point is the last
  shadow-bearing bucket, so it does not shift directional/spot 2D layer indices.
  A fallback 1x1 cube (cleared to `1e30`) is bound when no real cube exists.
- **No frustum fitting.** The six faces are fixed 90-degree perspectives centered
  on the light; the only tuning is `range` (the cube far plane) and the receiver
  bias. The directional tight-fit pipeline does not apply, and lights exceeding
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
  (`shadow_technique`, Settings window combo) selects `depth` (the RPDB path,
  default) or `distance` (the fwidth-baked distance map). For `distance`,
  `Shadow_render_node` allocates a parallel R32F `texture_2d_array` as a color
  attachment of the shadow passes; the caster (`standard.frag` under
  `VARIANT_SHADOW_DISTANCE`) writes
  `gl_FragCoord.z + coeff*fwidth(gl_FragCoord.z)` into it (`coeff =
  cdd*(1+pcfRadius)`, carried on the per-pass `light_control_block`), and the
  receiver (`erhe_light.glsl`, `ERHE_SHADOW_TECHNIQUE == DISTANCE`) samples
  `s_shadow_distance` and compares with no bias. The bundled `High (distance)`
  preset enables it. This 2D `distance` knob covers directional lights only; spot
  lights stay on the 2D depth path. Point lights are unaffected by it -- they
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
| `src/erhe/scene_renderer/erhe_scene_renderer/shadow_renderer.cpp` | Shadow pass, caster gathering, depth clamp pipeline, scissor border; distance-technique variant (color writes + `VARIANT_SHADOW_DISTANCE` + bias coeff); point-light cube pass (6 faces, `VARIANT_SHADOW_CUBE`, framebuffer_origin-gated `clip_space_y_flip`) |
| `src/erhe/scene_renderer/erhe_scene_renderer/light_buffer.cpp` | `Light_projections::apply()`, light UBO, shadow samplers; `s_shadow_distance` + distance fallback, `shadow_distance_bias_coeff` control field; `point_shadow_index` assignment, `point_light_position`, `shadow_cube_texture` + 1x1 fallback cube |
| `src/erhe/scene_renderer/erhe_scene_renderer/program_interface.cpp` | Bind group layout: `s_shadow_compare` / `s_shadow_no_compare` (depth) + `s_shadow_distance` (color) + `s_shadow_cube` (R32F cube array) sampler bindings |
| `res/shaders/erhe_light.glsl` | Shadow sampling: RPDB depth path + distance (unbiased) path, reference depth clamp; `sample_point_light_visibility` (cube direction sample, radial compare) |
| `src/erhe/scene_renderer/test/shadow_gpu_test_fixture.hpp` | Shadow GPU test fixture: station, poses, shadow map / forward / Shadow_tie renders |
| `src/erhe/scene_renderer/test/test_shadow_gpu.cpp` | Shadow sampling GPU test cases |
| `src/erhe/scene_renderer/test/shaders/shadow_tie.frag` | Shadow_tie fragment pass (reference depth offset per band) |
| `res/shaders/standard.frag` | Caster: `VARIANT_SHADOW_DISTANCE` writes the fwidth-biased light-space depth to the distance map; `VARIANT_SHADOW_CUBE` writes radial distance to the cube face; point receiver multiplies `sample_point_light_visibility` |
| `res/shaders/standard.vert` | `VARIANT_SHADOW_CUBE` passes `v_position` (world) to the cube caster fragment |
| `src/editor/rendergraph/shadow_render_node.cpp` | Editor wiring: settings refresh, fit camera override, technique-aware distance-map allocation + color attachment; point cube array + per-face render passes (`reconfigure` on `point_shadow_resolution` / `point_shadow_light_count`) |
| `src/editor/tools/debug_visualizations.cpp` | Shadow fit debug visualization |
| `src/editor/config/definitions/shadow_frustum_fit_config.py` | Frustum fit settings codegen definition |
| `src/editor/config/definitions/shadow_technique_mode.py` | Shadow technique enum codegen definition (depth / distance) |
| `doc/erhe/point_light_shadows.md` | Point-light cube shadow design, per-face coordinate flip, risks and tuning knobs |
| `doc/erhe/shadow_tight_fit.md` | Cost model and standing optimizations of the directional fit |

## Future work

- [plans/shadow_robustness.md](../plans/shadow_robustness.md) - bias hardening, shadow test scenes and automated shadow verification.
- [plans/shadows.md](../plans/shadows.md) - remaining fit and point-shadow performance candidates.
