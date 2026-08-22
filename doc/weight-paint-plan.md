# Weight painting plan (Blender-style, simplified)

Status (2026-08-22): phases 1 and 2 are implemented. Shader:
`Shader_debug::joint_weight_ramp` (34) in shader_key.hpp +
standard.vert/.frag. Editor: `tools/weight_display.{hpp,cpp}` (active
joint + zero-black flag via `App_rendering::debug_joint_indices`),
`tools/weight_paint_tool.{hpp,cpp}` (the brush),
`operations/paint_weights_operation.{hpp,cpp}` (per-stroke undo +
primitive rebuild). Verified with res/editor/assets/RiggedFigure: the
ramp follows the selected bone; non-skinned meshes keep normal shading.
Two divergences from the plan text are noted inline: the zero-black flag
rides in `debug_joint_indices.y`, not `extra1`, and the target joint is
marked per joint SLOT instead of by a single global index (see phase-1
step 2 - the global-index design only ever lit up one skin, which made
every joint of a multi-skinned character rig read as zero weight).

Goal: add weight painting to the erhe editor in two stages, keeping each
stage small and shippable:

1. **Phase 1 — weight visualization**: view the skin weights of a selected
   joint (bone) on a skinned mesh, using Blender's familiar blue→green→red
   ramp, with a "zero weight shows black" option.
2. **Phase 2 — basic weight painting**: a brush that edits `JOINTS_0` /
   `WEIGHTS_0` for the active joint with mix/add/subtract blending, radial
   falloff, and auto-normalization.

Later extensions (blur brush, mirror painting, locked groups, multi-joint
display) are explicitly out of scope but the design should not block them.

Reference: Blender source in `/c/git/blender` (paths below are relative to
that repo).

## How Blender does it (calibration)

Only the parts we intend to borrow; Blender-specific machinery (PBVH
threading, multi-paint, lock-relative, X-mirror, brush textures) is skipped.

### Weight → color

Modern Blender bakes a 256-entry 1D LUT
(`source/blender/draw/engines/overlay/overlay_instance.cc:186`) from an HSV
formula and samples it by weight in the fragment shader:

```
gamma = 1.5
hsv   = ( (2/3) * (1 - w),  1.0,  pow(0.5 + 0.5*w, gamma) )
rgb   = pow(hsv_to_rgb(hsv), 1/gamma)
```

Hue sweeps 240° (blue) → 0° (red); brightness rises from 0.5 to 1.0 so low
weights read darker, not just bluer. (The older piecewise "2.79" ramp lives
in `BKE_defvert_weight_to_rgb`, `blenkernel/intern/deform.cc:1559`; the HSV
form with gamma 1.0 reproduces it. We use the HSV form.)

Zero-weight display: Blender sign-encodes an "alert" into the per-vertex
weight scalar (`extract_mesh_vbo_weights.cc:20`): `-1` = vertex has no
weight in the active group (subject to the alert-mode setting); `-2` = the
error state where vertex groups exist but the active group index is
invalid. The fragment shader
(`overlay_paint_weight_frag.glsl`) blends toward the "unreferenced" color
(black) with `alert*alert`, and shows magenta for missing data. Optional
fake shading `abs(dot(N, L)) * 0.9 + 0.1` (computed in the paired vertex
shader) is multiplied in, which makes the shape readable under flat ramp
colors.

### Brush (for phase 2)

- Blender paints with a **world-space sphere**, not a screen-space circle:
  the cursor is ray-cast onto the surface once per dab, the pixel radius is
  converted to object space at that depth
  (`paint_calc_object_space_radius`, `editors/sculpt_paint/paint_utils.cc:97`),
  and every vertex inside the sphere is affected. An optional "tube" falloff
  (view-aligned cylinder) gives paint-through behavior.
- Falloff: `p = 1 - distance/radius`, default SMOOTH curve `3p² − 2p³`
  (`BKE_brush_curve_strength`, `blenkernel/intern/brush.cc:1608`).
- Per-dab influence `alpha = falloff * strength (* pressure)`; blend ops
  (`ED_wpaint_blend_tool`, `mesh/paint_vertex_weight_utils.cc:278`):
  - mix: `w' = p*a + w*(1-a)`
  - add: `w' = w + p*a`
  - sub: `w' = w − p*a`
  then clamp to [0,1] and snap values `< 1e-4` to exactly 0 so painting to
  zero terminates.
- **Non-accumulate stroke behavior** (Blender's default; the single most
  important "feel" feature): per stroke, keep (a) a snapshot of each
  vertex's weight at stroke start and (b) the max alpha each vertex has
  seen. A dab only applies if its alpha exceeds the recorded max, and it
  blends from the *snapshot*, not the current value
  (`mesh/paint_weight.cc:635`, `:1454`). Without this, overlapping dabs
  compound and the brush is uncontrollable.
- Auto-normalize: after writing the active weight, scale the vertex's
  *other* weights so the total is 1, keeping the just-painted value fixed
  when possible (`do_weight_paint_normalize_all_locked_try_active`,
  `mesh/paint_weight.cc:416`, which treats the active group as locked; the
  plain `do_weight_paint_normalize_all` at `:286` rescales everything
  including the active group). Required when the weights actually drive
  skinning, otherwise deformation drifts.
- Front-face rejection: skip vertices whose normal faces away from the view.

## Fit with existing erhe pieces

erhe already has most of the plumbing:

- **A joint-weight debug view exists**: `Shader_debug::joint_weights` (18) in
  `src/erhe/scene_renderer/erhe_scene_renderer/shader_key.hpp` shades
  skinned meshes by weight-blended per-joint palette colors
  (`res/shaders/standard.vert:302`, `standard.frag` `ERHE_SHADER_DEBUG == 18`
  branch). Phase 1 is "the same thing, for one joint, with a ramp".
- **An unused UBO slot for the target joint**:
  `Joint_block::debug_joint_indices` (uvec4) in
  `src/erhe/scene_renderer/erhe_scene_renderer/joint_buffer.hpp` is plumbed
  from an ImGui slider in `App_rendering` through `Composition_pass` and
  `Forward_renderer` into `Joint_buffer::update()` and the shader block —
  but read by no shader today. Phase 1 repurposes it for the active joint
  index; only the value's source changes. (The neighboring `extra1` field
  is in the block layout but hardcoded to 0 inside `update()`; using it
  needs a small signature change.)
- **Per-viewport debug-mode selection**:
  `Viewport_scene_view::set_shader_debug` + the ImGui combo over
  `c_shader_debug_strings` (`src/editor/scene/viewport_scene_view.cpp:1305`),
  persisted in settings. Alternatively, per-pass override via
  `Composition_pass_data::shader_debug_override` — the "Bone solid (N.V)"
  pass in `src/editor/app_rendering.cpp:625` is the worked example.
- **Bone picking already works**: `Bone_visualization`
  (`src/editor/tools/bone_visualization.hpp`) creates solid pickable bone
  proxies per joint (`get_joint_for_proxy(mesh)`), and
  `Mesh_component_selection` has a `bone` component mode. "Click a bone to
  make it the active paint joint" is nearly free.
- **A vertex paint tool already patches GPU vertex buffers**: `Paint_tool`
  (`src/editor/tools/paint_tool.cpp:328`, `paint_vertex()`) shows the exact
  recipe: `Buffer_mesh` → `Mesh_memory::get_vertex_input(key).vertex_format`
  → `find_attribute(usage, index)` → byte offset from
  `vertex_buffer_ranges[stream] + vertex_id * stride + attribute->offset` →
  `Mesh_memory::enqueue_vertex_data(range, bytes)`. It already handles the
  `format_8_vec4_unorm` encode via `erhe::dataformat::float_to_unorm8`, and
  `Element_mappings::mesh_corner_to_vertex_buffer_index` maps geometry
  corners to GPU vertex ids.
- **CPU-side authority exists**: `erhe::geometry::Mesh_attributes` has
  `vertex_joint_indices_0` (`GEO::vec4u`) and `vertex_joint_weights_0`
  (`GEO::vec4f`) — full-precision float weights, from which
  `Primitive_builder` packs the GPU streams.
- **Skins are registered and discoverable**: `Scene_root::register_skin`,
  `Skin_registered_message`, `Skin_data::joints`, and `Mesh::skin`.

Data-format constraint worth stating up front: the GPU-side skinned vertex
format (`Mesh_memory::vertex_format_skinned`,
`src/erhe/scene_renderer/erhe_scene_renderer/mesh_memory.cpp`) stores
`joint_indices_0` as `format_8_vec4_uint` and `joint_weights_0` as
`format_8_vec4_unorm`. So:

- weights are quantized to 1/255 on the GPU — fine for display and for
  skinning, but the **source of truth for editing must be the float
  attributes in `erhe::geometry`**, with the unorm8 GPU copy derived;
- at most 4 influences per vertex (standard glTF `JOINTS_0`/`WEIGHTS_0`),
  and at most 256 joints per primitive. Both match our glTF import/export
  path, so we accept them as feature limits rather than fight them.

## Phase 1 — weight visualization

### Design

A new shader debug mode, `Shader_debug::joint_weight_ramp`, that shows the
weight of a single target joint using the Blender HSV ramp.

1. **Shader key** (`shader_key.hpp` / `.cpp`):
   - Append `joint_weight_ramp` to `enum class Shader_debug` and the
     matching entry to `c_shader_debug_strings` (order must match).
   - The existing rule in `shader_key.cpp` (`SHADER_DEBUG != none` force-
     enables all varyings the vertex format supplies) already covers the
     needed attributes; verify `USE_SKINNING` is on for skinned meshes via
     `Shader_key::derive`.
   - No prewarm work: `prewarm.cpp` hardcodes `Shader_debug::none` and no
     debug variant is prewarmed today, so the new mode simply shares the
     same first-use compile hitch as the existing 34 modes. Prewarming
     debug variants would be a new mechanism; not worth it here.
2. **Target joint uniform**: reuse `Joint_block::debug_joint_indices.x`
   (uvec4 already written to the UBO by `Joint_buffer::update()` and read
   by no shader today; fed from `App_rendering::debug_joint_indices`
   through `Composition_pass` into `Forward_renderer`). The editor writes
   the active joint's global index in the joint buffer: the joint's index
   within `Skin_data::joints` plus the skin's base offset. Compute the base
   offset by walking `scene->get_skins()` in order — the same order every
   `Joint_buffer::update()` caller uses — rather than reading back
   `Skin_data::joint_buffer_index`, which is only assigned *during*
   `update()` and would be stale on the first frame or after skin changes.
   Sentinel `0xffffffffu` = no joint selected. Caveat (same as the existing
   `debug_joint_colors`): this is one global value applied to every scene's
   joint buffer, so with two open scenes the second scene highlights
   whatever joint occupies that global index; acceptable for now.

   **Superseded (2026-08-22).** A single global index cannot express the
   target at all once a rig has more than one skin. One joint `Node` is a
   joint of *every* skin that uses it. Matching one global index lit up at
   most one skin and left every other skinned mesh reading as zero weight
   (all black with zero-black on), which is exactly how the bug was reported.
   What the implementation does instead: the `Joint` struct carries a
   per-slot `uvec4 debug_flags` whose `.x` is 1 for the active joint, written
   by `Joint_buffer::update()` from a new `debug_target_joint` `Node*` plumbed
   alongside `debug_joint_indices`; the shader tests that flag.
   `debug_joint_indices.x` keeps only its sentinel role (`0xffffffffu` = no
   active joint). The `App_rendering` "Debug Joint Index" slider is gone with
   the index it fed - it addressed one slot, and cast the sentinel to `0` on
   the way through `int`. The two-open-scenes caveat above still stands, now
   as "the flag is set in whichever scenes' skins list that node".
3. **The "zero weight shows black" toggle**: pass it in
   `debug_joint_indices.y` — the uvec4 is already plumbed end to end and
   only `.x` carries a value, so no renderer signature change is needed at
   all. (`Joint_block::extra1` would work too but is hardcoded to `0`
   inside `Joint_buffer::update()`, which would mean threading a new
   argument through `Forward_renderer::Render_parameters`; `.y` is the
   same UBO for free. This is what the implementation does.)
4. **Vertex shader** (`res/shaders/standard.vert`): alongside the existing
   `v_bone_color` block, when the new debug mode is active compute a scalar
   varying `v_weight`:
   - `w = Σ over i of (a_joint_indices_0[i] + base_joint_index ==
     target_index ? a_joint_weights_0[i] : 0)` — the `+ base_joint_index`
     matches how `erhe_skinning.glsl` offsets per-primitive joint indices
     into the shared joint array.
   - Sign-encode Blender-style: if the vertex has no influence from the
     target joint *and* the zero-black toggle is on, emit `-1.0`; if the
     toggle is off, emit `0.0` (plain dark-blue ramp bottom). No target
     selected: emit `-2.0` ("missing data"). The toggle is evaluated here,
     in the vertex stage, because the joint UBO block is bound
     vertex-stage-only (`program_interface.cpp` declares it with
     `Stage::vertex` — "joint: skinning in *.vert only"); folding
     the toggle into the sign-encoding avoids extending the block's stage
     flags and rebinding it in the fragment stage. The vertex-side
     computation is gated on `ERHE_USE_SKINNING` like the existing
     mode 18.
5. **Fragment shader** (`res/shaders/standard.frag`): new
   `#elif ERHE_SHADER_DEBUG == <n>` branch, reading only the `v_weight`
   varying (no joint-block access needed in this stage). Important: the
   branch body must be wrapped in `#ifdef ERHE_USE_SKINNING` with an
   *empty* `#else`, so non-skinned variants keep their normally computed
   `out_color` (plus a `const float v_weight` fallback declaration so they
   link). Note the existing mode 18 does *not* do this — its fragment
   override runs unconditionally against a `const vec4 v_bone_color =
   vec4(0.5)` fallback (`standard.frag:113`), which is why non-skinned
   meshes render flat gray in that mode today; do not copy that. Branch
   contents:
   - the HSV ramp evaluated inline (a handful of ALU ops per fragment; a
     LUT texture is not worth new binding plumbing here — note this
     deliberately diverges from Blender's LUT approach),
   - `alert*alert` blend toward black when `v_weight < 0` (with
     `alert = -v_weight` clamped to [0,1]), dim magenta for the `-2`
     "missing data" case,
   - fake shading `abs(dot(N, V)) * 0.9 + 0.1` multiplied in, mirroring the
     `vdotn` debug mode's look so the surface stays readable.
   - While in there, the stub `ERHE_SHADER_DEBUG == 17` (joint_indices)
     branch can be filled with the palette color of the *strongest* joint,
     but that is optional and separate.
6. **Choosing the active joint (editor)**:
   - New small editor class `Weight_display` (or fold into an existing
     rendering-settings spot): holds
     `std::weak_ptr<erhe::scene::Node> active_joint` and the display flags.
   - Sources for the active joint: (a) selecting a bone proxy or joint node
     (listen to `Selection_message`, use
     `Bone_visualization::get_joint_for_proxy`, and accept nodes with
     `Item_flags::bone`); (b) a joint list in the tool's properties panel
     (iterate `Mesh::skin->skin_data.joints` of the selected mesh).
   - Each frame (or on change), resolve the joint to its global index (per
     step 2) and route it, with the zero-black flag, to
     `Joint_buffer::update()` the way `App_rendering::debug_joint_indices`
     flows today.
7. **Turning the view on**: the existing per-viewport shader-debug combo
   gets the new entry for free from `c_shader_debug_strings`. Phase 2's
   paint tool will force the mode on while active via
   `Viewport_scene_view::set_shader_debug` (restoring the previous mode on
   deactivate), so phase 1 needs no new UI beyond the joint picker and the
   zero-black checkbox next to it.

### Acceptance for phase 1

- Load a skinned glTF (e.g. a sample with a rigged character), select the
  new debug mode, click bones: the mesh shades blue→red by that bone's
  weight; unweighted areas are dark blue, or black with the option on.
- With no joint selected, skinned meshes render in the dim "missing data"
  look; non-skinned meshes are unaffected (fragment branch is empty for
  variants without `ERHE_USE_SKINNING`, unlike existing mode 18).
- No change to normal rendering when the mode is off (shader variants are
  keyed; existing variants unchanged).

## Phase 2 — basic weight painting

### Design

A new `Weight_paint_tool` in `src/editor/tools/`, structurally a copy of
`Paint_tool` (command bound to left-mouse drag, hover from
`Scene_view::get_hover(content_slot)`, `tool_render` feedback,
`tool_properties` panel), with these differences:

1. **State / properties panel**:
   - active joint (shared with phase 1's `Weight_display`),
   - weight target value [0..1] (Blender's "Weight" slider),
   - strength [0..1], radius (world units for the first cut; see
     "later" for pixel-radius),
   - blend mode: mix / add / subtract,
   - auto-normalize toggle (default on),
   - front-face-only toggle (default on).
2. **Dab application** (per mouse-drag motion event):
   - Hover gives `Hover_entry::position` (world) and the hit
     `scene_mesh` + `primitive_index` + `geometry`. The stroke is locked to
     the first-hit primitive (mesh + `scene_mesh_primitive_index`, hence
     one geometry): dabs whose hover lands on a different mesh *or another
     primitive of the same mesh* are ignored until the stroke ends
     (Blender effectively does the same; keeps stroke bookkeeping and undo
     single-geometry).
   - **The mesh may be posed.** Geometry vertex positions are bind-pose,
     but the user paints on the deformed surface (the whole point of weight
     painting is checking a pose). So the distance and front-face tests run
     on **CPU-skinned positions/normals**: for each candidate vertex,
     compute `Σ wᵢ · Skin_data::get_world_from_bind(jᵢ)` applied to the
     bind-pose position (and inverse-transpose for the normal), using the
     vertex's current joint indices/weights. `get_world_from_bind` returns
     `std::optional`; on `nullopt` (expired joint) treat that term as
     identity, matching the function's own fallback for missing
     inverse-bind matrices. This is per-dab and, after a
     coarse cull (bind-pose AABB of the brush sphere inflated by a
     conservative bound, or simply all vertices for a first cut), cheap.
     Distance test in world space against `Hover_entry::position` directly
     — this also sidesteps node scale entirely (Blender instead divides an
     object-space radius by the object scale, `paint_utils.cc:110`; under
     non-uniform scale both approaches are approximations, ours less so).
   - `d = |skinned_vertex_world − brush_center_world|`, skip `d > radius`,
     `falloff = smooth(1 − d/radius)` with `smooth(p) = 3p² − 2p³`.
   - Front-face test: with `view_dir` pointing camera → surface, skip if
     `dot(view_dir, skinned_normal) > 0` (normal facing away).
   - `alpha = falloff * strength`.
3. **Stroke bookkeeping** (created on `try_ready`, dropped on stroke end;
   keyed by geometry vertex id — one geometry per stroke, see the
   first-hit-mesh lock above):
   - snapshot of the target-joint weight per touched vertex at stroke
     start,
   - `alpha_max` per touched vertex; a dab applies only where
     `alpha > alpha_max[v]`, and blends from the snapshot value
     (Blender's non-accumulate behavior). An "accumulate" checkbox can
     bypass both later; the default is non-accumulate.
4. **Weight write (CPU truth first)**: for each affected vertex, operate on
   `Mesh_attributes::vertex_joint_weights_0` / `vertex_joint_indices_0`
   (float / uint, full precision):
   - find the active joint in the vertex's 4 slots; if absent and the new
     weight is > 0, insert it by evicting the smallest-weight slot (only if
     the new weight exceeds it);
   - apply the blend op, clamp [0,1], snap `< 1e-4` to 0;
   - if auto-normalize: rescale the other nonzero slots so the total is 1
     while keeping the painted value (if all others are zero and painted
     weight < 1, leave the remainder unassigned — glTF tolerates it and
     renormalizing in the skinning shader is already common practice; note
     erhe's current skinning shader does not renormalize, so document this
     as "weights may sum < 1 when painting a single influence" and revisit).
5. **GPU update**: reuse `paint_vertex()`'s byte-offset recipe but write
   *two* attributes per touched GPU vertex: `joint_indices_0`
   (`format_8_vec4_uint`, offset 12) and `joint_weights_0`
   (`format_8_vec4_unorm`, offset 16), both in stream 0 of
   `vertex_format_skinned` — byte-adjacent after the 12-byte position, so
   one 8-byte write per GPU vertex covers both (resolve offsets via
   `find_attribute`, do not hardcode). Use
   `Element_mappings::mesh_corner_to_vertex_buffer_index` over
   `Geometry::get_vertex_corners(v)` to reach every GPU vertex spawned from
   the geometry vertex. Batch the ranges per dab and let the existing
   once-per-frame `Mesh_memory::flush` upload them.
   Stale-stream caveat: the fill mesh is not the only GPU copy of the
   joint data — the expanded solid-wireframe mesh
   (`vertex_format_skinned_wireframe`) and the edge-line joint stream
   (`edge_line_joint_stream` from `Mesh_memory::make_primitive_buffer_info`)
   carry their own joint indices/weights and would keep deforming with the
   old weights. Handle it the way `Mesh_component_transform` handles
   interactive vertex edits: **rebuild the primitive on stroke end** (once
   per stroke, cheap), accepting that wireframe/edge-line overlays lag by
   the duration of a stroke while the fill updates live per dab.
6. **Undo**: one operation per stroke. On `try_ready`, record the full
   pre-stroke `vertex_joint_weights_0`/`indices_0` arrays of the touched
   primitive's geometry (they are small: 4 floats + 4 uints per vertex); on
   stroke end, record the post state and push an operation onto the
   operation stack (`src/editor/operations/`) whose undo/redo restores the
   arrays and re-enqueues the GPU ranges. (`Paint_tool` today is not
   undoable; do not copy that gap.)
7. **Visualization while painting**: on tool activate, force the phase-1
   debug mode on the hovered viewport and select it as the active display;
   restore on deactivate. Draw a brush circle at the hover point via
   `Primitive_renderer` in `tool_render` (as `Paint_tool` draws its hover
   feedback).
8. **Registration**: member + construction in `src/editor/editor.cpp`,
   pointer in `App_context`, `CMakeLists.txt` entry, toolbox icon via
   `Icon_set` — same checklist as any tool.

### Acceptance for phase 2

- Painting visibly and immediately changes both the ramp display and the
  actual deformation when scrubbing an animation.
- A stroke back-and-forth over the same area converges to the target
  weight, it does not flicker or overshoot.
- Undo/redo restores weights exactly (float truth, not unorm8 round-trip).
- glTF export after painting round-trips the edited weights.

## Out of scope (later extensions)

- Blur/average/smear brushes (blur first when we get there — it is cheap
  and useful; smear needs stroke-direction history).
- X-mirror / symmetry painting.
- Locked vertex groups, lock-relative, multi-paint.
- Pressure (tablet) support, custom falloff curves, brush textures.
- Screen-space (pixel) brush radius with perspective compensation
  (`paint_calc_object_space_radius` equivalent) — start with a world-space
  radius slider.
- Painting weights for joints beyond the 4-influence / 256-joint limits of
  the current vertex format.
- Adding influences to vertices of *other* primitives of the same mesh not
  under the brush, spatial acceleration for very dense meshes.

## Implementation order

1. Shader work: enum + strings + vert/frag branches; hardcode target
   joint 0 to see it working. (Small, isolated, testable.)
2. Active-joint plumbing: `Weight_display` state, selection listener,
   `debug_joint_indices` (.x = joint, .y = zero-black flag) fed through
   the existing `App_rendering::debug_joint_indices` route, joint status +
   zero-black checkbox UI. → **Phase 1 done.**
3. `Weight_paint_tool` skeleton copied from `Paint_tool`: command binding,
   hover, first-hit-primitive stroke lock, brush circle, properties panel.
4. CPU weight edit (blend ops, slot insert/evict, normalize) + CPU-skinned
   distance/front-face tests + GPU two-attribute write + primitive rebuild
   on stroke end (wireframe/edge-line streams). First without stroke
   bookkeeping (accumulate behavior), then add snapshot/alpha_max.
5. Undo operation.
6. Polish: force-debug-mode-while-painting, docs/notes.md updates.

## Risks / open questions

- `Mesh::skin` is per-mesh but primitives are cloned per skinned instance
  on glTF import ("because erhe currently puts skin into the mesh",
  `gltf_fastgltf.cpp:2705`). Painting edits the shared `Geometry`; two
  instances sharing a geometry would both change. Acceptable (matches
  Blender's shared-mesh behavior) but must be stated in the tool docs.
- Skinned meshes whose GPU buffers were built without the geometry
  attributes present (procedural or imported without weights): phase 2
  requires `c_joint_indices_0`/`c_joint_weights_0` to exist on the
  geometry (that is what makes `Primitive_builder` choose the skinned
  vertex format). Adding weights to a previously unskinned mesh means a
  full primitive rebuild — out of scope; the tool should simply refuse
  non-skinned meshes with a status message.
- The editor may render the same scene in multiple viewports; the debug
  mode is per-viewport, the active joint is global (one UBO slot). That is
  the same trade-off the existing debug_joint_colors make; fine for now.
- Weight sums < 1 after single-influence painting (see normalization note):
  decide between (a) shader-side renormalization, (b) always distributing
  the remainder to the largest other influence, or (c) accepting it. Start
  with (b) when auto-normalize is on, since it keeps data valid for any
  glTF consumer.
