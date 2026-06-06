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
2. `Shadow_renderer::render()` (erhe::scene_renderer) gathers shadow caster
   bounds when `fit_to_casters` is enabled, then calls
   `Light_projections::apply()`, which invokes
   `Light::projection_transforms()` per light to compute the light camera
   pose, projection, `clip_from_world` and `texture_from_world`.
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
| `optimize_rotation` | off | Rotating calipers roll around the light direction for minimum covered area |
| `near_from_main_frustum` | off | Pull the near plane down to the view frustum extent (needs `depth_clamp`) |
| `depth_clamp` | off | Depth-clamp rasterization in the shadow pass (caster "pancaking") |
| `texel_snap` | on | Snap the light space box to shadow map texels |
| `quantize_extents` | off | Round the box size up to multiples of `quantize_step` |
| `quantize_step` | 0 | World units; 0 derives `2 * shadow_range / 16` |
| `cap_by_shadow_range` | on | Never exceed the stable shadow-range box extents |
| `collect_debug` | off | Record per-step intermediates for the debug visualization |

Pipeline order, with the box recorded per step for debugging
(`Shadow_fit_step`): fit points -> frustum constraint -> shadow range cap ->
stabilization.

**fit_to_casters.** `Shadow_renderer::render()` gathers the 8 world space
AABB corners of every visible shadow-casting mesh. The fit builds a 3D convex
hull around them (`calculate_bounding_convex_hull`), then clips the hull to
the shadow caster volume F_shadow (`build_shadow_caster_volume_planes` +
`clip_convex_hull_points_by_planes`, Sutherland-Hodgman per hull triangle).
F_shadow is the main camera view frustum extruded toward the light: the
F_main planes whose inward normals face away from the light are dropped, so
the volume is open toward the light and contains every caster that can
shadow anything visible. Because clipping only produces points on the hull
surface, F_main corners that lie inside the hull are added back (a single
large caster enclosing the whole view frustum would otherwise lose the pure
plane-plane-plane intersection vertices). If the clip produces no points and
`fit_to_view_frustum` is off, the fit falls back to the stable path.

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

- **Caster point gathering** - when `fit_to_casters` is on, the world AABB
  corners of every mesh passing the shadow filter (visible + shadow_cast) are
  collected before `Light_projections::apply()`; the points are copied into
  frame-lifetime storage inside `Light_projections`.
- **Front-face culling** - only back faces write shadow depth, which reduces
  peter-panning for closed meshes (open / single-sided meshes do not cast
  from their front side). Mirrored (negative-determinant) buckets use the
  front-face-flipped pipeline variant; see "Mirrored (negative-determinant)
  geometry" in `editor_rendering.md`.
- **Depth clamp pipeline** - `Shadow_frustum_fit_settings::depth_clamp`
  selects `m_pipeline_depth_clamp`, which uses
  `Rasterization_state::cull_mode_front_ccw_depth_clamp`: the same
  front-face culling and y-flip winding compensation as the regular shadow
  pipeline, plus depth clamping. The preset exists so that toggling
  `depth_clamp` changes ONLY depth clamping, never the culling behavior
  (and so the per-bucket winding flip stays effective).
- **One texel empty border** - the pass sets a scissor rectangle inset by one
  pixel on each side, keeping the outermost texel ring at the clear value.
  Combined with the shadow samplers' `clamp_to_edge` address mode, any
  shadow lookup outside the map's XY range compares against the far clear
  value and resolves to fully lit. This is what makes out-of-XY receivers
  safe without any range check in the shader.
- **Prewarm** - `prewarm_pipelines()` warms the full pipeline matrix: both
  base pipelines (regular and depth clamp) times both winding variants, so
  neither toggling `depth_clamp` nor mirroring a mesh hitches at first draw.

## Shadow sampling

`sample_light_visibility()` in `res/shaders/erhe_light.glsl`:

- The fragment's world position is transformed by the light's
  `texture_from_world` into [0,1] texture space plus depth.
- The comparison sampler (`s_shadow_compare`) bakes a NON-STRICT comparison
  at engine init from the reverse-depth convention: `greater_or_equal` for
  reverse-Z, `less_or_equal` for forward-Z (`light_buffer.cpp`). 1.0 = lit.
- A slope-scaled bias is derived from screen space derivatives of the light
  texture coordinates (inverting the 2x2 Jacobian to get dz/dUV), based on
  https://renderdiagrams.org/2024/12/18/shadowmap-bias/ . The reference depth
  is then rounded direction-aware to 16-bit depth precision (toward the near
  plane) before the hardware comparison.
- Three comparison paths exist in the file; the hardware depth comparison
  path is the one enabled. The two disabled paths (textureGather PCF, shader
  comparison) use the same non-strict comparison semantics as the hardware
  sampler so they stay drop-in correct.

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

## Editor integration

- **Settings** - `Shadow_frustum_fit_config`
  (`src/editor/config/definitions/shadow_frustum_fit_config.py`, generated
  via erhe_codegen) mirrors `Shadow_frustum_fit_settings` 1:1 and is edited
  in the Settings window; it persists in `editor_settings.json`
  (`Editor_settings_config` version 4). `Shadow_render_node` re-reads it
  every frame, so changes apply live.
- **Fit target camera override** - the Scene View Config window (also
  openable from the viewport toolbar popup) has "Override Shadow Fit Target
  Camera": when set, the fit (and its debug data) targets that camera
  instead of the viewport camera, so the fit can be observed from outside
  its own frustum while flying around. Ignored if the camera is deleted or
  belongs to another scene.
- **Debug visualization** - with the `collect_debug` setting on, the fit
  records per-light intermediates (`Shadow_frustum_fit_debug_data`): caster
  AABB points, caster hull, F_shadow planes, receiver (view frustum)
  corners, fit point set, light plane 2D hull, rotating calipers OBB, and
  the light space box after each pipeline step. `Debug_visualizations` draws
  them under its "Shadow Fit" toggle (independent of the "Shadow Debug"
  shadow texel visualization), with per-element toggles, colors and line
  widths (negative widths are in pixels and do not scale with distance).
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
| `src/erhe/math/erhe_math/math_util.cpp` | Convex hull clip, point-in-hull, rotating calipers, F_shadow planes |
| `src/erhe/scene_renderer/erhe_scene_renderer/shadow_renderer.cpp` | Shadow pass, caster gathering, depth clamp pipeline, scissor border |
| `src/erhe/scene_renderer/erhe_scene_renderer/light_buffer.cpp` | `Light_projections::apply()`, light UBO, shadow samplers |
| `res/shaders/erhe_light.glsl` | Shadow sampling, bias, reference depth clamp |
| `src/editor/rendergraph/shadow_render_node.cpp` | Editor wiring: settings refresh, fit camera override |
| `src/editor/tools/debug_visualizations.cpp` | Shadow fit debug visualization |
| `src/editor/config/definitions/shadow_frustum_fit_config.py` | Settings codegen definition |
