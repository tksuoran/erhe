# Point set editing

Stability: experimental

Viewport editing of points that belong to one target - a lattice node's
control points, a curve prim's control points and handle ends (and a
segments kit layout's segment handles), a line, grid, ring or scatter kit
layout node's layout handles - shares one interface and one gizmo path. The
three users are described in
`doc/editor/lattice_deform_geometry_node.md` "Viewport editing",
`doc/editor/curves.md` "Viewport editing" and `doc/editor/level_tools.md`
"Layout handles".

## The interface

`IPoint_set` (`tools/point_set_tool.hpp`) is what a point set provides:

- `update_point_set()` re-resolves the target when a change it depends on
  was reported and says whether there is one; `get_point_set_scene_root()`
  is the scene the points live in.
- `get_point_world_positions()` fills a caller-owned list with the points
  in world space, by a flat index the provider defines, in one pass (the
  lattice resolves its cage once per call). An index that is no editable
  point at the moment (the handle of a corner curve point) is empty and is
  skipped by the pick, the drawing and the anchor.
- The selection: `is_point_selected()`, `has_point_selection()`,
  `select_point(index, Point_select)` (`replace` or `toggle`) and
  `clear_point_selection()`, which reports false when the set keeps a
  selection (a lattice always has one selected point).
- `get_point_frame_rotation()`: the target's world rotation, the gizmo's
  frame in Local mode.
- `get_point_drawing()`: `all` draws every point as a handle, `highlighted_only`
  draws the selected and hovered points only, for a target whose points
  another drawing already shows, `gizmos` draws every point as a small
  translate gizmo and lets the pointer bring the Transform gizmo to the
  nearest one ("Hover gizmo"; the curve tool).
- The hovered point: `update_hot_point()` re-decides it, `get_hot_point()`
  reads it, `clear_hot_point()` drops it ("Hover gizmo").
- One gesture: `begin_point_edit()` snapshots, `apply_point_edit(world_delta)`
  writes the snapshot moved by the gizmo's world-space delta (no delta: the
  snapshot exactly, for a press without motion), `commit_point_edit()`
  records one undoable operation, `cancel_point_edit()` restores the
  snapshot.

## Shared code

`Point_set_tool` (`tools/point_set_tool.*`) is the base of `Lattice_tool`,
`Curve_tool` and `Kit_layout_region_tool`, a background tool at priority 4
(above the object selection):

- The pick: the point within 12 px of the pointer (screen space, through
  `Viewport_scene_view::project_to_viewport()`), nearest to the camera
  among those; only in viewports of the target's scene, and never while the
  pointer is over a gizmo handle.
- The click (`<prefix>.select`, left button on release, any modifiers): a
  plain click on a point replaces the selection, Ctrl or Shift toggles the
  point, as the object and mesh component selections do. A click that hits
  no point goes to the provider (`try_ready_miss()` / `on_select_miss()`),
  which consumes it or lets it reach the object selection.
- `<prefix>.cancel_edit` (Escape, exact modifier mask 0, so it dispatches
  before the mask-less Escape bindings): during a gizmo gesture of the tool's
  points it cancels the gesture (`Transform_tool::cancel_component_edit()`,
  also MCP `cancel_component_edit`); the rest of the drag moves nothing and
  its release records nothing.
- `tool_render()` draws the points of the set (`highlighted_only`: the
  hovered and selected ones) as camera-facing circles (outlines): orange, white when
  hovered, yellow when selected, at Hidden Strength (default 0.5) where
  geometry hides them, and over the curve lines (stencil reference 3), after the provider's `render_extras()`,
  which is given the hovered point (the curve tool draws the hovered and the
  selected segment's chord). The
  colors, sizes (radius per metre of distance to the camera), line width and
  hidden strength are the
  `point_*` fields of `Debug_visualizations_style`, one group for both tools
  (Settings window, Debug Visualizations Style > Point Handles), read at draw
  time.

## Hover gizmo

A point set whose drawing is `gizmos` (the curve tool: control points,
handle ends and segment handles) needs no click before a drag:

- Every point draws as the camera-facing circle of "Shared code" (orange,
  white under the pointer, yellow when selected), except the point the
  gizmo sits on: the gizmo stands in for its circle.
- On a point's gizmo the arrows start right
  outside the plane sectors (one `plane_sector_gap` beyond them): there is
  no rotate ring to clear, and the gizmo covers a smaller area. Each axis
  shows one arrow, the one towards the eye, whatever Translate Negative
  Handles says and also while a plane sector is hovered: the other could
  hide behind a plane sector. An axis drag shows both, as on every gizmo.
  While a plane sector is hovered the arrow perpendicular to that plane
  stays shown, dimmed by `hover_dim_factor` like the other two sectors (on
  the Transform gizmo of a node it hides).
- Sizes are the point gizmo's own (Settings window, Transform Tool):
  `point_gizmo_scale` for the gizmo on the hovered point, in the units of
  `gizmo_scale` and independent of it.
- A point's gizmo bounds: the pointer is within the 12 px pick radius of
  the point, or inside the screen-space bounding box of the handles the
  full-size gizmo shows there (`Handle_visualizations::get_point_gizmo_extent()`:
  the arrow tips and the plane sectors), 4 px wider on each side
  (`c_gizmo_bounds_margin`).
- The pointer within 24 px (`c_hover_radius`) of a point and within that
  point's gizmo bounds makes the nearest such point the hovered point (of
  coincident points the first: a control point over its zero-length handle).
  The Transform gizmo then sits on that point, at the point gizmo scale, with the
  translate handles alone whatever tool is active
  (`Transform_tool::get_shown_handle_tools()`).
- Without a hovered point the set shows no gizmo: selected points away from
  the pointer are their yellow circles, and the anchor stays on their
  centroid for value edits (`Transform_tool::is_point_gizmo()`).
- The hovered point gives the gizmo back, and draws as a circle again, as
  soon as the pointer is outside its gizmo bounds and on no handle of it
  (`Transform_tool::is_pointer_on_handle()`, picked with the control ray the
  pointer position is taken from). While the pointer is within the bounds
  another point takes the gizmo only from within the pick radius.
- A press on a handle of the hovered point's gizmo
  (`begin_pointer_drag()`) selects the point: replacing the selection, or
  adding to it with Ctrl or Shift held and the pointer within the pick
  radius of the point (on an arrow they are the drag's modifiers). The drag
  moves the selection, so a hovered point that already is selected drags
  the whole selection. An unmoved release on a point that was selected at
  the press is a click on it (`end_pointer_drag()`): it becomes the only
  selected point, or with Ctrl or Shift leaves the selection. The gizmo's
  centre sector covers the point, so a click on the hovered point follows
  the same rules as the click of "Shared code".
- A value edit (a Transform window field, MCP `transform_selection`) acts on
  the selection about its centroid, never on the hovered point: it drops the
  hovered point first (`Component_edit_origin::value`).
- `Transform_tool::update_for_view()` calls `update_hot_point()` each idle
  frame before it asks for the anchor; the pointer has no change
  notification. MCP `get_curve_point_selection` reports it as `hover_point`.

## Point selection

The point selection stays until the user selects something else; a gizmo
drag, its release and the operation it records leave it as it was, so the
next drag needs no new click.

- A path or lattice replaced with the same points (a move, its undo or redo)
  keeps every index. The curve tool drops only the entries that no longer
  exist after a change from elsewhere (a removed point, a handle of a point
  that became a corner); its own insert and add select the new point, its own
  removal clears the selection.
- The curve tool re-resolves its target on a `Selection_message`, not while
  its own gizmo gesture runs; a message that names the same single curve
  keeps the selection. It clears when the target changes: another item or
  more than one item selected, the curve deselected, deleted, removed by an
  undo, or its scene closed.
- A removal operation (`Item_insert_remove_operation`) keeps the selection
  of the items that stay, so the regeneration of a locked path layout under
  a drag of its own curve (children removed and inserted in the drag's entry)
  leaves the curve selected throughout and never empties the selection in
  between.
- The lattice tool's selected point is a value of the lattice node and is
  always one point; a drag leaves it selected.
- The curve tool's segment handles: one segment at a time, exclusive with
  the point selection (selecting either clears the other); kept through its
  drags and their undo and redo, dropped when a change leaves fewer
  segments or the target changes.
- The region tool holds at most one selected handle, kept through its drags
  and their undo and redo, and cleared when the target changes or its
  region or kind does (the indices then name other handles). Its drag gives
  up when the node leaves the selection mid-drag, restoring the start.

`Point_set_transform` (`transform/point_set_transform.*`) is the gizmo
adapter. `Transform_tool::update_for_view()` asks, each idle frame while no
mesh component mode is active, the lattice tool, the curve tool and then the
region tool; the first whose `update_point_set()` holds and that has a hovered or a selected point owns the
gizmo (`Component_source::point_set`, `Transform_tool_shared::component_mode`).
The anchor is the hovered point, else the centroid of the selected points, in the provider's frame
rotation; a drag, a numeric Transform window edit and MCP `transform_selection`
hand `updated_world_from_anchor * inverse(initial)` to the provider. Without a
hovered or selected point the gizmo returns to the node selection.

What the point path honors: the Move tool's axis and plane constraints and
its translate snap (Snap Enable or Ctrl held, relative or absolute); the
Rotate and Scale tools turn and scale the selected points about the anchor.
The surface settings (Snap To Surface, Orient To Surface) rest moved
meshes and do not act on points (`Transform_tool::snap_to_surface()` refuses
component mode).
