# Shadow Mapping

This document covers shadow mapping in erhe: the directional light shadow
frustum fitting (the stable baseline fit and the modular tight fit added in
June 2026), the shadow pass mechanics that support it, the shadow sampling
shader (including the receiver depth range fix), and the editor integration
(settings, debug visualizations, fit target camera override).

For the wider rendering architecture (rendergraph, composer, forward pass,
winding variants) see [`editor_rendering.md`](editor_rendering.md); its
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

Spot lights use a single perspective shadow map built directly from the light
node pose (`Light::spot_light_projection_transforms()`); point lights
currently fall through to the same path. All of the frustum fitting below
applies to directional lights only.

## Directional light frustum fitting

### Light space conventions

The fit works in a light-aligned frame: x / y span the light plane
(perpendicular to the light direction), and `s = dot(p_in_light,
light_direction)` increases TOWARD the light (the light direction is the
light node +Z, pointing from the scene toward the light). The fitted box's
`s_max` face (light-most) becomes the near plane of the orthographic light
projection (`z_near = 0`); the `s_min` face becomes the far plane
(`z_far = s_max - s_min`). The light camera sits centered on the `s_max`
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
| `fit_to_casters` | off | Fit to the shadow caster convex hull clipped to the shadow caster volume F_shadow |
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
   two consumers (cull vs hull clip) have different needs - reusing one set for
   both is what made the volume collapse to a single plane:
   - `build_shadow_caster_volume_planes` keeps the F_main planes whose inward
     normals do not face away from the light; planes facing the light are
     dropped, leaving an **open** volume (no lateral closure). This collapses
     to a single plane when the light is near-antiparallel to the view (e.g. a
     top-down camera under a straight-up light), since only the far face then
     survives. Used only to clip the caster hull.
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
3. Expands the surviving AABBs to corners, builds a 3D convex hull
   (`calculate_bounding_convex_hull`), and clips the hull to the **open**
   F_shadow (`clip_convex_hull_points_by_planes`, Sutherland-Hodgman per hull
   triangle) - trimming the parts of straddling casters that stick out below
   the volume. The hull is already bounded laterally by the surviving casters,
   so the open set (not `filter_planes`) is used here. Because clipping only
   produces points on the hull surface, F_main corners that lie inside the
   hull are added back (a single large caster enclosing the whole view frustum
   would otherwise lose the pure plane-plane-plane intersection vertices).

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
  expansion to hull points happen inside the fit, not here (the gather is
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
- A slope-scaled bias is derived from screen space derivatives of the light
  texture coordinates (inverting the 2x2 Jacobian to get dz/dUV), based on
  https://renderdiagrams.org/2024/12/18/shadowmap-bias/ . For a fixed-point
  (UNORM) shadow map the reference depth is then rounded direction-aware to the
  format's depth precision (toward the near plane) before the hardware
  comparison; a floating-point (D32_SFLOAT) map skips the snap. The format is
  carried by the `ERHE_SHADOW_DEPTH_BITS` compile-time variant axis.
- The filter is a compile-time variant (`ERHE_SHADOW_FILTER`, set from the
  graphics preset's `Shadow_filter_mode`): `hard` does a single hardware
  comparison-sampler fetch against the snapped reference; `pcf_2x2` does one
  `textureGather` on the non-comparison sampler with bilinear weighting; the
  wide `pcf_4x4` / `pcf_6x6` paths do `(K/2)^2` gathers and average. The gather
  paths use the same non-strict `gequal` / `lequal` semantics as the hardware
  sampler, so all three variants agree.

### Receivers outside the fitted depth range (and the bug this fixed)

The sampler used to begin with an early-out:

```glsl
if (position_in_light_texture.z <= 0.0) {
    return 1.0;
}
```

With reverse-Z, light texture depth 0.0 IS the far plane, so this forced any
receiver beyond the light-space far plane to fully lit before the depth
comparison ever ran. The check predates the tight fit: with the stable fit
the far plane approximates the shadow range, and "beyond the shadow range =
no shadow" was a tolerable policy. With `fit_to_casters` the far plane hugs
the caster bounds, so every visible receiver below the lowest caster (the
classic floor under floating objects) hit the early-out and lost its shadow,
regardless of the other fit settings.

The fix removes the early-out and instead clamps the comparison reference
depth to [0, 1] after biasing. This is geometrically correct, not a
workaround:

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

## Bias technique: RPDB reference, and the distance/fwidth alternative

erhe's receiver-side bias is the receiver-plane depth bias (RPDB) method from
https://renderdiagrams.org/2024/12/18/shadowmap-bias/ (the shader cites it at
`res/shaders/erhe_light.glsl:84`). This section records how the implementation
maps onto that reference, where it goes beyond it, the known deltas, and the
alternative "bias-free" technique exposed as the `distance` shadow technique.

### What erhe implements today (RPDB)

`sample_light_visibility()` recovers the receiver's light-space depth gradient
`dz/dUV` by inverting the 2x2 screen-space Jacobian of the light texture
coordinates (`erhe_light.glsl:97-105`), then offsets the comparison reference
toward the texel the shadow map actually stored:

- Hard (`ERHE_SHADOW_FILTER == 0`): a single slope bias toward the texel center,
  a direction-aware precision snap, then the hardware comparison sampler
  (`:112-126`).
- 2x2 (`== 2`): one `textureGather`, the same center-relative slope bias per
  corner, bilinearly weighted (`:127-176`).
- Wide KxK (`>= 4`): `(K/2)^2` gathers; the `ERHE_SHADOW_BIAS` axis picks either
  `receiver_plane` (signed per-tap `dot(texel_uv_offset, dz_dUV)`, independent of
  kernel size so the contact shadow stays attached) or the legacy one-sided
  `slope_scaled` bias (`:177-234`).

This matches the article's core idea: a signed receiver-plane gradient extended
to PCF with a per-texel bias. The hard and 2x2 paths always use their own fixed
bias and ignore the `ERHE_SHADOW_BIAS` axis; only the wide paths switch on it.

erhe also goes beyond the article:

- Reverse-Z aware throughout (`cdd = clip_depth_direction` drives the bias sign,
  the rounding direction, and the non-strict `gequal` / `lequal` comparison).
- Receivers outside the fitted depth range are handled by clamping the reference
  to [0, 1] after biasing instead of an early-out (see "Receivers outside the
  fitted depth range" above).
- Filter, bias, technique, and shadow depth format are compile-time shader
  variants (`SHADOW_FILTER`, `SHADOW_BIAS`, `SHADOW_TECHNIQUE`,
  `SHADOW_DEPTH_BITS` in `shader_key.hpp`), selected from the graphics preset
  rather than branched at runtime. The hard path's precision snap is derived
  from `SHADOW_DEPTH_BITS` -- the format's `2^bits - 1` levels for a UNORM map,
  skipped for D32_SFLOAT -- so it matches the actual shadow map format.
- A complementary caster-side rasterizer depth bias (constant + slope) exists
  (`shadow_depth_bias_constant` / `_slope`, applied in `shadow_renderer.cpp` via
  `set_depth_bias`) but defaults to 0. The article criticizes rasterizer slope
  bias as an over-estimate, so erhe relies on RPDB by default.

### Deltas / open issues versus the reference

- Unexplained 2.0 bias scale. The `2.0 *` factor (`:119,152-155,197,224`, flagged
  in the comment at `:91-92`) has no counterpart in the article. It most likely
  compensates a half-texel-versus-full-texel footprint or a sign subtlety and
  should be pinned down rather than left as a fudge.
- Surface normal unused. `sample_light_visibility(..., float N_dot_L)` receives
  the geometric term but the bias path never uses it. The article notes that a
  geometric slope (from the surface normal) is more robust than `ddx` / `ddy` at
  discontinuities; erhe has the normal available and could fall back to it.
- Degenerate Jacobian untreated. When `detJ == 0` (grazing / silhouette texels)
  `dz_dUV` is left at zero -- no bias -- which can reintroduce acne where it is
  worst. The article assumes an invertible Jacobian.
- `ddx` / `ddy` across geometry edges are unreliable for both methods (a 2x2 quad
  straddling two surfaces); erhe applies no mitigation. This is a shared RPDB
  limitation.

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
orthographic light projection already produces.

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
  preset enables it. Directional lights only for now -- point / spot stay on the
  depth path (extending them needs true radial distance; see the bias technique
  section above).
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
| `src/erhe/scene/erhe_scene/light.cpp` | Stable fit, fit dispatch, transform assembly |
| `src/erhe/scene/erhe_scene/light.hpp` | `Shadow_frustum_fit_settings`, `Light_projection_parameters` |
| `src/erhe/scene/erhe_scene/light_frustum_fit.cpp` | Tight modular fit pipeline |
| `src/erhe/scene/erhe_scene/light_frustum_fit.hpp` | `Shadow_frustum_fit_debug_data`, `Shadow_fit_step` |
| `src/erhe/math/erhe_math/math_util.cpp` | Convex hull clip, point-in-hull, rotating calipers, F_shadow open planes (`build_shadow_caster_volume_planes`) + silhouette side planes (`build_shadow_caster_silhouette`), AABB-vs-convex-volume cull (`aabb_in_convex_volume`), half-space intersection to polyhedron (`convex_polyhedron_from_planes`) |
| `src/erhe/scene_renderer/erhe_scene_renderer/shadow_renderer.cpp` | Shadow pass, caster gathering, depth clamp pipeline, scissor border; distance-technique variant (color writes + `VARIANT_SHADOW_DISTANCE` + bias coeff) |
| `src/erhe/scene_renderer/erhe_scene_renderer/light_buffer.cpp` | `Light_projections::apply()`, light UBO, shadow samplers; `s_shadow_distance` + distance fallback, `shadow_distance_bias_coeff` control field |
| `src/erhe/scene_renderer/erhe_scene_renderer/program_interface.cpp` | Bind group layout: `s_shadow_compare` / `s_shadow_no_compare` (depth) + `s_shadow_distance` (color) sampler bindings |
| `res/shaders/erhe_light.glsl` | Shadow sampling: RPDB depth path + distance (unbiased) path, reference depth clamp |
| `res/shaders/standard.frag` | Caster: `VARIANT_SHADOW_DISTANCE` writes the fwidth-biased light-space depth to the distance map |
| `src/editor/rendergraph/shadow_render_node.cpp` | Editor wiring: settings refresh, fit camera override, technique-aware distance-map allocation + color attachment |
| `src/editor/tools/debug_visualizations.cpp` | Shadow fit debug visualization |
| `src/editor/config/definitions/shadow_frustum_fit_config.py` | Frustum fit settings codegen definition |
| `src/editor/config/definitions/shadow_technique_mode.py` | Shadow technique enum codegen definition (depth / distance) |
