# Mesh modeling tools

Stability: experimental

The modal mesh modeling tools of the editor: gestures that change a mesh's
topology interactively in a viewport, in a mesh component mode
(`doc/editor/mesh_component_selection.md`): loop cut, inset, bevel and
knife (`doc/plans/mesh_modeling.md` holds their design
and the Blender behaviour each tool follows). The discrete operations (delete, dissolve, merge,
subdivide) are Operations window buttons and are described in
`doc/editor/operations.md`; the slides in `doc/editor/transform.md` "Scalar
edits".

## Gesture lifecycle

A modal tool runs in four steps, following `doc/plans/mesh_modeling.md` D3:

1. **Preview.** The tool draws what a confirm would do, recomputed only when
   its input changes (the hovered mesh or edge, the tool's options, the
   mode, the geometry), never per frame, into persistent scratch on
   `Mesh_component_selection_tool`. Nothing is edited: cancel leaves the
   mesh and the operation stack untouched, and undo and redo stay
   available.
2. **Topology step.** Confirm builds the result `Geometry` once, builds its
   Primitive (`make_renderable_mesh` + `make_raytrace`), swaps it in place
   with `swap_mesh_primitives()` and installs the result selection with
   `Mesh_component_selection::set_after_operation()`. The pre-step selection
   entry stays with the before geometry, dormant.
3. **Slide.** The tool chains into a scalar edit of `Mesh_component_transform`
   (`Transform_tool::begin_scalar_drag()` with a `Scalar_topology_step`, or
   `run_scalar_edit()` for the numeric form) that moves the new elements'
   positions in place. While it runs, undo and redo decline
   (`Operation_stack::get_undo_block_reason()`).
4. **Commit or cancel.** Commit re-samples the corner texcoords around the
   slid vertices into the result geometry, recomputes its normals when a
   vertex moved, rebuilds the Primitive and queues one
   `Fork_geometry_operation` (before primitive, after primitive), so the
   whole gesture is one undo entry. Cancel swaps the before primitive back
   and switches back to the mode the gesture started in, which makes the
   pre-step selection entry the selection again; nothing is queued. A scene
   close cancels a running slide the same way.

`Scalar_topology_step` (`transform/mesh_component_transform.hpp`) carries the
mesh, the primitive index, the before and after `Mesh_primitive`, the undo
label and the mode to restore; with it, `Mesh_component_transform` edits only
that mesh primitive and treats it as an extruded group for commit and cancel.
For inset and bevel it also carries the vertices the edit moves with their
thickness (bevel: amount) and depth directions, and a `rebuild` function: commit then re-runs the
topology step from the before geometry with the final values and commits
that geometry (every attribute interpolated by the operation, the selection
entry carried onto it) in place of the edited one.

## Loop cut

Blender's loop cut (`doc/plans/mesh_modeling.md` section 4.5).

- **Start.** Ctrl+R over a content mesh (one component selection can
  address) in vertex, edge or face mode starts the loop cut mode, with one
  cut and smoothness 0. It is refused while a slide or another component
  edit runs.
- **Preview.** The seed is the edge of the hovered facet nearest to the
  pointer. The ring is `erhe::geometry::walk_edge_ring()` from it when the
  seed has a quad facet, else the seed alone. The preview draws, in the
  hover color, the cut points (`cuts` points at `i / (cuts + 1)` along every
  ring edge) and the cut segments between consecutive ring edges: the
  endpoints of the next edge pair with the current edge's through the quad
  holding both, so segments at one fraction never cross; a closed ring adds
  the segments between its last and first edge. The preview is straight
  (smoothness is not drawn).
- **Cut.** Left click or Enter cuts: `erhe::geometry::operation::subdivide_edges()`
  of the ring's edges with the cut count and smoothness, `only_quads` off
  (grid fill), into a new Geometry swapped in as above. The mode switches to
  edge mode (after the swap, so the dormant pre-cut entry is not converted)
  and the inner edges (the new loops) become the selection. The edge slide
  of those loops starts at the click position; its confirm (left click,
  Enter) commits "Loop Cut", its cancel (right click, Escape) restores the
  mesh, the mode and the selection. A seed without a quad facet cuts that
  one edge (its facets fill by the subdivide patterns), selects nothing and
  commits at once without a slide. A click with no edge under the pointer
  keeps the mode running.
- **Cancel.** Right click or Escape ends the mode; leaving the mesh
  component modes ends it too.

| Command | Key | Action |
|---------|-----|--------|
| `Mesh_component_selection.loop_cut` | Ctrl+R | Start the loop cut mode |
| `Mesh_component_selection.loop_cut_more_cuts` | PageUp, Numpad+ | One more cut (at most 500) |
| `Mesh_component_selection.loop_cut_fewer_cuts` | PageDown, Numpad- | One cut fewer (at least 1) |
| `Mesh_component_selection.loop_cut_more_smoothness` | Alt+PageUp, Alt+Numpad+ | Smoothness + 0.05 (at most 1) |
| `Mesh_component_selection.loop_cut_less_smoothness` | Alt+PageDown, Alt+Numpad- | Smoothness - 0.05 (at least -1) |
| `Mesh_component_selection.loop_cut_digit_0` .. `_9` | 0 .. 9, Numpad 0 .. 9 | Type the cut count (digits accumulate; past 500 the count restarts from the digit) |
| `Mesh_component_selection.loop_cut_wheel` | Wheel, Alt+Wheel | Cut count, smoothness |
| `Mesh_component_selection.modal_confirm` / `modal_confirm_click` | Enter, left press | Cut, then confirm the slide |
| `Mesh_component_selection.modal_cancel` / `modal_cancel_click` | Escape, right press | Cancel the mode, then the slide |

The keys carry exact modifier masks and consume their input only while the
mode runs, so PageUp / PageDown (fly camera) and the digits (hotbar) keep
their bindings otherwise. While the mode runs, the wheel command is Ready
(it out-ranks the fly-camera zoom), the modal click commands own the clicks,
the component click, box, paint and loop select gestures, the G slide and
the gizmo drag stand down, and the pointer hover highlight is replaced by
the preview.

### Numeric form

`Mesh_component_selection_tool::loop_cut(target, edge_key, cuts, smoothness,
slide, result, error)` cuts the ring through one edge, slides the new loops
with one `Scalar_input` step (edge slide factor in [-1, 1], even, flipped)
and commits, all in one call and one undo entry (D6). MCP `loop_cut_mesh`
(`scene_name`, `node_id` / `node_name`, `primitive_index`, `edge` [v0, v1],
`cuts` default 1, `smoothness` default 0, `factor` default 0, `even`,
`flipped`; schema in `config/editor/mcp_tools.json`) calls it and returns
the selection plus `ring_length`, `ring_closed`, `inner_edges`,
`slide_vertices`, `moved_vertices`, `loops` and the result's vertex, edge and
facet counts. It needs a mesh component mode.

## Inset

Blender's inset faces (`doc/plans/mesh_modeling.md` section 4.8; the rules
are in `erhe_geometry/operation/inset_faces.hpp` and `doc/erhe/geometry.md`).

- **Start.** I in face mode, with the pointer over a viewport and a live
  face selection, starts the inset mode on the first live entry with
  selected facets (one mesh primitive). It is refused while a loop cut, a
  slide or another component edit runs. There is no preview step: the
  topology step runs at once.
- **Topology step.** `erhe::geometry::operation::inset_faces()` of the
  selected facets with thickness and depth 0 builds the result Geometry,
  swapped in as above; the inset facets become the selection (face mode
  stays). A region without boundary edges (every facet of a closed mesh)
  ends the mode with nothing changed.
- **Slide.** A `Scalar_edit_kind::inset` edit
  (`Transform_tool::begin_scalar_edit()`) moves each inset vertex to its
  start position plus thickness times its thickness direction plus depth
  times its depth direction (`Inset_faces_result`). The pointer drives the
  thickness: the change of its distance from the press position times the
  mesh units per pixel at the centroid of the inset vertices (one world
  unit along the camera's right axis, projected; divided by the node's mean
  scale), never below 0. With Ctrl held it drives the depth instead; a Ctrl
  press or release starts a new segment from the value reached, so neither
  value jumps.
- **Options.** The mode starts from the library defaults (boundary, even
  offset and interpolate on; relative offset, edge rail, outset and
  individual off). O, I, B, E and R toggle outset, individual, boundary,
  even offset and relative offset; each toggle cancels the running edit
  (the before primitive back) and re-runs the topology step with the new
  options, keeping the thickness and depth.
- **Confirm / cancel.** Enter or a left click commits one
  `Fork_geometry_operation` "Inset": the step's rebuild re-runs the inset
  from the before geometry with the final thickness and depth, so the
  result equals the numeric form's. Escape or a right click cancels: the
  before primitive and the selection come back and nothing is queued.
  Undo and redo decline while the mode runs.

| Command | Key | Action |
|---------|-----|--------|
| `Mesh_component_selection.inset` | I | Start the inset mode |
| `Mesh_component_selection.inset_toggle_outset` | O | Toggle outset |
| `Mesh_component_selection.inset_toggle_individual` | I | Toggle individual |
| `Mesh_component_selection.inset_toggle_boundary` | B | Toggle boundary |
| `Mesh_component_selection.inset_toggle_relative` | R | Toggle relative offset |
| `Mesh_component_selection.modal_toggle_even` | E | Toggle even offset |
| `Mesh_component_selection.modal_confirm` / `modal_confirm_click` | Enter, left press | Commit |
| `Mesh_component_selection.modal_cancel` / `modal_cancel_click` | Escape, right press | Cancel |

The keys carry the exact modifier mask 0 and consume their input only while
they apply: I starts the mode only while it does not run and toggles
individual only while it does, and O, B and R fall through otherwise (B to
the box gesture). While the mode runs the modal click commands own the
clicks and the selection gestures, the G slide and the gizmo drag stand
down.

### Numeric form

`Mesh_component_selection_tool::inset(options, result, error)` insets the
live face selection with the options' thickness and depth and queues one
`Fork_geometry_operation` "Inset" (D6); a region without boundary edges
changes nothing (`Inset_result::changed` false, nothing queued). The
Operations window's "Inset" button (Components section) runs it with the
window's thickness, depth and option checkboxes. MCP `inset_mesh_faces`
(`thickness`, `depth` default 0; `boundary`, `even_offset`, `interpolate`
default true; `relative_offset`, `edge_rail`, `outset`, `individual`
default false; schema in `config/editor/mcp_tools.json`) calls it and
returns the selection plus `changed`, `inset_vertices`, `inset_facets`,
`rim_facets` and the mesh's vertex, edge and facet counts. It needs face
mode.

## Bevel

Blender's bevel (`doc/plans/mesh_modeling.md` section 4.9, M13a and part of
M13b): edges only, offset types `offset` and `width`, loop slide, segments and
profile. A beveled edge becomes a strip of `segments` quads following the
profile (0 a straight chamfer, 0.5 a quarter circle, 1 the square corner);
at a vertex with three or more beveled edges the corner is filled by the
cutoff patch (a centre facet at the profiles' middle samples, triangles and
quads toward each corner). Blender's adjacent-pattern grid patch (ADJ),
offset adjustment, clamp overlap, vertex bevel and miters remain for M13b.
The rules are in `erhe_geometry/operation/bevel_edges.hpp` and
`doc/erhe/geometry.md`.

- **Start.** Ctrl+B in edge mode (the selected edges) or vertex mode (the
  edges between selected vertices), with the pointer over a viewport, starts
  the bevel mode on the first live entry with such edges (one mesh
  primitive). It is refused while a loop cut, inset, knife, slide or another
  component edit runs. There is no preview step: the topology step runs at
  once.
- **Topology step.** `erhe::geometry::operation::bevel_edges()` of the edges
  with amount 0 builds the result Geometry (valid topology, the new vertices
  at their original vertices), swapped in as above; the edge facets with
  their edges and vertices become the selection (the mode stays). A
  selection with only boundary or non-manifold edges ends the mode with
  nothing changed.
- **Slide.** A `Scalar_edit_kind::bevel` edit
  (`Transform_tool::begin_scalar_edit()`) moves each new vertex to its
  original vertex's position plus the amount times its direction
  (`Bevel_edges_result`; every placement rule, the profile samples
  included, is linear in the amount, so the directions are exact for any
  segments and profile and an amount change never re-runs the topology
  step). The pointer drives the amount: the pointer's
  distance from the press position times the mesh units per pixel at the
  centroid of the new vertices, as for inset.
- **Options.** The mode starts from the library defaults (offset type
  `offset`, loop slide on, one segment, profile 0.5). W cycles the offset
  type (offset, width), L toggles loop slide, PageUp / PageDown change the
  segment count (1 .. 32), ] / [ the profile by 0.05 (0 .. 1). S hands the
  mouse wheel to the segment count for the rest of the mode (before it the
  wheel zooms the camera); Shift+wheel changes the profile. Each change
  cancels the running edit and re-runs the topology step, keeping the
  amount.
- **Confirm / cancel.** Enter or a left click commits one
  `Fork_geometry_operation` "Bevel": the step's rebuild re-runs the bevel
  from the before geometry with the final amount, so the result equals the
  numeric form's. Escape or a right click cancels: the before primitive and
  the selection come back and nothing is queued. Undo and redo decline
  while the mode runs.

| Command | Key | Action |
|---------|-----|--------|
| `Mesh_component_selection.bevel` | Ctrl+B | Start the bevel mode |
| `Mesh_component_selection.bevel_cycle_offset_type` | W | Cycle the offset type |
| `Mesh_component_selection.bevel_toggle_loop_slide` | L | Toggle loop slide |
| `Mesh_component_selection.bevel_segments_wheel` | S | The wheel changes the segment count |
| `Mesh_component_selection.bevel_more_segments` / `bevel_fewer_segments` | PageUp / PageDown | One segment more / fewer |
| `Mesh_component_selection.bevel_more_profile` / `bevel_less_profile` | ] / [ | Profile + / - 0.05 |
| `Mesh_component_selection.bevel_wheel` | wheel (after S), Shift+wheel | Segments, profile |
| `Mesh_component_selection.modal_confirm` / `modal_confirm_click` | Enter, left press | Commit |
| `Mesh_component_selection.modal_cancel` / `modal_cancel_click` | Escape, right press | Cancel |

W, L, S, PageUp, PageDown, ] and [ carry the exact modifier mask 0 and
consume their key only while the mode runs, falling through otherwise (W and
S to the fly camera, PageUp / PageDown to the loop cut and the fly camera, L
to select linked; the bevel's L is declared first, so it sees the key before
select linked while the mode runs). The wheel command is Ready only while the
mode runs and declines a plain wheel before S, so the camera still zooms.

### Numeric form

`Mesh_component_selection_tool::bevel(options, result, error)` bevels the
live edge (vertex) selection with the options' amount and queues one
`Fork_geometry_operation` "Bevel" (D6); a selection without a bevelable edge
changes nothing (`Bevel_result::changed` false, nothing queued). The
Operations window's "Bevel" button (Components section) runs it with the
window's amount, offset type, loop slide, segments and profile. MCP
`bevel_mesh_edges` (`amount` default 0, `offset_type` `offset` | `width`
default `offset`, `loop_slide` default true, `segments` 1 .. 1000 default 1,
`profile` 0 .. 1 default 0.5; schema in `config/editor/mcp_tools.json`) calls
it and returns the selection plus `changed`, `beveled_edges`,
`boundary_vertices`, `edge_facets`, `vertex_facets` and the mesh's vertex,
edge and facet counts. It needs edge or vertex mode.

## Knife

Blender's knife (`doc/plans/mesh_modeling.md` section 4.7; the cut rules
are in `erhe_geometry/operation/knife_cut.hpp` and `doc/erhe/geometry.md`).
The knife has no slide: steps 1, 2 and 4 of the gesture lifecycle, with the
topology step at the confirm.

- **Start.** K over a content mesh in vertex, edge or face mode starts the
  knife mode, with cut through off and no constraint. It is refused while a
  loop cut, inset, slide or another component edit runs. The first cut
  point picks the mesh the session cuts (one mesh primitive per session)
  and the view it is cut from: a `Knife_view` built from the viewport's
  camera at that moment (`clip_from_mesh` = clip_from_world times
  world_from_node, the eye and the camera's -Z in mesh space, perspective
  unless the projection is orthographic), kept for the session.
- **Point under the cursor.** The hovered facet (the content hover) of the
  session mesh gives the point: `Screen_snap` (below) snaps it to the
  facet's nearest vertex within the vertex radius, else its nearest edge
  within the edge radius (the edge point under the pointer, with Shift the
  edge's midpoint), else it is the hover point inside the facet
  (`Knife_snap::vertex` / `edge` / `facet`). Ctrl held skips snapping. The
  edge radius is 10 pixels times the UI scale, divided by half the number of
  the knife's cut vertices (the ends of its pending cut edges) within twice
  that radius of the pointer when that shrinks it; the vertex radius is 0.75
  of it.
- **Constraints.** A cycles the angle constraint off, screen, relative: the
  pointer's screen direction from the previous point is rounded to 30 degree
  steps, measured from the screen X axis (screen) or from the screen
  direction of the edge the previous point lies on (relative; the screen X
  axis when the previous point is not on an edge), and the pointer moves to
  its projection on that direction. X / Y / Z lock the world axis through
  the previous point (the same key again unlocks; the lock wins over the
  angle constraint): the pointer moves to the projection of the axis point
  nearest to the pointer ray. A constrained point is where a ray at the
  moved position hits the session mesh (`Scene_root::get_raytrace_scene()`),
  a facet point, without snapping.
- **Preview.** In the hover color: the pending cut edges
  (`Knife_cut::get_preview_segments()`), the cut points, the point under the
  pointer, and the rubber band from the last point of the open polyline to
  it. With nothing of the session mesh under the pointer the rubber band ends
  on the view plane through the previous point, and a click adds nothing.
  The preview is recomputed on the hover, modifier and key changes only.
- **Cut points.** A left press adds the point under the pointer
  (`Knife_cut::add_point()`); holding the button and moving adds a point each
  time the point moved more than the snap radius since the last one, while
  cut through is off. A second press within 0.3 s and 6 pixels of the
  previous (a double click) closes the polyline back to its first point
  (three points or more; `Knife_cut::close_polyline()`) and ends it. A right
  press ends the polyline (`Knife_cut::end_polyline()`): the next point
  starts a new polyline, and the earlier cut edges stay. Ctrl+Z removes the
  last point (`Knife_cut::undo_last_point()`, which also drops an end or
  close after it). C toggles cut through: the session's record of points,
  ends and closes is replayed into a new `Knife_cut` with the option, since
  occlusion changes every segment. Nothing touches the mesh before the
  confirm, and undo and redo decline while the mode runs
  (`Operation_stack::get_undo_block_reason()`), so Ctrl+Z reaches the knife.
- **Confirm.** Enter or Space finishes the cut into a new Geometry
  (`Knife_cut::finish()`), builds its Primitive, swaps it in, switches to
  edge mode with the cut edges selected and queues one
  `Fork_geometry_operation` "Knife". Points that make no cut edge (a single
  point, two facet points in one facet) change nothing and queue nothing.
- **Cancel.** Escape ends the mode with the mesh untouched; so do leaving
  the mesh component modes and MCP `cancel_component_edit`. A swap of the
  session mesh's geometry by something else, or the mesh leaving its scene,
  ends the mode at the next hover or geometry change.

| Command | Key | Action |
|---------|-----|--------|
| `Mesh_component_selection.knife` | K | Start the knife mode |
| `Mesh_component_selection.modal_confirm_click` | Left press | Add a cut point (double click: close the polyline) |
| `Mesh_component_selection.modal_cancel_click` | Right press | End the polyline |
| `Mesh_component_selection.modal_confirm` / `knife_confirm` | Enter, Space | Cut (one undo entry) |
| `Mesh_component_selection.modal_cancel` | Escape | Cancel |
| `Mesh_component_selection.knife_undo_point` | Ctrl+Z | Remove the last cut point |
| `Mesh_component_selection.modal_toggle_clamp` | C | Toggle cut through |
| `Mesh_component_selection.knife_cycle_angle` | A | Angle constraint off / screen / relative |
| `Mesh_component_selection.knife_lock_x` / `_y` / `_z` | X, Y, Z | Lock (unlock) a world axis |

The keys carry exact modifier masks and consume their input only while the
mode runs (K only to start it), so Space (hotbar), A (fly camera), X / Z
(brush rotation) keep their bindings otherwise. The Undo command, which
Ctrl+Z also triggers, declines while the knife runs, so the knife's binding
sees the chord. The click commands are re-armed Ready every frame of the
mode (a press leaves them inactive) and act on the press only; the
component click, box, paint and loop select gestures, the G slide and the
gizmo drag stand down. Split (Y) and separate (P) decline while any modal
gesture runs (`has_component_mode_selection()` in the Operations window), so
Y reaches the knife's axis lock.

### Numeric form

`Mesh_component_selection_tool::knife_cut(target, view, points, options,
result, error)` runs the one polyline of `points` (mesh space) through a
`Knife_cut` seen from `view` (mesh space, from `make_knife_view()`) or, when
it is null, from the camera of the last hovered viewport, and commits it like
the confirm (D6). MCP `knife_cut_mesh` (`scene_name`, `node_id` /
`node_name`, `primitive_index`, `points` as a list of {`position` [x, y, z]
mesh-local, `snap` vertex | edge | facet, `vertex`, `edge` [v0, v1],
`facet`}, `cut_through`, `close` and an optional `view` {`eye` [x, y, z]
world, `direction` [x, y, z] world, `perspective`, `viewport_width`,
`viewport_height`, `clip_from_world` 16 floats column-major}; schema in
`config/editor/mcp_tools.json`) calls it and returns the selection plus
`changed`, `cut_vertices`, `cut_edges` and the result's vertex, edge and
facet counts. It needs a mesh component mode.

## Screen snap

`Screen_snap` (`tools/screen_snap.hpp`, `doc/plans/mesh_modeling.md` D5)
snaps a viewport pixel position to one facet of a mesh: it projects the
facet's corners with `Viewport_scene_view::project_to_viewport()` and
reports the nearest vertex within `vertex_radius_px`, else the nearest edge
within `edge_radius_px` (the edge point nearest to the pointer ray, or the
midpoint with `Screen_snap_edge_point::midpoint`), else nothing, with the
mesh-local, world and viewport positions of the snapped point.
`excluded_vertices` (a per-vertex flag span) removes vertices and the edges
at them from the candidates. `Screen_snap::get_snap_radius()` is the
density rule: with n candidate points within twice the base radius of the
cursor, base / (n / 2), never more than base. The projection scratch lives
on the object, so a call allocates nothing after warm-up. The knife uses it
for its cut points and the move transform mode for its vertex / edge snap
(`doc/editor/transform.md` "Snap to vertices / edges").

## MCP tools

- `loop_cut_mesh` - the numeric loop cut (above).
- `inset_mesh_faces` - the numeric inset (above).
- `bevel_mesh_edges` - the numeric bevel (above).
- `knife_cut_mesh` - the numeric knife (above).
- `cancel_component_edit` - cancels the knife mode, the G slide or the
  mesh component edit of a gizmo drag.

## Verification

`py -3 scripts/mesh_modeling_verify.py` checks loop cut through
`loop_cut_mesh` on a box (one and two cuts, a slide factor, the selection and
a single undo step), a ring closing across a quad on a Catmull-Clark box and
the single edge case on an octahedron, and through Ctrl+R in a viewport
(Escape in the preview, wheel + click + move + click, Escape in the slide);
inset through `inset_mesh_faces` on the box (one facet with an inset vertex
position, two adjacent facets, every facet, individual on two facets, each
with the selection and one undo step) and through I in a viewport (move +
Enter, move + Escape, the I and E option keys); bevel through
`bevel_mesh_edges` on the box (one edge with the edge quad selected at the
offset, every edge, width on one edge, one edge with three segments, every
edge with two segments, each with one undo step) and through Ctrl+B in a
viewport (move + Enter: one "Bevel" entry; W, L, Escape: unchanged, no entry;
S, two wheel steps, Enter: the three segment strip and one "Bevel" entry); the
knife through
`knife_cut_mesh` on the box (two edge midpoints across the top face, a
vertex to vertex diagonal, three points over two faces with and without cut
through, each with the selection and one undo step) and through K in a
viewport (click, move, click, Enter: one "Knife" entry; click, Escape; click,
click, Ctrl+Z, Enter: no cut).
