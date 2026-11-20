# Level kit connections: implementation steps

Status: in progress

The build order for `doc/plans/level_kit_connections_design.md` (K-numbers).
It meets `doc/plans/level_kit_connections.md` (C-numbers). One step is one
commit. A step is done when its verification passes. A done step is removed
from this list, and what it built is described in
`doc/editor/level_tools.md` (sections "Connectors", "Connector switches" and
"Attachments", and the new "Connections", "Property flow" and "Polygon
floor"), `doc/editor/level_kit_shapes.md` and `doc/editor/curves.md`.

The final pass of the series runs these gates in the headless tree (a step
runs its own checks only, `AGENTS.md` "When to run the slow test suites"):
- `editor_level_tests`;
- `scripts/level_tools_verify.py` (its new groups, and the full run at
  steps 5 and 9);
- `scripts/curve_verify.py`;
- `ctest -C Debug -L editor -j1`.

MCP tool changes go into `config/editor/mcp_tools.json` in the step that
makes them.

## Built

Every step is built; what each built is described in
`doc/editor/level_tools.md`: step 1, connector types ("Connectors"); step 2,
the polygon floor as a floor kit layout (design K3.0; "Floor layouts");
step 3, standard properties, mappings and supply ("Standard properties and
supply"); step 4, connection records ("Connections" and "Replace"; the
connections are entries of the existing `Kit_piece.attachment` value,
design K6.0); step 5, the flow engine ("Property flow"); step 6,
Auto-connect ("Auto-connect and the connection settings"); step 7, previews
("Flow preview"); step 8, indicator colors by connector type ("Connector
indicators"); step 9, the final pass ("Persistence", "Verification": the
groups `--only-connection-persistence` and `--only-connection-fixes`, the
second holding the checks of the defects the review of the whole series
found).

What remains is the user's: the windowed test below, the review of the
as-built choices of the design (K0, K0.6 - K0.9, K3.0, K6.0) and the
rulings under "For the user to rule on".

## User test (windowed, pending)

For the test: build `build_xcode_metal` Debug `editor`, reset
`config/editor/desktop_windows.json` and `editor_settings.json`, launch it for
the user (`doc/agents/editor_runs.md` "Handing the editor to the user") and
ask for these checks. Before testing, Connector Snap in the Level Kit window
must be above 0 (0.5 m; the reset makes it 0) and Debug Visualization >
Connectors set to All.

1. A floor corner dragged with the Curve tool: the walls on it move and
   change length live; the release is one undo entry.
2. A floor's Thickness or Wall Thickness dragged in Properties: the walls
   rise or shift live.
3. A wall's Properties "Kit Piece" group: one "Connection" row per
   connection with Remove, and Detach.
4. The Scene hierarchy context menu's Detach Connections on a wall: it then
   drags alone.
5. The Brush tool in kit mode with `architecture.wall` near a floor edge: the
   preview joins the edge; placed, the wall takes the edge's length and
   connects to its neighbours at the corners.
6. The floor's connectors: `edge_<i>` inset (wall-floor color), `floor_<i>`
   on the outer edge.
7. A wall on a floor dragged outward: the floor fits and the side walls
   stretch live; the release equals what the drag showed. A wall slid along
   its edge, lifted or tilted returns to its edge.
8. Two free walls end to end, one moved sideways: the other keeps its far
   end, live.
9. A second floor dragged edge to edge onto the first connects; a shared
   corner dragged moves both floors, and a second edit still does.
10. A corner added to a floor edge with walls: the walls keep their edges.
11. The Level Kit window's Selection section: one row per connection of
    the selected piece with Remove, and Detach Selected (step 4).
12. Replace of a connected wall with a pillar: the window summary names
    the dropped connections (step 4).
13. A free wall dragged near a floor edge: while snapped it shows the
    edge's length, thickness, height and material, and its own again when
    the drag leaves the edge.
14. A wall dragged so far that the floor outline would cross itself: the
    preview stays at the last valid fit, and the release leaves everything
    where the drag began with no undo entry.
15. A floor edge handle (the Curve tool on a floor): the edge moves along
    its normal with its walls, a connected floor's shared corners follow.
16. A floor selected: its Properties "Kit Layout" group lists its
    connections with Remove and Detach.
17. The Level Kit window after a change that flows: the flow summary line
    under Detach Selected.

Not yet asked of the user: whether "lower/raise wall control points" (the
user's floor thickness request) meant walls built as a segments layout on a
curve, whose points do not follow a floor.

## For the user to rule on

Points the code decides as built, found by the review of the whole series:

1. Auto-connect leaves out pairs compatible only through `generic` (C5.1
   says every compatible pair), and it runs once followed by one flow (C5.3
   and L8 say repeat until nothing changes). The step limit bounds the value
   flow only (K0.2).
2. A non-right-angle corner edit of a floor removes the wall to wall joins
   at that corner: the wall lengths of L9 meet exactly only at right angles,
   and the ends part by more than the join tolerance. C3.7 would keep such
   a connection and report it apart; Auto-connect restores it when the
   corner is a right angle again. It belongs with the corner question of
   L11.
3. With the `wall` type masked off, values still flow between two walls
   through the type `generic` that both carry.
4. A duplicated or pasted room has no connections (the copies' records are
   not remapped to the copies) and connects only when a piece of it is
   edited.
5. An author's edit of a value a piece takes over a connection (a wall's
   thickness on a floor) is overwritten by the next change of the supplier.
6. Detach does not hold against a later value edit of the piece while
   Auto-connect is on: the edit connects it again (C9.1 covers a later
   drag).
7. A plain curve edit into an invalid floor outline is recorded (K3.0);
   only a fit refuses (C8.3).
8. A moving preview step runs the whole follow operation (design K0.7).
9. Two changed neighbouring edges of a floor that are near parallel (two
   walls on the halves of a split edge moved by different amounts): see
   `doc/editor/level_tools.md` "Property flow" for what the fit does.
