# Level kit connections: requirements

Status: in progress

Requirements for connector types, a graph of connections between kit pieces,
property flow over that graph and auto-connection. This document holds
requirements only; the design is `doc/plans/level_kit_connections_design.md`
and the build order is `doc/plans/level_kit_connections_implementation.md`. It extends `doc/plans/level_tools.md`
(R5.3 connectors, R5.13 snap to connectors) and the behavior in
`doc/editor/level_tools.md` "Connectors", "Connector switches" and
"Attachments".

Each requirement is marked `[must]` (part of the first accepted scope) or
`[should]` (wanted, may land after it). "Later" lists what is out of scope
for now; "Open questions" lists what needs a user ruling before the design.

## Rulings

The user's rulings (2026-11-19):

- L1: the auto-connection setting is named "Auto-connect" and is on by
  default (C5.5).
- L2: a floor replies a wall length per edge, so each of its wall connectors
  can give a different length; walls overlap at the corners for now, and
  proper wall joins are later work (C3.3, "Later").
- L3: a floor also supplies `room.height` and `wall.material`, and a wall
  supplies `room.height` to a neighbouring wall (C3.3).
- L4: a change of any connector propagates to the neighbours: property
  values flow over the graph of connected pieces and their connectors, up to
  the whole connected graph (C3).
- L5: a supplied value outside the receiving parameter's range is used as
  it is, not clamped (C3.5).
- L6: the tie rule of equal priorities is the orchestrator's choice (C4.3).
- L7: co-location does not compare the up axes, for now (C5.2).
- L8: auto-connection repeats until nothing changes, with a maximum step
  count (C5.3).
- L9: the floor reports edge lengths that make the end connectors of
  neighbouring walls meet (C2.4, C3.3).
- L10: connector snap during a drag previews the flow, and the preview is
  reverted when the drag unsnaps (C3.10).
- L11: the corner notch of the L9 edge lengths is accepted for now; a proper
  corner fix is later work ("Later").
- L12: the readings of C2.5, C3.2 and C3.7 are accepted.
- L13: the maximum step count is a setting, 64 by default (C3.8).
- L14: incompatible connectors that the author did not name produce no
  refusal messages (C1.9).
- L15: the first scope stays small, to see what works: walls stand only on
  floors; stacked walls and buildings of several storeys are later work
  (C1.5, "Later").
- L16 (revisiting the design's two kinds of connection): there is one kind
  of connection (C3.2).
- L17: a flow mask, one switch per connector type, all on by default, set by
  the author in the UI or over MCP, chooses over which connector types
  changes flow (C3.9).
- L18: moving a wall that stands on a floor makes the floor fit: the floor's
  edge follows the wall, and the other walls re-join the changed floor. A
  floor may become a general polygon, so its size is its edges rather than a
  width and a depth; a width and a depth (or edge lengths) remain as the
  initial values of a new floor (C3.15, C8).
- L19 (Q14, the user: "you choose, single solution feels better than
  multiple"; orchestrator choice): a room has one way to be built. Its
  outline is the polygon floor, a closed curve of corner points
  (`doc/editor/curves.md`) that is the floor piece and is edited with the
  curve's point editing. Its walls are free kit pieces connected to the
  floor's edges. The fit of a moved wall reuses the segment handle rule
  (U38). The segments layout stays a layout for runs along a path and gains
  no floor (C8).
- L20 (Q15): the fit rule as proposed: the moved wall's base line becomes its
  edge's line, and the corners slide to the crossings with the neighbouring
  edges' lines. A slide along the wall's run, a vertical move and a tilt are
  dropped, so the wall re-joins its edge's midpoint at the floor's height,
  upright. A Detach operation frees a piece from its connections, so a wall
  can be detached and dragged away (C9).
- L21 (Q16): the flow mask is an editor setting, like the connector snap
  settings, and it replaces the Connector Property Flow setting (C3.9).
- L23 (2026-11-19, the user: "our goal is to allow all values in as many
  places as possible. If there truly is no way to accept some value, then we
  clamp."): a shape's declared parameter ranges are where the UI's drags run,
  not limits. A float parameter takes any finite value of its range's sign
  (`Kit_parameter_values::validate()`); integer ranges stay limits; a flowed
  value a shape still refuses is clamped to the declared range.
- L24 (2026-11-19, the user: "Existing fit - at least the code which does it
  with scaling - should be removed, and replaced with setting length
  property; we might need a more generic child.length property (instead of
  wall.length) so it works with all layouts and layout children"; asked:
  segments only, a piece without the mapping placed unscaled): the standard
  property formerly `wall.length` is `child.length`; a segments kit layout sets each
  child's `child.length` to its segment's length and scales nothing; a child
  whose shape does not map it stands at its own size, centred on its
  segment.
- L25 (2026-11-19, the user: "floor edges should have connectors for floor
  pieces to connect to each other"; asked what connected floors do: "When
  floor tiles are connected, moving connected corner control point moves
  corner in both floor tiles. ... when it does [an edge handle], moving the
  edge will move the two corners - the corners which are connected will be
  moved on all connected paths"; values: not yet): a connector type `floor`
  on each floor edge; connected floors share their corners; no values flow
  between floors.
- L22 (Q17): two walls joined end to end: when one is moved, the other's
  length adapts, so its far connector stays where it is and its near
  connector stays on the moved wall's (C3.16).

Readings the orchestrator made to fit the rulings together are marked
"(reading)" where they appear; L12 accepted them.

## Terms

- Connector, join, attachment, connector switch: as in
  `doc/editor/level_tools.md` "Connectors".
- Connector type: a name stating what a connector is for (C1).
- Connection: a join between connector A of piece P and connector B of
  piece Q that the scene remembers (C3.1), made by any of the join paths of
  C1.3 or by auto-connection (C5).
- Connection graph: the kit pieces of a scene as vertices and their
  connections as edges. A connected group is one component of it.
- Standard property: a property name with one meaning across shapes
  (`wall.thickness`), which a shape maps onto one of its own parameters or
  material slots (C2).
- Supply: the values of standard properties a piece offers over one of its
  connectors to the piece connected there, each with a priority (C3.3, C4).
- Property flow: a piece takes values its neighbours supply, and a piece
  whose values change makes its neighbours take values again, through the
  connection graph (C3).

## C1 Connector types

- C1.1 `[must]` Each connector of a shape states a list of accepted connector
  types, at least one. A connector a shape declares without a list accepts
  `generic`.
- C1.2 `[must]` Two connectors are compatible when their lists share at least
  one type. Only compatible connectors connect.
- C1.3 `[must]` Every join path honours C1.2 with the same compatibility
  test: an explicit join (`connect_piece()`, the Level Kit window's Connect
  rows, `level_connect_pieces`), a placement with `connect`
  (`level_place_pieces`), the Brush tool's kit-mode connector join and its
  preview, connector snap during a Transform tool drag, the connection a
  snapped drag records on release, and auto-connection (C5).
- C1.9 `[must]` Only a join the author asked for by naming both connectors
  (the explicit join and the placement with `connect`) is refused with a
  message, naming both connectors and their types. Every search for a
  partner - connector snap, the Brush preview, auto-connection - treats an
  incompatible connector as not there: it is skipped with no message, no log
  line and no report entry (L14).
- C1.4 `[must]` The first set of types:
  - `generic`: general use; every existing connector keeps it, except the
    wall connectors named below.
  - `wall`: where walls meet each other (a wall's `start` and `end`).
  - `wall-floor`: where a wall stands on a floor (a wall's `base`; the floor
    connectors of C2.4).
- C1.5 `[must]` A connector may accept several types. A wall's `start` and
  `end` accept `wall` and `generic`, so a wall still meets a tower's or a
  pillar's side (orchestrator choice, for the user's review). A wall's `base`
  accepts `wall-floor` only, so a wall stands on a floor and nowhere else
  (L15); its `top`, `front` and `back` stay `generic`.
- C1.6 `[must]` The types are a fixed list known to the editor; a shape
  naming a type not on the list fails a unit test.
- C1.7 `[must]` The MCP catalog (`level_get_kits`) and `level_get_connectors`
  report each connector's types; the Properties window's "Kit Piece" group
  shows them beside each connector switch.
- C1.8 `[should]` The connector indicators (Debug Visualization >
  Connectors) show a connector's types, by color or label, so an author sees
  which connectors can meet.

## C2 Standard properties

- C2.1 `[must]` The first set of standard properties:

  | Property | Unit | Meaning |
  |---|---|---|
  | `room.height` | meters | height of walls and similar room-bounding shapes |
  | `floor.thickness` | meters | thickness of a floor |
  | `wall.thickness` | meters | thickness of a wall |
  | `child.length` | meters | length of a wall along its run |
  | `floor.material` | material | material of a floor's walking surface slot |
  | `wall.material` | material | material of a wall's main slot |

- C2.2 `[must]` A shape maps each standard property it takes part in onto one
  of its parameters or material slots (`architecture.wall`: `room.height` to
  `height`, `wall.thickness` to `thickness`, `child.length` to `length`,
  `wall.material` to slot `wall`; the polygon floor: `floor.thickness` to
  its thickness, `floor.material` to slot `floor`). A shape that maps none
  takes no part in property flow.
- C2.3 `[must]` The wall shapes that tile along a run (`architecture.wall`,
  the door wall, the window wall, the Japanese wall) and the polygon floor
  (C8) declare their mappings. `basic.floor_slab` stays a tile with generic
  connectors only (L19). Which further shapes join is listed in the design.
- C2.4 `[must]` The polygon floor places a `wall-floor` connector at the
  midpoint of each top edge, inset toward the floor's center by half of its
  `wall.thickness`, facing up, with its up axis toward the floor's center, so
  that a wall joined with no twist stands on the edge with its front face
  toward the room and its outer face flush with the floor's edge. The
  connectors move with the floor's outline and parameters, as every
  connector does.
- C2.5 `[must]` The polygon floor holds its own `wall.thickness`, `room.height`
  and `wall.material` as parameters and a slot value: the thickness defaults
  to the wall's default (0.25 m), the height to the level's wall height, the
  material to the wall slot's default. (Reading: the request had the floor's
  values "if set"; the edge lengths of L9 need a thickness at every edge, so
  the floor always has one.)

## C3 Connection graph and property flow

- C3.1 `[must]` A connection is remembered: both pieces and both connectors,
  saved with the scene, surviving save and load by glTF uid as an attachment
  does. A piece has at most one connection per connector and may have
  connections on several connectors (a wall standing on a floor and joined
  to the walls at both its ends).
- C3.2 `[must]` There is one kind of connection (L16). A connection keeps its
  two connectors joined: it is the generalization of today's attachment, and
  the following of `doc/editor/level_tools.md` "Attachments" applies along
  every connection whose types the flow mask lets through (C3.9). How a
  piece answers a change of its neighbour is stated by its shape per
  connector type: it follows (a rigid move that re-joins, a wall on a moved
  floor) or it fits (its values change so that its connector meets the
  neighbour's, a floor under a moved wall, C3.15). A scene's existing
  attachments load as connections. Cycles are allowed (a room of four walls
  on a floor is one).
- C3.3 `[must]` What a shape supplies is stated per connector type (or per
  connector where a shape needs it), each with a priority (C4):
  - A floor over `wall-floor`: `child.length` of that edge, `wall.thickness`,
    `room.height` and `wall.material`. The length of an edge is such that
    the end connectors of the walls on two neighbouring edges meet at the
    crossing of their center lines: the edge's length minus the floor's
    `wall.thickness` (L9). Each edge supplies its own length (L2).
  - A wall over `wall`: `wall.thickness`, `wall.material` and `room.height`.
- C3.4 `[must]` A piece's values are recomputed from its connections when it
  gets a connection and when a value its neighbours supply changes. Every
  value a neighbour supplies, through any connection, is a candidate; per
  property the piece takes the candidate of C4.2.
- C3.5 `[must]` The piece takes the chosen value onto its mapped parameter or
  slot as it is, also outside the parameter's range (L5). A parameter that
  took a value from the flow holds it as an explicit value (its seed no
  longer derives it, R3.9 of `doc/plans/level_tools.md`).
- C3.6 `[must]` Propagation (L4): a change of a piece's values - by the flow,
  by the author (Properties window, `level_set_piece_parameters`,
  `level_set_piece_material`, Replace) or by a new or removed connection -
  makes each neighbour recompute (C3.4); a neighbour whose values change
  makes its own neighbours recompute, on through the connected group. A
  piece the author edited keeps the edited values in that change; the flow
  writes into the other pieces.
- C3.7 `[must]` A piece's new values move its connectors, and the
  connections keep their connectors joined (C3.2): the poses are resolved
  outward from the piece the change started at, each piece re-joined to the
  neighbour it was reached from. A connection that the resolved poses leave
  apart (a loop that no longer closes) is reported by the change and listed
  as apart by the connection query; it is kept until the author removes it
  or the pieces meet again. (Reading.)
- C3.8 `[must]` Propagation stops when no value and no pose changes, or at a
  maximum step count shared with auto-connection (C5.3), a setting ("Connection
  Step Limit", 64 by default, L13); reaching the
  maximum is reported, naming the pieces still changing.
- C3.9 `[must]` The flow mask (L17): one switch per connector type, all on by
  default. Over a connection, values and poses flow only when a type both
  connectors accept is switched on. A connection of switched-off types is
  still made and kept; nothing flows over it, and a move that takes its
  connectors apart removes it (C3.14). The mask is one of the settings the
  UI remembers and an MCP call may set or state for one call, read by every
  join path of C1.3 and by propagation. It is an editor setting, like the
  connector snap settings, and there is no separate property flow setting
  (L21).
- C3.10 `[must]` Connector snap during a Transform tool drag or a Brush
  preview shows the flow live: the dragged piece takes the values the
  snapped connection would give it, and the propagation they cause, in the
  preview; when the drag unsnaps, every previewed value and pose is reverted;
  the release commits what the last snapped frame showed (L10).
- C3.11 `[must]` A child of a locked kit layout node never takes values from
  the flow (its layout owns its values); it can supply values. Propagation
  stops at it.
- C3.12 `[must]` The change that starts a flow, the flow, the propagation
  and the auto-connections that follow (C5) are one undo entry; undo
  restores every value, pose and connection.
- C3.13 `[must]` Every change that flows reports which properties flowed
  into which pieces, from which piece, at which priority, and which supplied
  values were not taken and why (a higher candidate, no mapping, a locked
  layout child); the MCP reply carries it, the Level Kit window's status
  line summarizes it.
- C3.14 `[must]` A move of a piece that takes one of its connectors off its
  joined connector, without the other piece following, removes that
  connection (as a move removes an attachment today); the removal is part of
  the move's undo entry.
- C3.15 `[must]` Fit (L18). When a wall standing on a floor is moved, the
  floor's edge under the wall's `wall-floor` connector follows the wall
  (L20): the edge's line becomes the line of the wall's base, the corners at
  both ends of the edge slide to the crossings with the neighbouring edges'
  lines, and the floor's other edges keep their lines. A slide along the
  wall's run, a vertical move and a tilt change no line; the wall re-joins
  its edge's midpoint at the floor's height, upright. The walls on the other
  edges re-join the changed floor and take their new lengths (C3.3).
- C3.16 `[must]` Fit of two walls (L22). When a wall joined end to end to
  another wall is moved, the other wall changes its length and pose so its
  far end connector stays where it was and its near end connector stays on
  the moved wall's connector. Where both walls also stand on a floor, the
  floor's fit (C3.15) and this one agree; where they differ the floor wins
  (C4).

## C4 Priorities

- C4.1 `[must]` Each shape states a priority, a small integer, for each
  standard property it supplies. Higher wins.
- C4.2 `[must]` Among the candidates of one property (C3.4) the piece takes
  the one of the highest priority. Example: wall W1 joined to wall W2 and
  standing on floor F takes `child.length` from F (priority 2) over W2
  (priority 1).
- C4.3 `[must]` Equal priorities (L6, orchestrator choice): the candidate
  whose value changed in the current change wins, so an edit spreads along a
  run of equal pieces; then the candidate of the connection made in the
  current change; then the candidate through the connector earliest in the
  receiving shape's declared order. The report of C3.13 names a tie.
- C4.4 `[must]` The first priorities: a floor supplies at 2, a wall at 1.

## C5 Auto-connection

- C5.1 `[must]` When a piece's values or pose change in a change of C3 (a
  connection made, a flow, a propagation, an author's edit of a connected
  piece), every enabled connector of that piece is checked against the
  enabled connectors of the other kit pieces of the scene; each compatible
  pair (C1.2) that is co-located and not connected yet is connected.
- C5.2 `[must]` Co-located: positions within 0.1 mm and outward axes
  opposing within 1e-4 rad, the tolerance of an attachment's join; the up
  axes are not compared (L7).
- C5.3 `[must]` A new auto-connection makes both pieces recompute (C3.4),
  and their changes run C5.1 again: the rounds repeat until no connection,
  value or pose changes, up to the maximum step count of C3.8 (L8).
- C5.4 `[must]` Auto-connection never moves a piece by itself: it records
  connections that the geometry already shows; the flow over them may then
  move pieces (C3.7).
- C5.5 `[must]` A setting, "Auto-connect", on by default (L1), read by every
  join path of C1.3 and by propagation; with it off, only the explicit join
  is made.

## C6 Exposure and persistence

- C6.1 `[must]` Parity with `doc/plans/level_tools.md` R6.1: the types, the
  connections, both settings and the flow report are reachable in the UI and
  over MCP through the same code path.
- C6.2 `[must]` MCP: the join tools take `flow_mask` and `auto_connect`
  (defaulting to the settings) and reply with C3.13; a query lists a piece's
  connections (with joined or apart) and a scene's connected groups; one
  tool removes a connection, one detaches pieces (C9); the polygon floor is
  created and its outline read and written (C8).
- C6.3 `[must]` The Properties window's "Kit Piece" group lists a piece's
  connections, each with a Remove button.
- C6.4 `[must]` A scene saved before connector types and connections loads
  unchanged, and its attachments load as connections (C3.2). Compatibility
  is tested when a join is made, not on load: a loaded connection between
  connectors that are no longer compatible (a wall's `base` on a `generic`
  top) is kept and followed.
- C6.5 `[must]` Replace (R5.8) keeps a connection whose connector the new
  shape has with a compatible type and drops the others, reported as Replace
  reports dropped values today; the new shape's values then flow (C3.6).

## C7 Verification

- C7.1 `[must]` Unit tests: every shape's connector types are on the list
  (C1.6); every mapping names an existing parameter or slot of the stated
  type; the floor connectors of C2.4 sit inset by half the wall thickness at
  every edge and at the ends of the parameter ranges; four walls taking the
  edge lengths of C3.3 have their end connectors meeting at the corners.
- C7.2 `[must]` A verify group (`scripts/level_tools_verify.py`): an
  incompatible join is refused over every join path of C1.3; a wall stood on
  a floor takes the edge's length, the thickness, the height and the
  material; a wall joined to a wall takes its thickness, material and height;
  the W1 / W2 / F case of C4.2; four walls on a floor auto-connect at the
  corners into one connected group; a floor corner edit propagates the new
  lengths and moves the walls; a wall moved along its normal or turned makes
  the floor fit and the other walls follow (C3.15); a wall moved along its run
  returns to its edge's midpoint; a fit that would make an invalid polygon is
  refused (C8.3); two walls end to end fit (C3.16); a thickness edit spreads
  along a wall run (C4.3); a drag snap previews and reverts (C3.10); the step
  maximum is reported; a flow mask with a type off and Auto-connect off each
  do nothing beyond the join; Detach frees a wall that then drags alone
  (C9); undo and redo of each; save and load keep the connections and the
  floor outline; an older scene with attachments loads them as connections.

## C8 Polygon floor

- C8.1 `[must]` The polygon floor (L19) is a closed curve of corner points
  that is a kit piece: it carries the floor's values (C2.5), its material
  slots and its connections, and its walking slab is generated from the
  outline. Its corners are edited with the curve's point editing; each edit
  is a change of the floor (C3.6). Its edges have their own lengths (L18); a
  moved wall can turn its edge, so the polygon need not stay a rectangle. The `wall-floor` connectors sit on each edge's midpoint,
  inset by half the floor's `wall.thickness` (C2.4), and each edge supplies
  its own `child.length` (C3.3).
- C8.2 `[must]` A new polygon floor starts as a rectangle of the initial
  width and depth the Level Kit window holds (as the initial values of a kit
  layout node, R5.12.9 of `doc/plans/level_tools.md`) or that an MCP call
  states. It is created from the Level Kit window, the Create menu's "Level
  Kit" submenu and over MCP.
- C8.3 `[must]` A polygon the fit of C3.15 would make invalid (edges
  crossing, an edge of zero or negative length, a corner turned inside out)
  refuses the move with the reason; the move is not recorded.

- C8.4 `[must]` The floor's edge count changes only by editing the curve
  (adding or removing a corner point); a wall on a removed edge loses that
  connection, reported in the edit's entry.

## C9 Detach

- C9.1 `[must]` Detach (L20) removes every connection of the selected pieces
  as one undo entry, with no flow; a later drag moves a detached piece alone
  and its former neighbours stay as they are.
- C9.2 `[must]` Detach is reachable from the Properties window's "Kit Piece"
  group, the Scene hierarchy's context menu, the Level Kit window and over
  MCP; a single connection is removed with its row's Remove (C6.3).
- C9.3 `[must]` Detach replaces today's detach of an attachment
  (`doc/editor/level_tools.md` "Attachments").

## Later

- Proper wall joins at corners: mitered or butted geometry in place of the
  inside overlap and the outside notch of L2, L9 and L11.
- Stacked walls and buildings of several storeys (a wall on a wall's top, a
  floor on walls).
- Author-defined connector types and standard properties (data, not C++).
- A per-piece override of a connector's types.
- Property flow for kit layout children (a segments layout around a floor).
- Supply into a floor of values (a wall's thickness changing the floor's
  inset); a wall's pose fitting the floor is C3.15.
- Whether a closed segments layout of walls is still wanted once rooms are
  built from a polygon floor (L19).

## Open questions

None.
