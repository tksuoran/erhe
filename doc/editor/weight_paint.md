# Weight painting (Blender-style, simplified)

Stability: experimental

The editor shows the skin weights of one joint on a skinned mesh, and paints
them with a brush. The two halves are independent: a shader debug mode that
visualizes the active joint's weight, and a tool that edits it.

Blender's weight paint is the calibration, and the parts erhe borrows are
recorded below so a future change can be checked against the same source.
Blender-specific machinery (PBVH threading, multi-paint, lock-relative,
X-mirror, brush textures) is not borrowed.

## What Blender does, and what erhe takes from it

### Weight to color

Blender bakes a 256-entry 1D LUT
(`source/blender/draw/engines/overlay/overlay_instance.cc:186`) from an HSV
formula and samples it by weight in the fragment shader:

```
gamma = 1.5
hsv   = ( (2/3) * (1 - w),  1.0,  pow(0.5 + 0.5*w, gamma) )
rgb   = pow(hsv_to_rgb(hsv), 1/gamma)
```

Hue sweeps 240 deg (blue) to 0 deg (red); brightness rises from 0.5 to 1.0, so
low weights read darker rather than only bluer. (The older piecewise "2.79"
ramp lives in `BKE_defvert_weight_to_rgb`,
`blenkernel/intern/deform.cc:1559`; the HSV form with gamma 1.0 reproduces
it.) erhe uses the HSV form, evaluated inline in the fragment shader: it is a
handful of ALU ops per fragment, and a LUT texture would need new binding
plumbing for no gain.

Zero-weight display: Blender sign-encodes an "alert" into the per-vertex
weight scalar (`extract_mesh_vbo_weights.cc:20`): `-1` means the vertex has no
weight in the active group, `-2` is the error state where vertex groups exist
but the active group index is invalid. The fragment shader
(`overlay_paint_weight_frag.glsl`) blends toward the "unreferenced" color
(black) with `alert*alert` and shows magenta for missing data. Optional fake
shading `abs(dot(N, L)) * 0.9 + 0.1` is multiplied in, which makes the shape
readable under flat ramp colors. erhe uses the same sign encoding and the same
fake shading, with `abs(dot(N, V))`.

### Brush

- Blender paints with a WORLD-SPACE SPHERE, not a screen-space circle: the
  cursor is ray-cast onto the surface once per dab, the pixel radius is
  converted to object space at that depth (`paint_calc_object_space_radius`,
  `editors/sculpt_paint/paint_utils.cc:97`), and every vertex inside the
  sphere is affected.
- Falloff: `p = 1 - distance/radius`, default SMOOTH curve `3p^2 - 2p^3`
  (`BKE_brush_curve_strength`, `blenkernel/intern/brush.cc:1608`).
- Per-dab influence `alpha = falloff * strength`; the blend ops
  (`ED_wpaint_blend_tool`, `mesh/paint_vertex_weight_utils.cc:278`) are
  mix `w' = p*a + w*(1-a)`, add `w' = w + p*a` and sub `w' = w - p*a`, then a
  clamp to [0,1] and a snap of values `< 1e-4` to exactly 0 so painting to
  zero terminates.
- NON-ACCUMULATE STROKE BEHAVIOR is Blender's default and the single most
  important "feel" feature: per stroke, keep a snapshot of each vertex's
  weight at stroke start and the max alpha each vertex has seen. A dab applies
  only where its alpha exceeds the recorded max, and it blends from the
  SNAPSHOT, not the current value (`mesh/paint_weight.cc:635`, `:1454`).
  Without this, overlapping dabs compound and the brush is uncontrollable.
- Auto-normalize: after writing the active weight, scale the vertex's OTHER
  weights so the total is 1, keeping the just-painted value fixed where
  possible (`do_weight_paint_normalize_all_locked_try_active`,
  `mesh/paint_weight.cc:416`, which treats the active group as locked).
  Required when the weights drive skinning, otherwise deformation drifts.
- Front-face rejection skips vertices whose normal faces away from the view.

## Weight visualization

`Shader_debug::joint_weight_ramp` (34, `shader_key.hpp`) shades a skinned mesh
by the active joint's weight using the HSV ramp above. The existing rule in
`shader_key.cpp` - a `SHADER_DEBUG` other than `none` force-enables every
varying the vertex format supplies - already covers the attributes it needs.
Debug variants are not prewarmed (`prewarm.cpp` hardcodes
`Shader_debug::none`), so the mode shares the first-use compile hitch of every
other debug mode.

**The active joint is marked per joint SLOT, not by a global index.** One
joint `Node` is a joint of EVERY skin that uses it, so a single global index
can light up at most one skin and leaves every other skinned mesh of a
multi-skinned rig reading as zero weight. The `Joint` struct therefore carries
a per-slot `uvec4 debug_flags` whose `.x` is 1 for the active joint, written
by `Joint_buffer::update()` from the `debug_target_joint` `Node*` plumbed
alongside `debug_joint_indices`; the shader tests that flag.
`debug_joint_indices.x` keeps only its sentinel role (`0xffffffffu` means no
active joint) and `.y` carries the "zero weight shows black" toggle - the
uvec4 is already plumbed end to end, so the toggle needs no renderer signature
change.

The vertex shader (`res/shaders/standard.vert`) computes a scalar varying
`v_weight` next to the existing `v_bone_color` block, sign-encoded as Blender
does it: a vertex with no influence from the target joint emits `-1.0` with
the zero-black toggle on and `0.0` with it off, and `-2.0` means no target is
selected. **The toggle is evaluated in the vertex stage** because the joint
UBO block is bound vertex-stage-only (`program_interface.cpp` declares it with
`Stage::vertex`); folding the toggle into the sign encoding avoids extending
the block's stage flags and rebinding it in the fragment stage. The
computation is gated on `ERHE_USE_SKINNING`.

The fragment shader branch (`standard.frag`, `ERHE_SHADER_DEBUG == 34`) reads
only `v_weight`: the HSV ramp, an `alert*alert` blend toward black when
`v_weight < 0` (with `alert = -v_weight` clamped to [0,1]), dim magenta for
the `-2` missing-data case, and the fake shading multiplied in. **The branch
body is wrapped in `#ifdef ERHE_USE_SKINNING` with an EMPTY `#else`**, so a
non-skinned variant keeps its normally computed `out_color`, plus a
`const float v_weight` fallback declaration so it links. Mode 18
(`joint_weights`) does not do this - its fragment override runs
unconditionally against a `const vec4 v_bone_color = vec4(0.5)` fallback,
which is why non-skinned meshes render flat gray in that mode. Do not copy
that.

`Weight_display` (`tools/weight_display.{hpp,cpp}`) holds the active joint and
the display flags. The joint comes from selecting a bone proxy or a joint node
(`Selection_message`, `Bone_visualization::get_joint_for_proxy`, and nodes
carrying `Item_flags::bone`) and stays active when deselected. Its status line
and the zero-black checkbox render in `App_rendering`'s "Skin Debug" section
and in the Weight Paint Tool properties. The per-viewport shader-debug combo
offers the mode; the paint tool forces it on while active
(`Viewport_scene_view::set_shader_debug`) and restores the previous mode on
deactivate.

**The active joint is one global value while the debug mode is per viewport.**
With two open scenes the flag is set in whichever scenes' skins list that
node. This is the same trade-off the other joint debug modes make.

## The brush

`Weight_paint_tool` (`tools/weight_paint_tool.{hpp,cpp}`) is structurally a
`Paint_tool`: a command bound to a left-mouse drag, hover from
`Scene_view::get_hover(content_slot)`, `tool_render` feedback (a brush circle
drawn through `Primitive_renderer`), and a `tool_properties` panel. Its
properties are the active joint (shared with `Weight_display`), the weight
target value [0..1], strength [0..1], a world-space radius, the blend mode
(mix / add / subtract), auto-normalize (default on) and front-face-only
(default on).

Per dab:

- The stroke locks onto the FIRST-HIT PRIMITIVE (mesh plus
  `scene_mesh_primitive_index`, hence one geometry). A dab whose hover lands
  on another mesh, or on another primitive of the same mesh, is ignored until
  the stroke ends. This keeps the stroke bookkeeping and the undo unit
  single-geometry, as Blender's effectively is.
- **The mesh may be posed.** Geometry vertex positions are bind-pose, but the
  user paints on the deformed surface, which is the point of weight painting.
  So the distance and front-face tests run on CPU-SKINNED positions and
  normals: for each candidate vertex, `sum w_i * Skin_data::get_world_from_bind(j_i)`
  applied to the bind-pose position, and the inverse transpose for the normal.
  `get_world_from_bind` returns a `std::optional`; a `nullopt` (an expired
  joint) is treated as identity, matching that function's own fallback for a
  missing inverse-bind matrix.
- The distance test runs in world space against `Hover_entry::position`
  directly, which sidesteps node scale. `d = |skinned_vertex_world -
  brush_center_world|`; a vertex with `d > radius` is skipped;
  `falloff = smooth(1 - d/radius)` with `smooth(p) = 3p^2 - 2p^3`;
  `alpha = falloff * strength`. The front-face test skips a vertex whose
  skinned normal faces away: with `view_dir` pointing camera to surface,
  `dot(view_dir, skinned_normal) > 0`.

Stroke bookkeeping is created on `try_ready` and dropped at stroke end, keyed
by geometry vertex id: a snapshot of the target joint's weight per touched
vertex at stroke start, and `alpha_max` per touched vertex. A dab applies only
where `alpha > alpha_max[v]`, and blends from the snapshot, which is Blender's
non-accumulate behavior.

The float truth is `Mesh_attributes::vertex_joint_weights_0` and
`vertex_joint_indices_0`. For each affected vertex the tool finds the active
joint among the 4 slots and, if it is absent and the new weight is above the
smallest slot's, evicts that slot; applies the blend op; clamps to [0,1]; and
snaps values below 1e-4 to 0. With auto-normalize on it rescales the other
nonzero slots so the total is 1 while keeping the painted value.
**Weights may sum to less than 1 when painting a single influence**: when all
the other slots are zero the remainder is left unassigned, because erhe's
skinning shader does not renormalize.

The GPU update writes TWO attributes per touched GPU vertex, `joint_indices_0`
(`format_8_vec4_uint`) and `joint_weights_0` (`format_8_vec4_unorm`), both in
stream 0 of `vertex_format_skinned` and byte-adjacent after the 12-byte
position, so one 8-byte write covers both. Resolve the offsets through
`find_attribute` rather than hardcoding them, and reach every GPU vertex a
geometry vertex spawned through
`Element_mappings::mesh_corner_to_vertex_buffer_index` over
`Geometry::get_vertex_corners(v)`. The ranges are batched per dab and uploaded
by the once-per-frame `Mesh_memory::flush`.

**The fill mesh is not the only GPU copy of the joint data.** The expanded
solid-wireframe mesh (`vertex_format_skinned_wireframe`) and the edge-line
joint stream (`edge_line_joint_stream`, from
`Mesh_memory::make_primitive_buffer_info`) carry their own joint indices and
weights. The primitive is therefore REBUILT AT STROKE END, once per stroke:
the fill updates live per dab, and the wireframe and edge-line overlays lag by
the duration of a stroke.

Undo is one `Paint_weights_operation` (`operations/`) per stroke: `try_ready`
records the pre-stroke weight and index arrays of the touched primitive's
geometry (4 floats plus 4 uints per vertex), stroke end records the post
state, and undo and redo restore the arrays and re-enqueue the GPU ranges.
The primitive rebuild the operation performs is what refreshes the wireframe
and edge-line streams.

## Limits

- **A geometry is shared.** `Mesh::skin` is per mesh, but primitives are
  cloned per skinned instance on glTF import. Painting edits the shared
  `Geometry`, so two instances sharing a geometry both change. This matches
  Blender's shared-mesh behavior.
- **The tool refuses a non-skinned mesh** with a status message. It requires
  `c_joint_indices_0` and `c_joint_weights_0` on the geometry, which is what
  makes `Primitive_builder` choose the skinned vertex format; adding weights
  to a previously unskinned mesh means a full primitive rebuild.
- The 4-influence and 256-joint limits of the skinned vertex format bound what
  can be painted.

## Future work

- [plans/weight_paint.md](../plans/weight_paint.md) - further brushes, symmetry
  and the normalization question.
