# Curves: curve nodes, viewport editing, curve kit layouts, 2D Bezier editor

Status: in progress

Requirements, design outline and build steps for 3D curves as scene nodes:
a curve node type, editing of its control points in the 3D viewport, kit
layout nodes that place pieces along a curve
(`doc/plans/level_tools.md` R5.12, `doc/editor/level_tools.md` "Kit layout
nodes"), and a separate 2D Bezier editor widget. The shared pieces this
builds on are named in "What the work builds on". The curve node as built -
its class, data, persistence, drawing, viewport editing, Properties rows and
MCP tools - is described in `doc/editor/curves.md`, the point set editing it
shares with the lattice tool in `doc/editor/point_set_editing.md`, the kit
layout along a curve in `doc/editor/level_tools.md` "Path layouts"; the
rulings and requirements below stay the reference for the remaining steps.

## Rulings

The user's rulings (2026-11-14):

- V1: a curve is its own node type.
- V2: a polyline is a special case of a curve.
- V3: curves and polylines are editable in the 3D viewport.
- V4: a curve's control points are positions in 3D space, not in time.
  Camera and other animation keys have and keep their time keys; a curve
  node is not an animation and the animation system is unchanged by this
  work.
- V5: the 2D Bezier editor is a separate goal from the curve node and its
  viewport editing.
- V6: animating a curve's control points is future work; when control
  points move, everything laid out along the curve is laid out again.

## Requirements

- C1 `[must]` Curve node. A scene node of its own type holds one curve: an
  ordered list of control points in the node's own frame, open or closed.
  It saves and loads with the scene, is listed, named, parented, moved and
  deleted as other nodes are, and every edit of it is one undoable
  operation.
- C2 `[must]` Control points. A control point has a position and an
  incoming and an outgoing handle. A point is a corner (no handles: the
  neighboring spans leave it straight), smooth (the handles stay on one
  line) or free (the handles move separately). A curve whose points are
  all corners is a polyline (V2).
- C3 `[must]` Evaluation by distance. A curve gives position, tangent and
  an up-keeping frame at a distance along it, and its length, so users of a
  curve step along it by distance, not by parameter.
- C4 `[must]` Viewport editing (V3). With a curve node selected, its
  curve, control points and handles are drawn in the viewports; a point or
  a handle is picked in the viewport and moved with the Transform tool's
  gizmo (axis and plane constraints, snapping, surface snap); points are
  added on the curve and at its ends, removed, and switched between corner,
  smooth and free. A finished drag is one undo entry.
- C5 `[must]` Parity. Every curve edit and read of C1 - C4 is an MCP tool
  as well (`doc/agents/mcp_api_guidelines.md`): create, read, set points,
  add, remove, set a point's kind, evaluate at a distance.
- C6 `[must]` Curve kit layout. A kit layout along a path is one node
  (`doc/plans/level_tools.md` U18, R5.12.8): a curve node that carries the
  kit layout values and has the pieces as its children. The pieces sit
  along the curve by count or by gap (R5.12.7), each turned to the curve's
  tangent; over a curve of corner points this is the polyline arrangement.
  A locked kit layout is laid out again when its curve changes (a point, a
  handle, the closed setting), as one undo entry with that change, and
  live while a point is dragged.
- C7 `[must]` Change-driven. A curve's users are notified by the change
  itself; nothing polls a curve per frame.
- C8 `[should]` 2D Bezier editor (V5). An ImGui widget that edits a planar
  cubic Bezier curve with the same point kinds and handle behavior as C2
  and C4 (handles aligned by default, one side freed and axis locks by
  modifier keys), independent of animation data and of the texture graph's
  node editor, usable by both.
- C9 `[should]` The sweep shape takes its spine from a curve node.

## What the work builds on

- `erhe::math` curve path (`curve_path.hpp`, `doc/erhe/math.md` "Curve
  path"): control points with handles and kinds, cubic spans, the arc-length
  table, and up-keeping and parallel-transported frames at a distance. The
  `erhe::geometry` sweep (`shapes/sweep.cpp`) evaluates its one Bezier of
  any degree, stepped by parameter, through its Bezier and transport
  helpers.
- `erhe::math` `hermite.hpp`: the scalar cubic Hermite segment the texture
  curve widget and the animation edit helpers share.
- The point set editing of the lattice and curve tools
  (`doc/editor/point_set_editing.md`): points of one target picked by
  screen-space distance, drawn as handles, and moved by the Transform tool's
  gizmo anchored at the selection, one operation per gesture. Mesh component
  editing (`doc/editor/mesh_component_selection.md`) has its own anchor
  adapter (`Mesh_component_transform`).
- The Animation window's curve canvas (`animation/animation_window.*`):
  tangent handles aligned by default, Alt freeing one side, Shift locks;
  bound to animation channel and key ids.
- The texture curve widget (`texture_graph/texture_graph_widgets.*`): a
  y(x) curve of points without handles, a plain vector in and a changed
  flag out.
- The kit layout node (`level/kit_layout_values.hpp`, `level_repeat.hpp`):
  a path layout is a curve node carrying the layout values, its pieces its
  children (`doc/editor/level_tools.md` "Path layouts").

## Design outline

- D1 `erhe::math` gains the curve path: control points with handles and a
  kind, cubic Bezier spans (a corner gives a straight span), an arc-length
  table, and position, tangent and frame at a distance. The sweep shape
  evaluates through it.
- D2 The curve node is the `erhe::scene::Curve` prim class
  (`doc/editor/curves.md`). Its USD form is a `BasisCurves` prim (step 7).
- D3 The point set interface (`IPoint_set`, `doc/editor/point_set_editing.md`)
  is built and used by the lattice and curve tools. What remains: a point
  set's drag honors the surface settings (step 8); mesh component editing
  stays on its own adapter (`Mesh_component_transform`) - its selection is
  per mesh primitive and its edits are topology-aware, which the point set
  interface does not carry.
- D4 The kit layout along a curve is built as one node (C6, R5.12.8): the
  `Curve` prim carries the `Kit_layout` values and has the pieces as its
  children, laid out along its own path in its own frame, again in the entry
  of each curve edit and live during a point drag (`doc/editor/level_tools.md`
  "Path layouts", `doc/editor/curves.md` "Users of a curve").

## Steps

5. The 2D Bezier editor widget (C8).
6. The sweep shape's spine from a curve node (C9).
7. USD persistence of a curve node: a `Curve` prim saves as a `BasisCurves`
   prim (cubic, bezier basis, `wrap` periodic for a closed curve, the
   control points and handle ends as `points`, the point kinds as an erhe
   custom attribute) and a `BasisCurves` prim of that form loads as a
   `Curve`; until then a USD save writes a curve as an `Xform` with a
   warning naming it.
8. Surface snap of point drags (C4 "surface snap"): with Snap To Surface on,
   a translate drag of curve points (and lattice control points) rests the
   moved points on the surface under them, as a node drag rests a moved
   mesh; `Transform_tool::snap_to_surface()` acts on node entries only
   today.

## Later

- Animated control points (V6): keys on control point positions and
  handles, with the users of the curve laid out again as the points move.
