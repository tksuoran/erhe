# Level kit connections: design

Status: in progress

The design that meets `doc/plans/level_kit_connections.md` (C-numbers,
rulings L1 - L22). The build order is
`doc/plans/level_kit_connections_implementation.md`. It builds on the
connectors, connector switches and attachments of
`doc/editor/level_tools.md`, on curves (`doc/editor/curves.md`) and on
`doc/plans/level_tools_design.md` (D-numbers there; this document's sections
are K-numbers).

## K0 Choices for the user's review

- K0.1 The polygon floor is a Curve prim node that carries the kit piece
  record. Its slab is one generated child mesh, the same arrangement as the
  stairs layout's staircase (K3.5). A Curve prim cannot also be a mesh, and
  this keeps the curve's point editing, drawing and persistence as they are.
- K0.2 A step of the step limit (C3.8, C5.3) is one round of the flow (K7.3),
  not one piece's recompute, so the limit does not depend on the size of the
  connected group.
- K0.3 A Transform drag whose fit would make an invalid floor (C8.3) shows
  the refusal in the preview while the drag goes on. A release at such a pose
  ends as a cancelled drag: the start is restored and nothing is recorded.
- K0.4 The polygon floor takes no part in Replace: it is not a mesh shape of
  the shape list, and no other shape replaces it.
- K0.5 The flow mask has a switch for `generic` too, so turning it off
  freezes today's attachment following as well.

- K0.6 (as built, for the user's review): two `wall` connectors connect on
  position alone, since walls meet at a room corner at an angle (C5.2 asks
  for facing connectors); such a join is never followed rigidly, only by
  the fits.
- K0.7 (as built, for the user's review): a drag step that moves runs the
  whole follow operation of the release (parameter operations, collision
  shapes, a gather of the connected pieces) and so allocates as a recorded
  change does, about 5 ms a step in a Debug build for a wall in a room of
  four walls. This makes the release equal the last step by construction.
  The lighter design, a preview copy of the graph gathered at the drag's
  begin with a mesh-only parameter preview, would allocate nothing while
  moving and needs its own proof that the release matches.
- K0.8 (as built, for the user's review): a fit into an invalid floor
  outline refuses the whole change on every route, records nothing and
  keeps the redo history; `set_node_transform` records at once so that its
  reply can name the refusal, while `set_item_property` and Transform tool
  releases report it in the log, the Level Kit window and
  `level_get_flow_report`.
- K0.9 (as built, for the user's review): connections may form cycles; a
  cycle that cannot close keeps its last connection and reports it apart.

## K1 Connector types

- K1.1 `enum class Kit_connector_type : unsigned int { generic = 0, wall,
  wall_floor }` in `level/kit_connector.hpp`, with `c_str()` giving
  `generic`, `wall` and `wall-floor`, and the parse used by MCP.
- K1.2 `class Kit_connector_types`: a bit set over `Kit_connector_type`
  (`std::uint32_t bits`), `contains()`, `intersects()`, `is_empty()`, and a
  `generic` default. Being an enum, an unknown type cannot be named in C++,
  so C1.6's unit test checks only that no connector's set is empty.
- K1.3 `Kit_connector` gains `Kit_connector_types types` (default generic).
  It stays trivially copyable and allocation free. `transform_connector()`,
  the join helpers and `Kit_connector_point` (the snap search) carry it.
- K1.4 `is_compatible(const Kit_connector&, const Kit_connector&)` is
  `intersects()`: the one test for every join path (C1.3).
- K1.5 Shapes: `architecture.wall`, the door wall, the window wall and the
  Japanese wall give `start` and `end` the type `wall` and `base` the type
  `wall_floor` (L15); `start` and `end` accept `generic` too (C1.5). Their
  `top`, `front` and `back` stay generic. A helper
  `type_wall_run_connectors()` (`kit_shapes_basic.cpp`) types the four shapes'
  connectors. `basic.floor_slab` keeps generic connectors only
  (L19).

## K2 Where compatibility is tested

- K2.1 Refused with a message (C1.9). `connect_piece()` and
  `Kit_placement::connect` check `is_compatible()` before computing the pose
  and refuse with "connector '<a>' (<types>) of '<piece>' and connector
  '<b>' (<types>) of '<target>' share no connector type". This covers the
  Level Kit window's Connect rows, `level_connect_pieces` and
  `level_place_pieces`.
- K2.2 Skipped silently (C1.9). These treat an incompatible pair as absent,
  with no log line:
  - the connector snap search (`find_connector_snap()`,
    `kit_connector_snap.cpp`) skips incompatible pairs while it compares
    candidates;
  - the Brush tool's kit-mode join preview offers only compatible target
    connectors;
  - the auto-connect search (K8).
- K2.3 Not tested: the connection gather (K6.1) reads loaded records as they
  are (C6.4). A pair from the snap is compatible by construction.

## K3 Standard properties, mappings and the polygon floor

- K3.0 As built (step 2), replacing the floor parts of K3.4, K3.5, K3.7 and
  K3.8: the polygon floor is a kit layout of kind `floor` on a closed curve,
  built as the stairs layout is (`doc/editor/level_tools.md` "Floor
  layouts"). Its values are layout values (`Kit_layout_pattern::floor`,
  `floor_material`, `floor_wall_material`), not a kit piece record, so there
  is no `basic.floor_polygon` shape, no supply slot and no
  `Kit_piece.supply_materials`; it is created as every curve layout is
  (`level_create_layout`, the Level Kit window, the Create menu). The outline
  has one reader, `read_floor_outline()`. A curve edit that makes an invalid
  outline (crossing edges, a curved span, a corner off the plane) is recorded
  and reported as the layout's run problem with the slab kept, as a stairs
  layout's is; the strict refusal of C8.3 applies to the fit of step 5. The
  floor layout node takes part in connections as a connection end (step 4),
  with its edge connectors from `get_floor_connectors()`.
  The floor takes no values in this scope (no mappings; a wall's thickness
  changing a floor is "Later" of the requirements), so its `floor.thickness`
  and `floor.material` are its own layout values.
- K3.0.1 As built (step 3): a supplied material is a resolved material name
  (`Kit_supplied_value::material`, a view the caller keeps alive while it
  consumes the list), not a material pointer; the flow resolves an empty
  `floor_wall_material` to the wall slot's default before it supplies it.
  `get_supply` takes the connector's name. An edge shorter than the wall
  thickness plus 0.1 m supplies no `child.length`.

- K3.1 `enum class Kit_standard_property : unsigned int { room_height = 0,
  floor_thickness, wall_thickness, child_length, floor_material,
  wall_material }` with names `room.height` ... `wall.material`, and
  `get_kind()` returning `length` (meters, a float) or `material`. A new file
  `level/kit_standard_property.{hpp,cpp}` holds it.
- K3.2 `class Kit_property_mapping { Kit_standard_property property; const
  char* target; }`: target names a float parameter for a length, or a
  material slot for a material. `Kit_shape_definition` gains
  `std::vector<Kit_property_mapping> property_mappings`. A unit test checks
  that every target exists with the right kind (C7.1).
- K3.3 Wall mappings (C2.2): `room.height` to `height`, `wall.thickness` to
  `thickness`, `child.length` to `length`, `wall.material` to slot `wall`.
- K3.4 The polygon floor's shape, `basic.floor_polygon`, in the shape list:
  - Parameters: `thickness` (0.2 m, mapped from `floor.thickness`),
    `wall_thickness` (0.25 m) and `room_height` (1 wall height), all fixed,
    not derivable.
  - Slots: `floor` (mapped from `floor.material`) and a supply-only `wall`
    slot (`Kit_shape_definition::supply_slots`). The supply-only slot has no
    facets and holds the `wall.material` the floor supplies. Its value lives
    in a new record value, `Kit_piece.supply_materials`, a canonical JSON
    object from slot to material name (a material of the scene by name,
    else a palette name). Empty means the palette default.
  - Outline: the closed corner-point path of the node's Curve prim. It is not
    a parameter, so the parameter text and the brush cache do not see it.
- K3.5 The floor node (K0.1): a Curve prim with `Curve_closure::closed`,
  corner points only, in the floor's top plane (local y = `thickness`; the
  slab's base at y = 0). It carries `Kit_piece.shape` =
  `basic.floor_polygon`, the parameters, slot materials, switches and
  connections. Its one generated child holds the slab mesh: the outline
  extruded down by `thickness`, triangulated by ear clipping. The child is
  a walkable body and navigation input, and it is regenerated in the entry of
  every change of the outline or the values, the way the stairs layout
  regenerates its staircase (`Level_kits::append_curve_follow_ups()`). The
  child is not a kit piece and cannot be selected apart from the floor.
- K3.6 Validity (C8.3), `check_floor_outline()`: at least three corners;
  every edge at least 1 cm long; no two non-neighbouring edges cross; the
  winding stays as it was (no corner turned inside out). Every outline
  write runs it, whether a fit, a point edit, MCP or a load (a load reports
  and keeps the outline).
- K3.7 Creating (C8.2): `Level_kits::add_polygon_floor(scene_root, position,
  width, depth)` makes a rectangle. The Level Kit window has the initial
  width and depth (with the layout initial values) and an "Add Polygon
  Floor" row. The Create menu's "Level Kit" submenu and MCP
  `level_create_floor` (`width`, `depth`, `position`, or `points`) call it.
- K3.8 Point editing: the floor's corners are the curve's points, so the
  Curve tool edits them: drag, insert, remove, and the Properties rows.
  Handles are not offered: every point stays a corner. Every edit is a floor
  change (K7.2). Inserting or removing a point renumbers edges, and the same
  entry rewrites the edge names of the floor's connections (C8.4); a
  connection whose edge is removed is dropped and reported.

## K4 Supply

- K4.1 `class Kit_supplied_value { Kit_standard_property property; int
  priority; float length; std::shared_ptr<erhe::primitive::Material>
  material; }` and `class Kit_supply_list`: a `std::array` of six (one per
  property) plus a count, exposed as a `std::span`, so filling it allocates
  nothing.
- K4.2 `Kit_shape_definition::get_supply`, a
  `std::function<void(const Kit_piece_view&, std::size_t connector_index,
  Kit_supply_list&)>`. `Kit_piece_view` holds a piece's parameter values,
  its resolved slot and supply materials and, for the floor, its outline.
- K4.3 The floor, over its edge connectors, supplies at priority 2:
  - `child.length` = the edge's length minus `wall_thickness` (L9);
  - `wall.thickness` = `wall_thickness`;
  - `room.height` = `room_height`;
  - `wall.material` = its supply material.
- K4.4 A wall, over `start` and `end`, supplies `wall.thickness`, its `wall`
  slot material and `room.height` (its `height`) at priority 1 (C3.3, L3,
  C4.4).
- K4.5 Floor connectors (C2.4): edge i (from point i to point i + 1) has the
  connector `edge_<i>`. It is typed `wall_floor` and sits at the edge's
  midpoint, inset toward the inside by `wall_thickness / 2`. It faces +Y with
  its up axis toward the inside. The names come from a static table
  (`edge_0` to `edge_63`), so the connector name stays a literal (K1.3) and
  the floor has at most 64 edges, the shape connector limit.
  `get_connectors` reads the outline from `Kit_piece_view`, so the
  connectors follow it.

## K5 Responses: follow and fit

- K5.1 A shape states, per own connector type, how its piece responds to a
  connected neighbour that changed, with a priority (C3.2):
  - `follow`: a rigid move that re-joins the connector (today's attachment
    motion);
  - `fit`: the shape's fit function stages new values, outline or pose.

  `Kit_shape_definition::get_response(connector_index) -> Kit_response {
  kind, priority }`. A shape without one follows over every connector at
  priority 0.
- K5.2 The first responses:

  | Piece | Over | Response | Priority |
  |---|---|---|---|
  | wall | `wall_floor` | follow, re-joined upright at the edge's midpoint | 2 |
  | wall | `wall` | fit, two ends (K5.4) | 1 |
  | floor | `wall_floor` | fit, edge lines (K5.3) | 2 |
  | other | `generic` | follow | 0 |

- K5.3 Floor fit (C3.15, L20). Input: every connected wall whose pose
  changed, with its base connector's world position and its run direction
  (the wall's x axis). Both are taken into the floor's frame and projected
  onto its plane, which drops vertical moves and tilts. For each such wall,
  the line of its edge becomes the line through that point along that
  direction. Then each corner whose neighbouring edges changed is solved as
  the crossing of the two edges' lines. A corner between a changed edge and
  an unchanged one slides along the unchanged edge's line, as a segment
  handle drag does. `move_curve_segment()` (`level/level_repeat.hpp`) moves a
  segment along its normal only; it gains a sibling,
  `move_curve_segment_to_line()`, that takes a line, keeps the neighbour
  slide rule and the 15 degree straight-along-normal rule
  (`c_segment_slide_min_sine`), and is shared by both. The staged outline
  goes through K3.6; a refusal fails the change (C8.3, K0.3).
- K5.4 Wall fit (C3.16, L22). Input: the moved neighbour's connector, at the
  near end. The far end connector's world position is kept. The near end
  goes to the neighbour's connector position. The new `length` is the
  horizontal distance between them. The new pose is upright, centered
  between them, with its x axis from start to end. A wall whose far end is
  also connected to a changed neighbour in the same round is solved from
  both ends.
- K5.5 Wall follow over `wall_floor`: the join of today's attachment
  (twist 0, gap 0), so a wall that was slid along its run, lifted or tilted
  goes back onto its edge's midpoint, upright (L20).
- K5.6 Choosing among responses: a piece with several changed neighbours
  takes the response of the highest priority. The tie rule is C4.3's
  (K7.3). Value supplies are chosen separately, per property (C4.2).

## K6 Connection records

- K6.0 As built (step 4), replacing the record of K6.1 and K6.3: the
  connections are the entries of the existing `Kit_piece.attachment` value,
  one object for one connection (the older form, so older scenes load
  unchanged) and a sorted array for more; there is no
  `Kit_piece.connections` value and nothing to move on write. The cycle
  refusal of `check_attach()` (over every entry) stays until step 5. Until
  the flow engine of step 5 replaces the following, each entry is followed
  as an attachment: a piece with two affected entries re-joins each in turn
  (the last wins), and an entry whose target did not move is removed when
  the piece moves off it. The piece status (`get_attachment_statuses()`,
  `level_get_pieces`' `attachments`, the connection rows) has one status per
  entry; `level_get_connections` lists both ends' records.
- K6.1 One kind (L16): a new record value, `Kit_piece.connections`, on one
  end of each connection: the piece that was connected, the requesting end.
  It is a canonical JSON object `{owner, connections: [{connector, other,
  other_connector, twist_steps, gap}]}` sorted by connector. `owner` and
  `other` are glTF uids, so a copy is inert, as an attachment's is. The
  meaning does not depend on the end that stores it: following and fitting
  run both ways (K5).
- K6.2 One connection per connector, counting both ends (C3.1). Every write
  checks it against the gathered graph (K7.1). A raw write that breaks it
  loads and is reported, as raw attachment writes are today. Cycles are
  allowed: `check_attach()`'s cycle refusal is removed.
- K6.3 `Kit_piece.attachment` loads as a connection: the gather reads it as
  one more entry of its owner. Any write of the owner's connections moves it
  into `Kit_piece.connections` and clears the old value in the same entry.
  New writes never write `Kit_piece.attachment`.
- K6.4 Made by: an explicit join (with `attach` false, no record, as today),
  a snapped drag release, and auto-connection (K8).
- K6.5 Removal (C3.14). A connection of switched-off types (the flow mask)
  whose connectors a move takes apart is removed in the move's entry. A
  connection of switched-on types is kept joined by the flow, so a move does
  not take it apart. One left apart (a refused fit inside a cycle, a mask
  change) is reported as apart and kept (C3.7).
- K6.6 Detach (C9): `Level_kits::detach_pieces(nodes)` removes every
  connection with an end in the nodes, records on other pieces included, as
  one entry with no flow. It replaces `detach_piece()`. MCP
  `level_detach_pieces` replaces `level_detach_piece`. The Properties
  "Kit Piece" group, the Scene hierarchy context menu and the Level Kit
  window have Detach. `remove_connection()` removes one.
- K6.7 Queries (cold, may allocate): `get_connections(node)` gives each
  connection with the other piece, both connectors, its types and joined or
  apart. `get_connected_group(node)` gives the component.

## K7 Flow engine

`level/kit_connection_flow.{hpp,cpp}`: `class Kit_connection_flow`, owned by
`Level_kits`, with persistent scratch (graph, worklist, staged values, poses
and outlines), cleared and not reallocated in the steady state.

- K7.1 Graph. It is gathered once per change from the scene: the pieces with
  records (floors included), their connections, their connectors resolved
  through `get_connector_switches()` (a disabled connector takes part in
  nothing), each connection's shared types, and an adjacency per piece. A
  connection with no shared type switched on in the flow mask is left out
  (C3.9).
- K7.2 Inputs, from the change being recorded:
  - the origin set E: pieces moved by the author, pieces whose values,
    materials or outline the author edited, and replaced pieces. They keep
    what the author gave them (C3.6);
  - the new connections, each with its requesting end;
  - the call's `flow_mask` and `auto_connect` overrides.
- K7.3 Rounds, at most the step limit (C3.8, L13, K0.2). Each round reads the
  state the previous round left and stages:
  1. Every pending piece that is not in E and not a child of a locked
     layout (C3.11) recomputes its response and its values:
     - The response: over all its connections in the graph, the highest
       priority response whose neighbour changed. A tie goes first to the
       neighbour changed in this change, then to a new connection, then to
       the connector earliest in the piece's shape order (C4.3).
     - The values: per mapped property, the highest priority supply among
       all its connections, with the same tie rule. A chosen value is taken
       as it is (L5), and a taken parameter becomes explicit (its derive bit
       is cleared).
     - A piece whose values or pose changed makes all its neighbours in the
       graph pending.
  2. Auto-connect (K8) over the pieces changed in this round. A new
     connection makes both ends pending.

  The first round starts with these pending: the neighbours of E and the
  requesting ends of new connections. The flow ends when a round leaves
  nothing pending. Reaching the limit reports the pieces still pending.
- K7.4 Output. The staged results become operations, in order:
  - per changed piece, a `Kit_piece_parameters_operation` through the
    existing `set_parameters()` path (swaps the mesh and body; the brush
    cache shares meshes);
  - slot material writes;
  - floor outline writes (`Curve_set_operation`, with the slab
    regeneration);
  - connection record writes and removals;
  - `Node_transform_operation`s for the moved pieces.
- K7.5 `Kit_connection_operation` replaces `Kit_attachment_follow_operation`
  as the last operation of every entry that changes kit pieces or a floor's
  curve. Every existing call site keeps calling it, through
  `make_attachment_follow_operation()` renamed
  `make_connection_follow_operation()`. New call sites:
  - `set_slot_material()` and supply material writes;
  - Replace;
  - `connect_piece()` and connected placements;
  - floor curve edits (`Curve_set_operation` follow-ups);
  - `remove_connection()` and Detach (graph change only, no flow).

  Its first execute runs K7.3 from the state the entry's earlier operations
  left and records the operations of K7.4. Redo replays them and undo
  reverses them (C3.12), as the follow operation does today. A refused fit
  makes the first execute fail the whole entry with the reason, and nothing
  is recorded.
- K7.6 Report (C3.13): `Kit_flow_report`, filled on request (cold, may
  allocate). It lists:
  - the taken values (piece, property, value, supplier, priority, tie) and
    the values not taken, with their reasons;
  - the responses (follow or fit, from which neighbour);
  - new, removed and apart connections;
  - a refusal, and whether the limit was reached.

  Join and edit replies over MCP include it as `flow`. The Level Kit
  window's status line shows a one-line summary.

## K8 Auto-connection

- K8.1 The setting is "Auto-connect", on by default (L1). It is checked per
  change, and a call's `auto_connect` overrides it.
- K8.2 Search: the world frames of all enabled connectors of the scene's
  kit pieces and floors are gathered once per change into the graph scratch
  and updated for the pieces each round changes. For each connector of a
  piece that changed in the round, every frame of another piece is a
  candidate when it passes all of:
  - compatible (K1.4);
  - position within 0.1 mm;
  - outward axes opposed within 1e-4 rad;
  - both connectors free (K6.2).

  Up axes are ignored (L7). The match is a plain scan; grouping by grid cell
  is added only if a measurement asks for it.
- K8.3 A match records a connection on the changed piece (the requesting
  end). When both ends changed, it goes on the piece earlier in the scene's
  node order. Auto-connection moves no piece (C5.4).

## K9 Previews

- K9.1 Transform tool drag (C3.10, L10). Each drag step that changes a pose
  or the snap pair runs the release's own follow operation outside the undo
  stack and keeps it applied until the next step undoes it (K0.7). The
  dragged pieces are E, and a connector snap pair is a hypothetical new
  connection. This covers both cases:
  - a free wall snapped onto a floor previews the values it would take;
  - a connected wall dragged previews the floor fit and the other walls.

  When the snap releases its pair or changes it, the preview is restored
  before the next step. A step whose fit is refused keeps the last valid
  preview and shows the refusal (K0.3). Release records the entry; its first
  execute recomputes from the committed state, which gives what the last
  snapped step showed.
- K9.2 Brush tool kit-mode join preview: the previewed piece takes the
  values its target supplies, and the preview brush is built at those
  values. On placement, the entry runs the full flow.
- K9.3 Curve tool drags of a floor's corners preview the walls' follow and
  values in the same way.
- K9.4 A held step (the poses and the snap pair of the step before) runs
  nothing and allocates nothing. A step that moves costs what a recorded
  change costs (K0.7).

## K10 Settings and exposure

- K10.1 Settings in `Surface_snap_config`, version 6:
  - `connector_flow_mask`: a `Kit_connector_types` value, all types by
    default (C3.9, L21);
  - `connector_auto_connect`: `enum class Connector_auto_connect { off, on
    }`, default on (L1);
  - `connection_step_limit`: an int, default 64, at least 1 (L13).

  `Level_kits` has a get and set for each. The connector rows of
  `surface_settings_ui` show them (one checkbox per type for the mask) where
  the connector snap rows are: the Level Kit window, the viewport toolbar and
  the Transform tool settings.
- K10.2 MCP:
  - The catalog: connector `types`, the shape's `property_mappings`, and its
    supplies and responses per connector with priority.
  - `level_get_connectors`: each connector's `types`.
  - New tools: `level_get_connections` (a piece's connections, or the
    scene's connected groups), `level_remove_connection`,
    `level_detach_pieces` (K6.6) and `level_create_floor` (K3.7).
  - The floor's outline is read and written with the curve tools of
    `doc/editor/curves.md` "MCP".
  - `level_connect_pieces` and `level_place_pieces` take `flow_mask` and
    `auto_connect` overrides.
  - `flow` in the replies of the join tools, `level_set_piece_parameters`,
    `level_set_piece_material`, `level_replace_pieces`,
    `transform_selection` and the curve edit tools.
  - The connector settings tool takes the three settings.
- K10.3 Properties "Kit Piece" group:
  - the types beside each connector switch (C1.7);
  - a Connections list: other piece, both connectors, joined or apart, and
    Remove (C6.3);
  - Detach;
  - the floor's supply material row.
- K10.4 Connector indicators (C1.8): a connector's color is taken from its
  first type (generic keeps today's color). The label of an indicator under
  the pointer names all its types.

## K11 Persistence and Replace

- K11.1 `Kit_piece.connections` and `Kit_piece.supply_materials` ride the
  node's `ERHE_node` properties as the other record values do. The floor's
  outline is its curve, saved as curves are. An empty value writes nothing.
  A scene without them loads unchanged (C6.4).
- K11.2 Replace keeps each connection whose connector the new shape has and
  whose types still meet the other end's, drops the others and reports them
  (C6.5). The flow then runs with the replaced piece in E. The polygon floor
  is not replaceable (K0.4).

## K12 Verification

- Unit tests (`editor_level_tests`):
  - no connector type set is empty;
  - mapping targets exist with the right kind;
  - the floor connector inset on rectangles and on a non-rectangular
    outline;
  - `check_floor_outline()` cases;
  - `move_curve_segment_to_line()` against `move_curve_segment()` for
    moves along the normal;
  - the four-wall corner meeting of C7.1;
  - the flow engine on hand-built graphs: priorities, each tie rule, floor
    fit, wall fit, the step limit, a locked child, E kept, a refusal.
- A verify group `--only-connections` in `scripts/level_tools_verify.py`
  covering C7.2.
- The gates of each step: `editor_level_tests`, `level_tools_verify.py`,
  `curve_verify.py` and `ctest -C Debug -L editor -j1` in the headless tree.
