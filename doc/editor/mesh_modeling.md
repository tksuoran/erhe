# Mesh modeling tools

Stability: experimental

The modal mesh modeling tools of the editor: gestures that change a mesh's
topology interactively in a viewport, in a mesh component mode
(`doc/editor/mesh_component_selection.md`). Loop cut is the one tool that
exists; inset, knife and bevel follow the same gesture lifecycle
(`doc/plans/mesh_modeling.md`, which holds their design and the Blender
behaviour each follows). The discrete operations (delete, dissolve, merge,
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

## Verification

`py -3 scripts/mesh_modeling_verify.py` checks loop cut through
`loop_cut_mesh` on a box (one and two cuts, a slide factor, the selection and
a single undo step), a ring closing across a quad on a Catmull-Clark box and
the single edge case on an octahedron, and through Ctrl+R in a viewport
(Escape in the preview, wheel + click + move + click, Escape in the slide).
