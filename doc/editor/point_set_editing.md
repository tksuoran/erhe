# Point set editing

Stability: experimental

Viewport editing of points that belong to one target - a lattice node's
control points, a curve prim's control points and handle ends, a scatter kit
layout node's region handles - shares one interface and one gizmo path. The
three users are described in
`doc/editor/lattice_deform_geometry_node.md` "Viewport editing",
`doc/editor/curves.md` "Viewport editing" and `doc/editor/level_tools.md`
"Scatter region handles".

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
  another drawing already shows.
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
- `tool_render()` draws the points as camera-facing quads: orange, white when
  hovered, yellow when selected, after the provider's `render_extras()`. The
  colors and sizes (half size per metre of distance to the camera) are the
  `point_*` fields of `Debug_visualizations_style`, one group for both tools
  (Settings window, Debug Visualizations Style > Point Handles), read at draw
  time.

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
- The region tool holds at most one selected handle, kept through its drags
  and their undo and redo, and cleared when the target changes or its
  region or kind does (the indices then name other handles). Its drag gives
  up when the node leaves the selection mid-drag, restoring the start.

`Point_set_transform` (`transform/point_set_transform.*`) is the gizmo
adapter. `Transform_tool::update_for_view()` asks, each idle frame while no
mesh component mode is active, the lattice tool, the curve tool and then the
region tool; the first whose `update_point_set()` holds and that has a selected point owns the
gizmo (`Component_source::point_set`, `Transform_tool_shared::component_mode`).
The anchor is the centroid of the selected points in the provider's frame
rotation; a drag, a numeric Transform window edit and MCP `transform_selection`
hand `updated_world_from_anchor * inverse(initial)` to the provider. Without a
selected point the gizmo returns to the node selection.

What the point path honors: the Move tool's axis and plane constraints and
its translate snap (Snap Enable or Ctrl held, relative or absolute); the
Rotate and Scale tools turn and scale the selected points about the anchor.
The surface settings (Snap To Surface, Orient To Surface) rest moved
meshes and do not act on points (`Transform_tool::snap_to_surface()` refuses
component mode).
