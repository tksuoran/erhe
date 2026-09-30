# Mesh modeling operations: remaining work

Status: in progress

The Blender-style modeling operations of the editor exist and are described
in the current documents: the modal tools (loop cut, inset, knife, bevel,
their gesture lifecycle, keys, snapping and MCP tools) in
`doc/editor/mesh_modeling.md`; the selection commands, loop and ring select,
box and paint select and the per-operation keys and MCP tools in
`doc/editor/mesh_component_selection.md`; the slides, the scalar drag path
and cancel in `doc/editor/transform.md` "Scalar edits"; the discrete
operations (delete, dissolve, merge, subdivide, split, rip, separate, fill,
connect, bridge, flip and recalculate normals, smooth) in
`doc/editor/operations.md`; the library side (the topology walkers,
`Edit_mesh` and its primitives, each `erhe_geometry/operation/*.hpp`) in
`doc/erhe/geometry.md`. `scripts/mesh_modeling_verify.py` checks every
operation headless, `erhe_geometry_tests` covers the library. This plan
holds what is not built, with the design decisions the remaining items
inherit and the Blender files each rule was read from.

## 1. Design decisions the remaining work inherits

- **D1. Blender is the behaviour reference, never the source.** erhe is
  MIT-licensed and Blender is GPL. Each remaining item states its rules as
  Blender's editor exposes them; erhe code is written against erhe's own
  structures and no Blender source is transcribed. Section 5 lists the
  Blender files for re-checking a rule.
- **D2. Discrete operations compose `Edit_mesh` primitives** and emit
  through `Edit_mesh_operation` (`doc/erhe/geometry.md`), so attributes
  interpolate, `edge_sharpness` propagates and the selection remaps.
- **D3. Interactive operations build topology once, then preview positions
  in place** through the scalar path of `Mesh_component_transform`, commit
  one `Fork_geometry_operation` and cancel by restoring the before
  primitive (`doc/editor/mesh_modeling.md` "Gesture lifecycle").
- **D6. Every operation has an MCP tool and a numeric path**, and
  `scripts/mesh_modeling_verify.py` gains a case per operation.
- **D7. Keys follow Blender where the editor's bindings allow it**, bound
  with an exact modifier mask through the Commands overrides
  (`doc/editor/input_bindings.md`); mode-dispatching commands decline
  during a modal gesture through the shared guard in `Operations`.

## 2. Remaining items

Impact and effort are 1 (low) to 5 (high); score is `2 * impact - effort`.

| Id | Item | Impact | Effort | Score |
|----|------|-------:|-------:|------:|
| R1 | Grid fill (section 3.1) | 3 | 4 | 2 |
| R2 | Bevel: adjacent-pattern vertex patch, offset adjustment, clamp overlap, vertex bevel, miters, the other offset types (section 3.2) | 3 | 5 | 1 |
| R3 | Knife project and bisect (section 3.3) | 2 | 3 | 1 |
| R4 | Separate by loose parts and by material | 2 | 2 | 2 |
| R5 | Proportional editing falloff for the component transform | 3 | 2 | 4 |
| R6 | Symmetry (mirror) editing across a chosen axis of the component transform | 3 | 3 | 3 |
| R7 | Triangles to quads on the selection, selection-aware triangulate | 2 | 2 | 2 |
| R8 | Shortest-path select (Ctrl+click), select similar, checker deselect | 2 | 2 | 2 |
| R9 | Extrude individual faces, extrude along a picked axis; spin and screw | 2 | 3 | 1 |
| R10 | Compute-shader box and lasso select for vertices and edges with occlusion, replacing the CPU projection (`doc/plans/mesh_component_selection.md`) | 3 | 3 | 3 |
| R11 | Small follow-ups (section 3.4) | 2 | 1 | 3 |

Items are taken up in score order when a user asks for them.

## 3. Behaviour of the remaining items

### 3.1 Grid fill (R1)

Options: span (1, auto-computed), offset 0, simple interpolation off. A
single closed loop of even length splits at its sharpest corner (rotated by
offset) and the next sharpest, or at quarter points when the corners are
alike, into two rails and two arcs; unequal arcs are expanded by splitting
edges that are collapsed again afterwards; interior points come from each
boundary row mapped through the triangle frame of its side and blended by
row fraction (simple: a four-side weighted blend); vertex attributes use the
same weights; corner attributes interpolate from the boundary corner pairs
on the sides that have facets; winding votes from the boundary facets. It
joins `fill_selection()` as the case for one closed loop of even length
when the user asks for a grid (a "Grid Fill" button and MCP argument), the
plain n-gon fill staying the F default.

### 3.2 Bevel, second version remainder (R2)

`bevel_edges()` has segments and profile with the cutoff vertex patch.
Remaining: the adjacent-pattern patch (a coarse grid over the three or more
profiles at a vertex, cubically subdivided toward the profile, Blender's
default for three or more beveled edges); offset adjustment (solve the
offset chains and cycles so widths match at both ends of every edge; skipped
for percent / absolute types); clamp overlap (limit the amount so no
boundary vertex passes another); vertex bevel (affect vertices: a polygon
replacing each selected vertex, the edges shortened, with segments); the
outer miters sharp / patch / arc and inner sharp / arc with spread; the
offset types depth, percent and absolute; harden normals and face strength.

### 3.3 Knife project and bisect (R3)

Knife project: the boundary edges of the other selected meshes, projected
to the view, are replayed as knife polylines over the active mesh with
snapping off; afterwards the facets whose interior point lies inside the
projected polygons are selected, growing across edges that were not cut.
Bisect: an interactive straight-line gesture whose plane contains the
screen line and the view direction (point: the start unprojected at the
view pivot depth), with flip, fill (the cut loops filled as n-gons) and
clear inner / outer; vertices classify by signed distance with a threshold,
edges strictly crossing split at the plane, each facet with crossings splits
between its on-plane vertices sorted along the facet's in-plane direction,
with an inside parity so a concave facet crossing several times splits
correctly.

### 3.4 Small follow-ups (R11)

- The Edit menu's Delete declines while a component selection is live;
  route it to the component delete as the Delete key is.
- A no-op result (limited dissolve that changes nothing, nothing to fill)
  still records an undo entry; detect the no-op before queueing.
- `Mcp_test` injected-input cases for the G slide, loop cut, inset, knife
  and bevel gestures (the verify script covers them today).
- On-screen drawing of slide rails and the modal tools' option values,
  and pointer wrap at the viewport edge during a scalar drag.
- The move transform mode's vertex / edge snap option has no runtime check.
- Bevel at a mesh-boundary vertex (open fan) has no cap facet and no test.
- The bridge of three loops in an open chain skips the middle pair because
  every erhe loop has facets (Blender leaves wire loops); a bridge over a
  loop with facets on both sides needs those facets deleted first.
- The multiview overlays of `doc/plans/mesh_component_selection.md`.

## 4. Verification

- `erhe_geometry_tests`: one file per library operation asserting counts,
  positions, the remapped selection, corner texcoords across a seam and
  `edge_sharpness` survival; every result passes `validate()`.
- `scripts/mesh_modeling_verify.py`: headless editor; for every MCP action
  and key path, select components on a box or grid, run, check the counts
  and selection, undo and check the counts return.
- `scripts/geometry_edit_node_order_verify.py`: one case per primitive
  swap and per node insertion, so the hierarchy order stays put.
- Steady-state allocation: the preview paths use persistent scratch; a
  hover frame allocates nothing (`AGENTS.md` "Run-time Memory Allocation
  Discipline"). `Edit_mesh` runs on the operation's worker and may allocate.

## 5. Reference

Blender's implementations, in a Blender source checkout under
`source/blender/` (behaviour reference only, D1):

| Subject | Files |
|---------|-------|
| walkers, selection | `bmesh/intern/bmesh_walkers_impl.cc`, `editors/mesh/editmesh_select.cc` |
| dissolve, delete | `bmesh/operators/bmo_dissolve.cc`, `bmesh/tools/bmesh_decimate_dissolve.cc`, `bmesh/intern/bmesh_delete.cc`, `bmesh/intern/bmesh_mods.cc`, `bmesh/intern/bmesh_core.cc` |
| merge | `editors/mesh/editmesh_tools.cc`, `bmesh/operators/bmo_removedoubles.cc` |
| loop cut, subdivide | `editors/mesh/editmesh_loopcut.cc`, `editors/mesh/editmesh_preselect_edgering.cc`, `bmesh/operators/bmo_subdivide.cc` |
| slide | `editors/transform/transform_convert_mesh.cc`, `transform_mode_edge_slide.cc`, `transform_mode_vert_slide.cc` |
| knife, bisect | `editors/mesh/editmesh_knife.cc`, `editmesh_knife_project.cc`, `editmesh_bisect.cc`, `bmesh/tools/bmesh_bisect_plane.cc` |
| inset | `bmesh/operators/bmo_inset.cc`, `editors/mesh/editmesh_inset.cc` |
| bevel | `bmesh/tools/bmesh_bevel.cc`, `editors/mesh/editmesh_bevel.cc` |
| bridge, fill, connect | `bmesh/operators/bmo_bridge.cc`, `bmesh/intern/bmesh_edgeloop.cc`, `bmo_create.cc`, `bmo_edgenet.cc`, `bmesh/tools/bmesh_edgenet.cc`, `bmo_fill_edgeloop.cc`, `bmo_fill_grid.cc`, `bmo_connect.cc`, `bmo_connect_pair.cc` |
| keys | `scripts/presets/keyconfig/keymap_data/blender_default.py` |
