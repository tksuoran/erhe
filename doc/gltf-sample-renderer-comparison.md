# erhe scene / editor vs. the Khronos glTF-Sample-Renderer

A feature-by-feature comparison of `erhe::scene` + `erhe::gltf` + the editor
against the Khronos reference implementation
([glTF-Sample-Renderer](https://github.com/KhronosGroup/glTF-Sample-Renderer),
read at `/d/glTF-Sample-Renderer`), written 2026-08-20.

Scope of the comparison: scene structure, node hierarchy, animation, skinning,
cameras, lights, and the loader / exporter. Vertex and fragment shading is out
of scope, and so are glTF extensions erhe does not implement — except where an
extension is accepted by the parser but not acted on, which is a defect rather
than a gap.

The reference is a viewer, not an editor: it has no export, no undo, no
physics, no raytracing and no XR, so most of erhe is simply outside the
comparison. What the reference *is* good for is being a second, independently
written reading of the spec — every defect below was found by diffing behavior
against it, and the specification text is the authority for each one.

## Functionality map

| Area | glTF-Sample-Renderer | erhe | Verdict |
|---|---|---|---|
| glTF parse | own JSON parser (`source/gltf/*.js`) | fastgltf | ✅ |
| Scenes | all scenes, `state.sceneIndex`, honors `json.scene` | only `scenes.front()` | see [Rejected](#rejected) |
| Node hierarchy, TRS + matrix | `node.js`, `applyMatrix()` decomposes | `parse_node_transform`, `Trs_transform` | ✅ erhe keeps a real TRS |
| World transform propagation | full re-walk with dirty flags | dirty-list driven `Scene::update_node_transforms` | ✅ |
| Skinning | `skin.js`, joint texture, inverse bind matrices | `Skin_data::get_world_from_bind`, `Joint_buffer` | ✅ |
| Animation TRS | STEP / LINEAR / CUBICSPLINE + slerp | same | ✅ since `3aa6d8b47` |
| Animation, multiple active | `animationIndices[]` | one animation in `Animation_player` | ⚠ gap |
| Animation, rest pose | `AnimatableProperty.restValue` + `resetAnimatedProperties()` | applies destructively into `parent_from_node` | ⚠ gap |
| Morph targets / weights | full (`primitive.js`, `WEIGHTS` channels) | none, anywhere | ⚠ gap |
| KHR_animation_pointer | yes (JSON-pointer targets) | no | gap, out of scope |
| Cameras | perspective (incl. infinite zfar) + orthographic, honors `aspectRatio` | `erhe::scene::Projection` | ✅ since `a1f62499d` |
| Punctual lights | directional / point / spot, `range = -1` is infinite | `erhe::scene::Light` | ⚠ `range` default |
| Primitive modes | POINTS / LINES / STRIP / FAN / TRIANGLES | `primitive.mode` ignored | ⚠ [deferred](#deferred) |
| Non-indexed primitives | supported | skipped with an error | ⚠ gap |
| Sparse accessors | `accessor.js`, full support | logs an error, loads base view | ⚠ gap |
| Draco | yes | disabled in both extension masks | ⚠ gap |
| EXT_mesh_gpu_instancing | builds instance matrices | expands into child nodes | ✅ since `05a1a6f9c` |
| KHR_materials_variants | yes | none | ⚠ gap |
| KHR_texture_transform | yes | yes (`Material_texture_sampler`) | ✅ |
| KHR_mesh_quantization / EXT_meshopt_compression | yes | yes (via fastgltf) | ✅ |
| `material.doubleSided` | yes | yes | ✅ since `295405364` |
| Blend sorting | `sortPrimitivesByDepth()` back-to-front | separate translucent pass, no depth sort | ⚠ gap |
| Picking / hover | picking-color renderer, `KHR_node_*ability` | `Id_renderer`, richer selection | ✅ |
| Editing, undo, brushes, physics, raytrace, XR, MCP, export | — | yes | erhe only |

## Fixed

### Animation — `3aa6d8b47`

Three of the glTF interpolation rules were implemented wrongly in
`Animation_sampler::evaluate()`:

- **STEP was interpolated.** The function only branched on `CUBICSPLINE`, so
  `STEP` fell into the `LINEAR` mix / slerp and every step-keyed animation
  played smoothed. Compare `interpolator.js`, which has an explicit
  `InterpolationModes.STEP` case.
- **CUBICSPLINE ignored the keyframe delta.** glTF 2.0 appendix C scales both
  tangent terms by `t_d`, and the stored tangents are per-second derivatives,
  so leaving it out is only correct when keyframes happen to be one second
  apart. At 30 fps sampling the tangent contribution came out 30× too large.
  Compare `interpolator.js`: `const a = keyDelta * output[...]`.
- **CUBICSPLINE rotation returned an unnormalized quaternion.** The normalized
  value was computed into a local and then discarded; a component-wise spline
  of quaternions is not a rotation. The two slerp endpoints on the `LINEAR`
  path are now normalized too, since rotation output accessors may be
  normalized byte / short, which does not round-trip to unit length.

Animation data comes from files, so the malformed-input paths were hardened at
the same time: the strictly-increasing-timestamps `ERHE_VERIFY`s became a
keyframe hold, an empty or too-short sampler returns a per-path identity value
(identity quaternion for rotation, 1 for scale — not 0, which would collapse
the node), `seek()` clamps a `start_position` left over from longer sampler
data, and `get_first_time()` / `get_last_time()` skip empty samplers instead of
calling `front()` / `back()` on them.

Covered by `src/erhe/scene/test/test_animation_sampler.cpp` (all three modes
plus each malformed path); the `t_d` test fails on the previous code.

### Cameras — `a1f62499d`

- **Orthographic cameras came in 2× too zoomed.** glTF `xmag` / `ymag` are
  half extents — the reference builds the projection as `1/xmag` along X, so
  the view spans `[-xmag, xmag]` — while erhe's `ortho_width` / `ortho_height`
  are full extents and `Projection::Type::orthogonal` uses
  `±0.5 * ortho_width`. Import now doubles them and export halves them. The
  export mirrored the import, so erhe-to-erhe round-trips hid this; anything
  else did not.
- **An absent `zfar` produced a broken frustum.** It means an infinite far
  plane; import stored `0.0f`, which is not caught by `create_frustum()`'s
  degenerate check and yields a frustum with the far plane in front of the
  near one. Only content-fit widening in `parsers/gltf.cpp` accidentally
  rescued it, and only when a fit was computed.
- **`perspective.aspectRatio` was parsed, logged and thrown away**, so a
  camera with a fixed aspect ratio silently adopted the viewport's. The export
  side computed `aspectRatio` as `fov_x / fov_y` — a ratio of angles, not an
  aspect ratio.

`Projection` gained `infinite_z_far` for the second of these. `z_far` stays a
finite, meaningful depth hint, because that is what the rest of the editor
reads as a number (shadow range fitting, the transform tool's gizmo distance,
the properties slider); only the projection matrix goes unbounded.

Worth knowing if you touch this: **erhe produces reverse depth by handing the
finite builders `z_far` and `z_near` the other way round**, which has no
meaning when the far plane is at infinity. The `create_*_infinite_far()`
builders therefore take `reverse_depth` as a parameter and derive the depth row
directly. With `z_clip = c·z_view + d` and `w_clip = -z_view`:

| `Depth_range` | forward | reverse |
|---|---|---|
| `zero_to_one` | `c = -1`, `d = -z_near` | `c = 0`, `d = z_near` |
| `negative_one_to_one` | `c = -1`, `d = -2·z_near` | `c = 1`, `d = 2·z_near` |

An authored `aspectRatio` now maps to `Projection::Type::perspective` (both fov
angles, viewport aspect ratio ignored) with
`fov_x = 2·atan(aspect · tan(fov_y/2))`; export uses the exact inverse,
`aspect = tan(fov_x/2) / tan(fov_y/2)`. `infinite_z_far` round-trips through
[`ERHE_camera`](gltf_extensions/ERHE_camera.md), export omits the core `zfar`
when it is set, and the camera properties get a matching checkbox.

Covered by the `CreateFrustumInfiniteFarReverse` and
`CreatePerspective*InfiniteFar` cases in `src/erhe/math/test/test_projection.cpp`.

### Double-sided materials — `295405364`

`material.doubleSided` had no representation in erhe at all, and every content
pipeline is `cull_mode_back_ccw`, so a double-sided material — foliage, cloth,
most of the Khronos alpha-blend sample assets — rendered with its back faces
missing.

`Material_data::double_sided` (default `false`, matching the glTF default of
culling back faces) reaches the rasterizer through a `disable_face_culling`
variant in `Base_render_pipeline::get_pipeline_for()`, built on
`Rasterization_state::with_face_culling_disabled()`. It partitions draws in
both renderer paths: `Render_bucket` for the classic per-pass bucketing and
`Draw_list_key` for the persistent draw lists, alongside the existing
negative-determinant split.

Draw lists capture their partitioning at register time, so an in-place edit of
a material already in use needs them rebuilt; that hook went into
`Material_change_operation` (execute and undo), which also covers
`blending_mode` and `bxdf_model`, where the same staleness was already
possible.

The shadow pass is deliberately untouched: it culls per the graphics preset's
`Shadow_cull_mode` (the peter-panning trick), which is a global choice rather
than a per-material one. Revisit if thin double-sided geometry turns out to
drop shadows in practice.

`double_sided` rides the core glTF `material.doubleSided`, so it does **not**
appear in [`ERHE_material`](gltf_extensions/ERHE_material.md).

### EXT_mesh_gpu_instancing — `05a1a6f9c`

The extension was listed in both fastgltf `Extensions` masks, so files using it
parsed "successfully", but `Node::instancingAttributes` was never read: an
instanced node silently drew exactly one instance at the node transform.

erhe has no instanced draw path, so `parse_node()` expands the instances into
child nodes carrying one mesh clone each. The extension's instance transform is
a local transform applied before the node's own, which is exactly what a child
node is, so the result matches the reference's `instanceMatrices`. TRANSLATION
/ ROTATION / SCALE are decoded through fastgltf, which dequantizes the
normalized byte / short rotation accessors the extension allows. Instancing is
ignored, with a warning, on a skinned mesh node — the extension forbids the
combination, and the clone-per-instance expansion would attach the skin to only
one clone.

The instance count is logged, because the expansion costs one node and one mesh
clone per instance. A large instanced asset stays heavy until there is a real
instanced draw path.

### Unsupported vertex attribute formats — `8418f13a5`

`to_erhe_attribute()`'s per-accessor-type cases each ended with an inner
`default: break` and no outer `break`, so an unrecognized component type fell
through into the next accessor-type case. Reaching the end of the function then
meant `ERHE_FATAL` — taking down the process over one malformed attribute in an
imported file. It now logs the accessor type / component type it could not
express and returns `format_undefined`; the caller drops the primitive, the way
it already does for non-indexed ones. (Keeping the primitive is not an option:
an undefined attribute has zero size and would desynchronize every following
attribute offset.)

## Rejected

Two findings from the comparison were examined and deliberately not acted on.

- **Only `scenes.front()` is parsed; `asset.defaultScene` is ignored.** The
  pre-scan in `Gltf_scan` does use `defaultScene`, so the two disagree, and a
  skin whose joints live outside scene 0 hits an `ERHE_VERIFY` in
  `parse_skin()`. Not fixed: glTF 2.1 deprecates more than one scene per file,
  so single-scene parsing is the direction of travel rather than a defect.
- **Generated tangent `w` sign.** `gltf_fastgltf.cpp` writes
  `is_orientation_preserving ? -1.0f : 1.0f`, the opposite of MikkTSpace's own
  convention and of erhe's other caller,
  `geometry_tangents.cpp` (`bIsOrientationPreserving ? 1.0f : -1.0f`). Not
  changed: the rendering result has been verified by eye against the sample
  renderer and is consistent. The two callers still disagree, so if normal-map
  handedness ever looks wrong on glTF content without authored tangents, this
  is the first place to look.

## Deferred

- **`primitive.mode` is ignored.** `to_erhe(fastgltf::PrimitiveType)` exists
  and is never called; `Triangle_soup::primitive_type` is never assigned by the
  loader. POINTS / LINES / LINE_STRIP / LINE_LOOP / TRIANGLE_STRIP /
  TRIANGLE_FAN primitives are all reinterpreted as an indexed triangle list,
  with no diagnostic. Needs the primitive type plumbed through `Triangle_soup`
  and the renderers.

## Remaining gaps

Not defects — features the reference has and erhe does not. Roughly in order of
how often they bite on real assets:

- **Morph targets** — absent end to end (mesh, buffers, animation). The
  exporter already warns when it drops a weights channel. Note that
  `Animation_path::WEIGHTS` exists but is inert: `get_component_count()`
  returns 0 for it and the loader has no `Scalar` case for sampler output, so
  weights channels load as zeros and apply as a no-op.
- **Sparse accessors** — `copyFromAccessorWithOutStride()` logs an error and
  loads the base buffer view; the reference applies the sparse override.
- **Non-indexed primitives** — dropped with an error (documented at the call
  site in `parse_primitive`).
- **Draco** — commented out in both extension masks.
- **KHR_materials_variants** — no representation.
- **Multiple simultaneous animations** — `Animation_player` holds one; the
  reference plays a set (`state.animationIndices`).
- **Rest pose / non-destructive animation** — the reference keeps `restValue`
  per animatable property and calls `resetAnimatedProperties()` every frame;
  erhe writes the sampled pose permanently into `parent_from_node`, so
  scrubbing a timeline destroys the imported bind pose with no way back. This
  matters more for an editor than it does for the viewer it was copied from.
- **Blended-geometry depth sorting** — erhe has the opaque / translucent pass
  split but no back-to-front ordering inside the translucent pass.
- **`KHR_lights_punctual` range** — `parse_light()` substitutes `1000.0f` for
  an absent `range`; the spec (and the reference's `-1`) means infinite.
  Already marked TODO in the code.
- **MikkTSpace vertex splitting** — the glTF path writes per-face-vertex
  tangents into shared vertices (last face wins) rather than splitting on
  tangent discontinuities, so UV-seam vertices get whichever face ran last.

## Redoing this comparison

The reference files worth reading, and their erhe counterparts:

| Reference | erhe |
|---|---|
| `source/gltf/animation.js`, `interpolator.js` | `erhe_scene/animation.cpp` |
| `source/gltf/node.js`, `scene.js` | `erhe_scene/node.cpp`, `scene.cpp` |
| `source/gltf/skin.js` | `erhe_scene/skin.cpp` |
| `source/gltf/camera.js` | `erhe_scene/projection.cpp` |
| `source/gltf/light.js` | `erhe_scene/light.cpp` |
| `source/gltf/primitive.js`, `accessor.js` | `erhe_gltf/gltf_fastgltf.cpp` |
| `source/gltf/material.js` | `erhe_gltf/gltf_fastgltf.cpp`, `erhe_primitive/material.hpp` |
| `source/GltfState/gltf_state.js` | the editor's viewport / settings |

`source/gltf/animatable_property.js` is the piece with no erhe counterpart at
all, and it is what the rest-pose gap above is about.
