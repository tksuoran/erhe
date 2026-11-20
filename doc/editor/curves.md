# Curves

Stability: experimental

A curve is a scene prim of its own class, `erhe::scene::Curve`
(`src/erhe/scene/erhe_scene/curve.hpp`), holding one 3D curve: an ordered list
of control points in the prim's own frame, open or closed. The requirements
and rulings this follows are in `doc/plans/curves.md`; the curve math is
`erhe::math` "Curve path" (`doc/erhe/math.md`).

## The prim class

- `Curve` sits below `Gprim` in the prim class hierarchy
  (`doc/erhe/scene.md` "Prim levels"), the level of USD `UsdGeomBasisCurves`
  (`Curves : Gprim`). Its type bit is `erhe::Item_type::curve`, its type name
  and class token `Curve`. It is a transformable prim: it is named,
  parented, moved, duplicated, copied, pasted and deleted as every other prim
  is, and any prim can be its child.
- The data is one `erhe::math::Curve_path`: `Curve_point`s (a position, an in
  and an out handle stored as offsets from the position, and a kind -
  `corner`, `smooth` or `free`) and a `Curve_closure`. A curve of corner
  points only is a polyline. The control points are positions in space, not
  in time; a curve is not an animation.
- `Curve::set_path()` is the one writer. It replaces the path, increments the
  curve's revision (`get_revision()`), rebuilds the baked
  `erhe::math::Curve_path_table` (`get_table()`: spans, arc-length table,
  evaluation by distance) and the draw points (`get_draw_points()`: the curve
  as a polyline in the prim's frame - the two ends of a line span, 24 steps of
  a cubic span), and tells the scene host. Every consumer reads the table and
  the draw points the curve holds; nothing rebuilds them per frame.
- A clone (duplicate, copy and paste, prefab instancing) copies the path and
  builds its own table.
- A new curve (`erhe::scene::make_default_curve_path()`) has two smooth points
  1 m apart along +X with handles that arc the curve towards -Z.

## Change notification

A curve's users are told by the change itself (C7):

1. `Curve::set_path()` calls `Scene_host::on_curve_changed(curve,
   Curve_change::shape)`; `Curve::handle_transform_update()` - every write of
   the prim's world transform, including the propagation of a parent's move -
   calls it with `Curve_change::transform`. The host hook has an empty default,
   so a host without curve users ignores it.
2. `Scene_root::on_curve_changed()` (any thread) queues the curve once, noting
   whether its path changed.
3. `App_scenes::flush_curve_changes()` runs once per frame in `Editor::tick`,
   after the transform propagation, and sends one `Curve_changed_message`
   (`App_message_bus::curve_changed`: the scene root, the curve, its revision
   and whether the path changed) per queued curve that is still in the scene.
   A frame without a change sends nothing.

So an edit, its undo and its redo, a move of the curve or of an ancestor, and
a live Properties drag (one message per frame of the drag) each reach the
subscribers once per frame they happen in. `Scene_root` counts the messages
(`get_curve_change_message_count()`), which the MCP replies report.

`Scene_root` also keeps the list of its curve prims at node registration and
unregistration (`get_curves()`), which the drawing and `get_curves` read.

## Persistence

- glTF: a curve is a glTF node carrying the `ERHE_curve` node extension
  (`doc/gltf_extensions/ERHE_curve.md`, writer and reader in
  `erhe_gltf/gltf_curve.*` and `gltf_fastgltf.cpp`). The numbers are native
  JSON numbers in their shortest float form, so a save and a load reproduce
  every value bit for bit (`erhe_gltf_tests` `Gltf_curve.*`).
- USD: not carried yet. A USD save writes a curve as an `Xform` with a
  warning naming it; the `BasisCurves` mapping is `doc/plans/curves.md`
  step 7.

## Undo

Every edit of a curve's path is one `Curve_set_operation`
(`operations/curve_set_operation.*`), which holds the whole path before and
after and sets it on execute and undo, so undo restores points, handles,
kinds and closure exactly. Its first execute records the operations the
change implies on the curve's users ("Users of a curve") and runs them; redo
runs them again after the path, undo undoes them before it, so an edit and
its consequences are one undo entry. Creating a curve is an
`Item_insert_remove_operation`.

## Users of a curve

A path or segments kit layout is a curve node itself: the Curve prim carries
the kit layout values and has the pieces as its children
(`doc/editor/level_tools.md` "Path layouts", R5.12.8, and "Segment layouts",
R5.12.10). A path layout's pieces are laid out along its own path in its own
frame; a segments layout puts one piece, of the chord's length
(`child.length`, never scaled), on the straight chord between each two
neighbouring control points (handles do not
bend it). A stairs kit layout is a curve node the same way, with one
generated child: a staircase that follows the curve in plan and in height,
handles included (`doc/editor/level_tools.md` "Stairs layouts", R5.12.13).
All three are driven by the change, never by a poll:

- A path edit: `Curve_set_operation` asks
  `Level_kits::append_curve_follow_ups()` for the generation of the curve when
  it is a locked path, segments or stairs layout and holds it as its follow-ups, so every edit
  path (MCP, the curve tool's commands and its drag release, the Properties
  rows) lays the pieces out in the edit's own entry. A live drag (the curve
  tool, a Properties drag) writes the path without an operation and the
  pieces follow each step without one (`doc/editor/level_tools.md` "Path
  layouts"); the release's operation records the layout change.
- A move of the node: the pieces are its children and ride with it rigidly;
  with a surface setting on, the Transform tool's end of move and
  `set_node_transform` add the generation that snaps them to the surface again to the move's
  entry (`Level_kits::append_layout_move_operations()`).
- `Level_kits::mirror()` reflects a curve's points across its local x = 0
  plane in the mirror's entry, so a mirrored curve is the exact mirror image.

## Creating a curve

- The Hierarchy's Create menu (`Curve`, from the child prim catalog
  `scene/child_prim_types.cpp`), the main menu `Create > Curve`
  (`scene.create_new_curve`), the Create window's `Curve` button:
  `Scene_commands::create_new_curve()`, the default curve under the given
  prim, one undo entry.
- MCP `create_curve` and `create_child_prim` with type `curve`.

The Hierarchy shows a curve with its own icon (`Icon_set::get_item_icon`).

## Drawing

`Debug_visualizations::curves_visualization()` draws the curves of the view's
scene from the scene's curve list with the line renderer: a selected (or
hovered) curve as its draw points, a marker on each control point (yellow;
orange for a corner) and, for a smooth or free point, a line to each handle end
with a marker there (magenta). The Debug Visualizations window's `Curves` mode
picks which curves draw: `Off`, `Selected` (the default) or `All`, where an
unselected curve is a thinner dim line. The colors (with alpha), line widths
and marker size are the `curve_*` fields of `Debug_visualizations_style`
(Settings window, Debug Visualizations Style > Curves; saved in
`editor_settings.json`); the drawing reads them each time it draws, so an edit
shows on the next frame. The defaults: a selected curve 4 px bright cyan, an
unselected one 2 px dim cyan, handle lines and markers 2 px. The drawing transforms the cached draw
points by the prim's world transform and allocates nothing in the steady
state. The lines are drawn in the line renderer's x-ray mode, so a curve
lying on a surface (a path on the ground) or behind geometry keeps its full
strength. The curve tool draws a handle quad on every control point and handle
end of its curve (orange; white hovered, yellow selected; the Point Handles
style shared with the lattice tool, `doc/editor/point_set_editing.md`) on top
of it ("Viewport editing").

## Viewport editing

`Curve_tool` (`tools/curve_tool.*`) is a point set tool
(`doc/editor/point_set_editing.md`): a background tool whose target is the
curve while it is the only selected item. It has no hotbar entry; selecting a
curve is the activation, as a lattice designation is for the lattice tool.

- The point set holds three entries per control point (`Curve_point_part`):
  the point, its in handle end and its out handle end; the handles of a
  corner are not editable. The selection belongs to the tool; when it is kept
  and cleared is `doc/editor/point_set_editing.md` "Point selection". A
  curve that is a segments kit layout has one more entry per segment, its
  segment handle (`doc/editor/level_tools.md` "Segment handles").
- A click on a point or a handle end selects it; Ctrl or Shift toggles it. A
  plain click off the points clears the point selection first, so the next
  click reaches the object selection. Ctrl+click on the curve (within 12 px of
  its drawn line, away from a point) inserts a point there, as Ctrl+click near
  a curve inserts a key in the animation window, and selects it.
- With a point selected the Transform gizmo sits at the selection's centroid
  and moves it (the curve prim's rotation is the gizmo's Local frame); with
  none it moves the curve prim. Moving a control point carries its handles
  (offsets; a rotate or scale turns and scales them with the point). Moving a
  handle end goes through `erhe::math::set_curve_handle()`: on a smooth point
  the other handle turns opposite, keeping its length; holding Alt during the
  drag moves the dragged handle alone and the point becomes free (Alt is read
  live each drag step, so releasing it before the button keeps the point
  smooth and aligned) - the animation window's convention of aligned handles
  with Alt freeing one side. A handle end selected together with its point
  rides with the point.
- A drag writes the path at every drag step that changes it (one
  `Curve_changed_message` per such frame, none while the pointer rests) and
  its release records exactly one `Curve_set_operation` from the path at the
  press; Escape during the drag restores it and records nothing.
- `Curve.remove_points` (Delete, exact modifier mask 0; `Selection.delete`
  declines while curve points are selected) removes the control points whose
  point is selected; a curve keeps two points, a removal that would leave
  fewer is refused with a warning in the log.
  `Curve.add_point_at_start` / `Curve.add_point_at_end` (open curves) extend
  the curve along its end tangent by the end span's chord (a corner beyond a
  corner end, else a smooth point with handles a third of that chord) and
  select the new point; `Curve.set_kind_corner` / `_smooth` / `_free` set the
  kind of every control point with a selected part (the kind-change handle
  rule of `erhe::math::set_curve_point_kind()`); `Curve.toggle_closed` closes
  or opens the curve. These have no default keys; the Properties window's
  `Curve` group shows them as the `Add Point` (At Start, At End) and `Selected
  Points` (Corner, Smooth, Free, Remove) rows while the curve is the tool's
  target. Each edit is one `Curve_set_operation`.
- An insert splits the span with de Casteljau's construction
  (`erhe::math::insert_curve_point()`), so the shape stays the same: a line
  span gives a corner and two line spans, a cubic span a smooth point; a
  neighbor that gave the span no handle (a corner, or a zero handle) gets the
  split's handle and becomes free, its other side unchanged (a corner's other
  handle is zeroed, keeping that span straight).
- The gizmo path honors the axis and plane constraints and the translate
  snap; the surface settings do not act on curve points
  (`doc/editor/point_set_editing.md`).

## Properties

A curve's Properties section has the `Curve` group
(`Properties::curve_properties`): `Closed` (a checkbox), `Points` and
`Length` (read-only), and one `Point i` group per control point with its
`Position`, `Kind` and, for a smooth or free point, its `In Handle` and `Out
Handle`, each edited as numbers. A drag applies live and its release records
one undo entry from the path at the start of the drag; a kind change applies
the handle rule of `erhe::math::set_curve_point_kind()` (to corner zeroes the
handles, from corner gives Catmull-Rom handles, free to smooth aligns the in
handle); a handle edit of a smooth point turns the other handle opposite
(`erhe::math::set_curve_handle()`). The rows use the key-based row ids
(`doc/editor/properties_window.md` section 5), the point index as the
instance. The prim's `Gprim` rows (`Rendering`) are listed as for every
geometric prim.

## MCP

`mcp/mcp_server_curves.cpp`, schemas in `config/editor/mcp_tools.json`:

- `create_curve`: name, parent, world position and rotation, points (each
  `position`, optional `in`, `out`, `kind`; at least 2) and `closed`; the
  default curve without points. One undo entry, inserted at once.
- `get_curves`: every curve of the scene without the point lists.
- `get_curve`: one curve - points in the prim's frame (and in world space with
  `world`), length, point distances, world pose, revision and the scene's
  change message count.
- `set_curve`: replaces the points, the closed setting or both; one undo
  entry, none when nothing changes.
- `curve_set_point_kind`: one point's kind with the kind-change handle rule;
  one undo entry.
- `evaluate_curve`: at distances in the prim's own units (clamped on an open
  curve, wrapped on a closed one): world-space position, tangent and
  up-keeping frame, and the local position and tangent; read-only.
- `curve_add_point`: `at` `start` / `end` (open curves) with the computed end
  point, a `position` in the prim's frame or exact `point` values; `at`
  `curve` with a `distance` or a world-space `position` (the nearest place,
  `Curve_path_table::get_nearest_location()`) splits the span there. One undo
  entry; the reply adds the new point's `index` (and the split `location`).
- `curve_remove_points`: `indices`; one undo entry.
- `select_curve_points` / `get_curve_point_selection`: the curve tool's point
  selection (`index`, `part` `point` / `in` / `out`, world position). Selecting
  makes the curve the only selected item; the gizmo then moves the points, so
  a script moves them with `transform_selection`, `get_transform_handles` and
  `mouse_drag` as a user does. `cancel_component_edit` cancels a held drag.
- The viewport commands are callable by name (`Curve.set_kind_free`,
  `Curve.toggle_closed`, ...).

The action tools refuse unknown arguments, values that are not finite and
fewer than two points, and refuse to edit a curve inside a reference instance
(its path is not an override).

## Verification

- `erhe_scene_tests` `Curve_prims.*`: the class, the table and draw points,
  the clone, the host notification.
- `erhe_gltf_tests` `Gltf_curve.*`: the JSON form bit-exact and its
  refusals, a curve prim's glTF round trip.
- `mcp_server_tests` `Mcp_test.document_edits_record_one_undo_entry_each`:
  `create_curve`, `set_curve`, `curve_set_point_kind`.
- `python3 scripts/curve_verify.py` (self-launching headless editor; restores
  the config files it touches): creation through MCP and the UI, reads,
  `set_curve` and undo / redo, evaluation against known answers (a straight
  polyline, an L-shaped polyline, a quarter-circle approximation, a closed
  curve wrapping), world-space evaluation of a moved and turned prim,
  duplicate, copy and paste, delete and undo, a glTF save and reopen, the
  change message count, Properties row edits, and a screenshot of a selected
  curve (`logs/curve_selected.png`); the lattice tool's viewport editing
  (a click picks a control point, a gizmo X drag moves it, one undo entry);
  the curve tool (a click at a point's projection picks it, gizmo axis, plane
  and Ctrl-snapped drags, Escape cancel, handle drags with and without Alt,
  a two-point move, `transform_selection`, Ctrl+click insert keeping the
  sampled shape within 1e-5, Delete and its refusal, the kind and closed
  commands, the MCP point edits and refusals, the change message count of a
  drag, the selection clearing on deselect and delete) with a screenshot of
  a selected point and its handles (`logs/curve_point_selection.png`).
  `--sections base,lattice,viewport` picks parts. Scripted gizmo drags press
  and release at whole pixels (`doc/agents/mcp_ui_driving.md` "Coordinate
  space"); the drag checks allow 0.75 px of the pointer travel and print the
  shortfall.

## Future work

`doc/plans/curves.md`: the 2D Bezier editor (step 5), the sweep spine from a
curve (step 6), the USD form (step 7) and surface snap of point drags (step 8).
