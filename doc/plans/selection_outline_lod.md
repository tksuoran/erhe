# Selection outline at the drawn LOD level

Status: in progress

Makes the selection outline trace the LOD level a mesh is drawn with
(`doc/plans/lod.md`). Found while testing LOD step 3: with a forced level,
the selected fill draws the level while the outline still traces the
full-detail mesh. The outline keeps its current method (wide-line edge
ribbons masked by the selection silhouette); what changes is that every
generated LOD level gets its own edge lines (`doc/plans/lod.md` D2), and the
silhouette mask is drawn at the same level as the fill.

## State (2026-10-31)

Branch `lod` (worktree `.claude/worktrees/lod`), rebased onto the local
`render-thread` and force-pushed to `origin/lod` (6c4891b16). Each step is
reviewed by an independent Fable agent (medium effort) before its commit and
before the next step starts (user rule); the user tests interactively when
the editor is ready (launch the windowed `build_vs2026_vulkan` Debug
editor, stop it afterwards).

- Step 1 DONE (d6b0d9c29): per-level edge lines.
- Step 2 DONE (7046e0b13, revised 6c4891b16): the feed uses the mesh's
  forced level; shader debug never changes edge lines or the outline (user
  decision). User verified.
- Shader debug fills draw through the draw lists since 6d2a0cc37 (from
  `render-thread`), so a shader debug view draws the forced level too.
  User verified.
- NEXT: step 2b, then step 3, then step 4.

Verification recipe (windowed editor, MCP on 127.0.0.1:3743, read the port
from `logs/log.txt`): `create_shape` torus `{"position":[0,0.8,-4],
"scale":2.0,"motion_mode":"none"}` (in front of Camera A at (0, 1.3, 4)),
`generate_lods`, `select_items`, `set_item_property` `Lod.apply_lod` true /
`Lod.mode` Forced / `Lod.forced_level` 4, `capture_screenshot`;
`push_shader_debug {"shader_debug":7}` / `pop_shader_debug {}` for the
debug case. `get_lod_info` reports `edge_count` per level.

## 1. How the outline is drawn now

The outline is two parts that meet in stencil bit 7 (the selection
silhouette):

1. **The silhouette mask.** Bit 7 is written by whichever pass draws the
   selected meshes' triangles:
   - "Content fill selected" and "Content fill alpha tested selected" -
     on the draw lists, so they draw the forced LOD level.
   - "Selection stencil mask (fill disabled)" and "Selection stencil mask
     (proxy hidden)" (`src/editor/app_rendering.cpp`) - on the bucket path,
     because they set `color_blend_override` (color writes off) and force
     `VARIANT_DEPTH_ONLY`, neither of which a draw-list color pass supports
     (`doc/erhe/draw_list_renderer.md`, the pass routing table). The bucket
     path draws level 0.
2. **The outline itself.** "Content outline opaque selected" is an
   `edge_lines` pass in content wide-line group 2: `Viewport_scene_view`
   (and `Headset_view` for the headset) feeds each selected or hovered mesh
   to `Content_wide_line_renderer::prepare_mesh()`, which reads the
   primitive's **original** variant's `edge_line_vertex_buffer_range` and
   expands every edge into a screen-space ribbon
   (`compute_before_content_line.comp`) of the pulsing selection width. The
   `outline` pipeline draws the ribbons with depth off where bit 7 is clear
   and sets bit 7. The ribbon halves outside the silhouette form the
   outline; where the selected mesh is occluded (no bit 7 there), its edges
   draw over the occluder as an x-ray wireframe (kept as is, user decision
   O2).

The edge-line vertex buffer holds two endpoints per mesh edge, 32 bytes
each: `vec4` position and `vec4` face normal, whose `w` packs the per-face
interior-tangent sign and edge-traversal winding the tent method needs
(`Build_context::build_edge_lines()`, `primitive_builder.cpp`), plus a
joint side buffer (uvec4 indices + vec4 weights per endpoint) for skinned
meshes. It is built from the `GEO::Mesh` polygon edges and has no LOD
levels - the root cause.

## 2. Target

- Each generated level `i >= 1` has edge lines built from its simplified
  triangles; the outline, and the edge-line display passes, draw the edges
  of the level the mesh is drawn with.
- The silhouette mask is drawn at that same level in every configuration
  (selection fill on or off, proxy-hidden sources).
- Same colors, pulse, active-item color, hovered handling, proxy-hidden
  sources and x-ray as now.

## 3. Steps

### Step 1: per-level edge lines in erhe::primitive

- **Generation.** A new function beside `generate_lod_chain()`
  (`erhe_primitive/lod.hpp`) builds a level's edge-line data from the
  level's triangles over the same `Lod_vertex_input` (float positions,
  joints): the unique edges of the level's triangles, keyed by the
  position pair (so attribute seams, which split vertices but not
  positions, do not duplicate an edge), with up to two adjacent triangles
  per edge. Per edge, the two endpoints in the existing layout: position,
  the adjacent triangle's geometric normal, and the packed sign / winding
  computed exactly as `build_edge_lines()` does from the triangle's normal,
  centroid and winding (boundary edge: face B mirrors face A; the
  per-endpoint packing is factored out of `build_edge_lines()` into one
  shared helper so the two cannot drift). Every edge of the level's
  triangles is written: the level's edge lines show its geometry as it is.
  Skinned input also writes the joint side data (the endpoint's joints /
  weights).
- **Storage.** `Lod_level` gains the level's edge-line vertex and joint
  `Buffer_range`s (level 0 keeps reading the original variant's ranges).
  The bytes of all levels are allocated in the optimized variant's existing
  allocation transaction (`buffer_mesh_allocation_mutex()`), as one
  edge-line vertex allocation and one joint allocation on the optimized
  `Buffer_mesh` (its existing `edge_line_vertex_allocation` /
  `edge_line_joint_allocation` fields), sub-ranged per level, from the
  pools named by `Buffer_info::edge_line_vertex_stream` /
  `edge_line_joint_stream`. The `Buffer_info` both optimized builds make
  today (`make_prepared_optimized_render_shape()`, primitive.cpp; the soup
  optimize in mesh_optimizer.cpp) leaves those streams null on purpose;
  with a LOD request they carry the base build's streams through. The
  ranges share the optimized variant's lifetime: published, retired and
  freed with it, the frees frame-fenced by
  `Mesh_memory::apply_ready_pending_frees()` as the original variant's are,
  so a frame in flight still reads a replaced variant's level edges safely.
- `lod.hpp`'s header comment ("no level has vertices of its own") and
  `doc/plans/lod.md` section 2.7 / D2, which still specify a per-level
  edge-line `Index_range`, are rewritten in the same commit: a level owns
  edge-line vertex and joint ranges, not line indices.
- **Both paths.** The geometry path
  (`make_optimized_render_shape_from_staged_build()`) and the soup path
  (`build_buffer_mesh_from_triangle_soup()`, which serves the prepared
  on-request rebuild) build them from the inputs they already pass to
  `generate_lod_chain()`. A primitive whose original variant has no edge
  lines (a prepared primitive before its full build) still gets level edge
  lines, so a forced level shows its edges even before the full build.
- `log_lod_chain()` reports the edge count per level.
- Tests (`src/erhe/primitive/test/test_lod.cpp`): edge count of a known
  mesh per level, no duplicate edge across an attribute seam, boundary
  edges mirrored, every triangle edge present, packed signs equal to
  `build_edge_lines()`'s on a level-0-equivalent input, skinned joint data.

### Step 2: the edge feed reads the drawn level

- `Content_wide_line_renderer::prepare_mesh()` takes the level the mesh is
  drawn with: for level `i >= 1`, when the primitive's live optimized
  variant has a chain level `i` (clamped to the chain like `add_entries()`
  does) with edge lines, the record points at that level's ranges;
  otherwise the original variant's ranges as now.
- The feed passes each mesh's
  `get_forced_lod_level(draw_list_scene->get_mesh_lod(mesh))` (valid in M2,
  after `flush_pending()`), whatever path the view's fill takes (user
  decision 2026-10-31): edge lines are not content, so shader debug and the
  fill's routing never change them; they respect the LOD level and the
  selection outline style (make_selection_outline_settings()), and are to
  respect the edge line color source (step 2b). The bucket path (shader
  debug fills) draws level 0 and is being retired in favour of the draw
  lists, so a shader debug view shows the outline at level N over a level-0
  fill.
- **Headset.** `Headset_view` calls `add_mesh()` inside `render_headset()`,
  on the render thread; `get_mesh_lod()` there would race `apply_lod()`
  in the overlapping M2 (`Draw_list_scene`'s rule: the render phase reads
  only what `publish()` copied for its frame). The headset feed moves to
  M2 as `prepare_mesh()` + `add_feed()`, like the viewport's (closing the
  open "headset records tools in R" item for the wide lines), so it reads
  the level the same way.
- The skinned path takes the level's joint range.
- An optimization hold (a live vertex drag) drops the optimized variant;
  the hold's re-registration puts the fill at level 0, and the feed falls
  to the original ranges with it (no live chain level). The live drag
  patches only the original variant's edge entries, which is what is then
  drawn.
- Out of scope: without `Content_wide_line_renderer` (null renderer), edge
  lines take the bucket line-index path on the original variant - level 0.
- Verification (headless Vulkan): a torus and a capsule with chains, each
  forced level, `capture_screenshot` of the selected mesh shows the outline
  on the drawn silhouette (no ribbon of the full-detail silhouette poking
  out); `use_draw_lists` off draws level 0 fill and level 0 outline; the
  edge-line display passes ("Content edge lines") show the level's edges.

### Step 2b: edge lines honor the color source

`Render_style_appearance::edge_lines_color_source` (constant color or a
per-primitive source) does not reach the wide-line edges:
`App_rendering::prepare_wide_line_feed()` passes one flat color per mesh
(`settings.get_selected_color()`), `Content_wide_line_record` holds one
`color`, and the compute pass emits it per record. A hovered, not selected
mesh in the selection outline pass gets `constant_color0` rather than
`constant_color1`. The feed resolves the color per mesh / primitive from the
pass's `Primitive_interface_settings` the way the primitive buffer writers
do for the bucket and draw-list records (the color source, hovered / active
flags), so edge lines show the configured source in the viewports and the
headset. User requirement (2026-10-31): edge lines respect the color source
setting.

### Step 3: stencil mask passes on the draw lists

- Both mask passes, "Selection stencil mask (fill disabled)" and "Selection
  stencil mask (proxy hidden)" (`app_rendering.cpp`), take no shader debug:
  the selection silhouette is not content, and a debug mode must not change
  the outline. With selection fill off and shader debug on, the outline
  must still be masked by the forced level's silhouette: that is the
  verification case. NOT via `shader_debug_override`: since 6d2a0cc37 any
  pass with a `shader_debug_override` stays on the bucket path
  (`composition_pass.cpp` routing gate, `!data.shader_debug_override`), so
  the override would block step 3. A pass-data property that keeps the
  view's shader debug out of the pass (its prepared `shader_debug` none, and
  no `shader_debug_layers`) is needed instead; design it with the routing
  gate's current form in front of you.
- Findings for the design (2026-10-31): `Draw_list_renderer::Render_parameters`
  already carries `color_blend_override`, honored by `draw_color`
  (`draw_list_scene.cpp`, the color blend selection), so a pass-supplied
  base pipeline or that override both work. The color variant comes from
  `Color_environment::make_environment_key()` (`draw_list_scene.cpp`), the
  uniform environment of every color list; `flat_draw_color` is the
  precedent of a position-pass axis there, so a `depth_only` environment
  flag setting `Shader_bool::VARIANT_DEPTH_ONLY` fits the "environment
  affects resolution, not partitioning" rule (that bit is only ever set by
  force masks). The alpha-tested cutout needs a derived shader gate in
  `res/shaders/erhe_standard_variant.glsl`, built like
  `ERHE_VARIANT_ID_RENDER_ALPHA_TEST` / `ERHE_VARIANT_PVS_ALPHA_TEST`:
  `ERHE_VARIANT_DEPTH_ONLY` on an alpha_test / screen_door material is not a
  position pass, so `standard.frag` runs the material inputs up to the
  discard and then writes nothing (a branch beside the ID / PVS writes after
  the discard). The material blending mode stays in the key under
  `VARIANT_DEPTH_ONLY` (`Shader_key::derive`), and the gate also fixes the
  bucket path's full-quad mask for alpha-tested meshes.
- A dedicated base pipeline for the mask (color writes disabled, the
  selected fill's depth / stencil state) replaces `color_blend_override`;
  the pass supplies it like any other base pipeline.
- The draw-list color passes gain a depth-only sub-variant, resolved per
  list like the shadow lists' {depth-only, distance, cube} axis, so the
  `VARIANT_DEPTH_ONLY` force mask becomes a covered sub-variant instead of
  a fallback trigger. Its alpha-tested form keeps the alpha test:
  `ERHE_VARIANT_DEPTH_ONLY` enables the position-pass gate that skips
  material sampling (erhe_standard_variant.glsl), so a plain depth-only
  variant on alpha-tested lists would stencil the full quad - the known
  "selection stencil mask is depth-only on alpha-tested meshes" defect,
  fixed here rather than carried over.
- The routing rule also requires a draw-list blending policy; both mask
  passes use `override_with_base_render_pipeline`. Each mask pass becomes
  an opaque and an alpha-tested pass (`opaque_primitives_only` /
  `alpha_tested_primitives_only`), mirroring "Content fill selected" /
  "Content fill alpha tested selected".
- All mask passes then route to the draw lists. Verification: with
  selection fill off, and with a proxy-hidden source selected, the outline
  at each forced level matches step 2's with fill on; an alpha-tested
  selection's outline follows its cutout; `use_draw_lists` off unchanged.

### Step 4: docs

`doc/editor/rendering.md` "Selection outline", `doc/erhe/draw_list_renderer.md`
(the depth-only color sub-variant, the routing table - which also still
names an `EDGE_LINES_FROM_ID` gate composition_pass.cpp no longer has),
`doc/erhe/meshoptimizer_integration.md` "Level of detail" (level edge
lines, storage), `doc/erhe/scene_renderer.md` (`prepare_mesh()`'s level)
and `doc/editor/lod.md` describe the result, in the commit of each step.

## 4. Interactions with the LOD plan

- `doc/plans/lod.md` step 4 (automatic selection) chooses the level per view
  and per entry, on the GPU path inside `cull_compact.comp`. The edge feed
  is prepared on the CPU per mesh, so step 4 must give the feed the same
  level: either the CPU evaluates the same selection for the meshes it feeds
  (few: selected, hovered, and meshes under edge-line display styles), or
  the wide-line dispatch reads the selected level from the GPU. This plan
  covers forced levels only; step 4's plan takes this as a requirement.
- `doc/plans/lod.md` step 6 (levels stored in the prepared glTF) stores the
  level edge lines too, or regenerates them at load from the stored level
  indices (cheap, CPU only); decided there.
- This plan implements D2 of `doc/plans/lod.md`; section 2.7 there points
  here (rewritten in step 1's commit).
- Positions: the level edges take the LOD input's float positions, which
  come from the source streams, while the fill draws the optimized
  variant's quantized positions; the ribbons sit within half a quantum of
  the fill, as level 0's do today.

## 5. Cost

A level's edge lines are about 1.5 edges per triangle at 64 bytes per edge
(two endpoints), plus 64 bytes per edge of joint data when skinned. With
each level half the previous one, all levels together are about the size of
level 0's edge lines.
