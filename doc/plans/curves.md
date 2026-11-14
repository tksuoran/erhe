# Curves: curve nodes, viewport editing, curve kit layouts, 2D Bezier editor

Status: proposed

Requirements, design outline and build steps for 3D curves as scene nodes:
a curve node type, editing of its control points in the 3D viewport, kit
layout nodes that place pieces along a curve
(`doc/plans/level_tools.md` R5.12, `doc/editor/level_tools.md` "Kit layout
nodes"), and a separate 2D Bezier editor widget. The shared pieces this
builds on are named in "What the work builds on".

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
- C6 `[must]` Curve kit layout. A kit layout node has a path arrangement
  that follows a curve node it names: the pieces sit along the curve by
  count or by gap (R5.12.7), each turned to the curve's tangent. The
  polyline arrangement of the kit layout node is this arrangement over a
  curve of corner points. A locked kit layout node is laid out again when
  its curve changes (a point, a handle, the closed setting, the curve
  node's transform), as one undo entry with that change.
- C7 `[must]` Change-driven. A curve's users are notified by the change
  itself; nothing polls a curve per frame.
- C8 `[should]` 2D Bezier editor (V5). An ImGui widget that edits a planar
  cubic Bezier curve with the same point kinds and handle behavior as C2
  and C4 (handles aligned by default, one side freed and axis locks by
  modifier keys), independent of animation data and of the texture graph's
  node editor, usable by both.
- C9 `[should]` The sweep shape takes its spine from a curve node.

## What the work builds on

- `erhe::geometry` sweep (`shapes/sweep.cpp`): 3D Bezier point and tangent
  evaluation and parallel-transported frames, private to that file, one
  Bezier of any degree, stepped by parameter.
- `erhe::math` `hermite.hpp`: the scalar cubic Hermite segment the texture
  curve widget and the animation edit helpers share.
- The lattice tool (`doc/editor/lattice_deform_geometry_node.md` "Viewport
  editing"; `tools/lattice_tool.*`, `transform/lattice_point_transform.*`):
  control points held as data of one node, picked by screen-space distance,
  drawn as handles by the line renderer, and moved by the Transform tool's
  gizmo anchored to the selected point (`Transform_tool_shared`
  `component_mode`), one parameter operation per gesture. Mesh component
  editing (`doc/editor/mesh_component_selection.md`) uses the same anchor
  adapter.
- The Animation window's curve canvas (`animation/animation_window.*`):
  tangent handles aligned by default, Alt freeing one side, Shift locks;
  bound to animation channel and key ids.
- The texture curve widget (`texture_graph/texture_graph_widgets.*`): a
  y(x) curve of points without handles, a plain vector in and a changed
  flag out.
- The kit layout node (`level/kit_layout_values.hpp`, `level_repeat.hpp`):
  polyline points held in the layout node's pattern, expanded into poses by
  arc length over straight segments.

## Design outline

- D1 `erhe::math` gains the curve path: control points with handles and a
  kind, cubic Bezier spans (a corner gives a straight span), an arc-length
  table, and position, tangent and frame at a distance. The sweep shape
  evaluates through it.
- D2 The curve node. Whether it is a prim class of `erhe::scene` or an
  Xform node carrying an attached value group is settled by the first step
  that builds it, on what the node systems, the persistence
  (`doc/gltf_extensions/`), USD and the item tree need; V1 requires that
  the user sees and makes it as a node type of its own either way.
- D3 A point-set editing interface generalized from the lattice tool: a
  provider gives the points (and handles) of the selected node in world
  space, the pick radius and the selection, and takes back a moved point;
  the lattice tool, the curve tool and later users implement it, and the
  gizmo anchor adapter is shared.
- D4 A kit layout node names its curve node by a scene-hosted reference
  (`doc/editor/coding_rules.md`); the curve node tells its users of a
  change, and the kit layout regeneration of that change joins the curve
  edit's undo entry (the follow-up mechanism of `Property_set_operation`).

## Steps

1. The curve path in `erhe::math` (D1) with unit tests; the sweep shape
   uses it.
2. The curve node (C1, C2, C3, C5 reads and whole-curve writes, D2):
   data, persistence, drawing when selected, Properties rows, MCP tools.
3. Viewport editing (C4, the point edits of C5, D3): the point-set
   interface, the lattice tool moved onto it, the curve tool.
4. The curve kit layout (C6, C7, D4): the path arrangement over a curve
   node; the kit layout node's polyline arrangement and its point list are
   removed (`doc/plans/level_tools_implementation.md` step 22 is this
   step).
5. The 2D Bezier editor widget (C8).
6. The sweep shape's spine from a curve node (C9).

## Later

- Animated control points (V6): keys on control point positions and
  handles, with the users of the curve laid out again as the points move.
