# Level kit surface attachments: plan

Status: in progress

A plan for GitHub issue 304, "Level kit nodes should remember where they
were attached": a kit piece A placed on another piece B with snap to surface
becomes a child of B in the scene graph and remembers its place on B, and
when B is updated - moved, turned, or changed in shape, as a wall whose
floor corners move - A's position and orientation are updated so that A
stays attached to B. The issue covers the surface case only: a piece joined
by connectors already remembers its join (`doc/editor/level_tools.md`
"Attachments") and is outside this plan.

This document states the rulings, the requirements (S-numbers), the design
sketch and the build order. Every step is built ("State"); the behavior is
described in `doc/editor/level_tools.md` "Surface attachments". Open are
the user's windowed test and the review of the planner's choices ("For the
user to review").

## State

Built 2026-11-20, steps 1 - 7, headless only. Final gates:
`editor_level_tests` 944, `level_tools_verify.py` 2193 (its group
`--only-surface-attachment` 144), `navigation_verify.py` 76, `ctest -L
editor` 108; `curve_verify.py` 156 of 157 (`doc/plans/open_items.md`
"Verify scripts").

Where the built behavior differs from the text below:

- S7.1: Detach is the Kit Piece group's Detach row, shown for a surface
  attachment alone too; the "On" row is text.
- S5.2: a replaced layout child's attached pieces keep their local
  transforms; they are parked under the scene root between the removal and
  the insert of the successor.
- The record has a sixth key, `on_surface`, false while the status is
  `off_surface` (S3.4).
- Prefab instances: a record inside a template names the instance's node.
- A piece placed by the Brush tool attaches when Snap To Surface found the
  surface; Place As Layout leaves the layout node under the scene root, and
  a kit layout node snapped as a node does not attach (S2.6 second
  sentence is not built).

User test (windowed, pending):

1. Brush tool, kit mode, Snap To Surface on: place a crate on a table and
   a picture on a wall; each shows under its support in the item tree and
   "On" in Properties.
2. Move, turn and resize (parameter drag) the table and the wall: the
   attached pieces stay, live during the drag; undo restores.
3. A polygon floor with connected walls: drag a floor corner; a picture on
   a wall keeps its proportion along the wall.
4. A vase on a table on a floor: move the floor, resize the table.
5. Drag the crate along the table top (stays attached), off the table
   (detaches to its home), onto another piece (attaches there).
6. A locked segments layout of walls: place a picture on a wall, select
   and move it, drag a control point, reduce the segment count.
7. Duplicate and group copy of a table with a crate; Detach; delete the
   table and undo.

## Rulings

User, 2026-11-20:

- T1 The plan covers the surface case only; connector joins are a different
  thing and have priority over a surface attachment (S2.4).
- T2 The place is proportional: a position in the support's bounds, not an
  offset in meters (S1.1, S3.2).
- T3 Content that is no kit piece is not a support now; later.
- T4 A child of a locked kit layout node is a support (S2.3, S5).
- T5 The surface settings have an Attach To Surface switch, on by default.
- T6 A piece whose support has no surface under it stays attached (S3.4),
  should the proportional place leave that case open at all.
- T7 A floor is a support: moving it carries every piece snapped onto it.
- T8 The attached piece is a child of its support's node in the scene graph;
  the support is the parent (S1.2, S2.1).
- T9 The attached piece is updated whenever its support - its parent - is
  updated (S3).
- T10 Chains are expected: A attached to B attached to C (S4).

## Where the code stands

- A connector join is remembered in `Kit_piece.attachment` (`Kit_attachment`,
  `kit_piece.hpp`). Every change that moves a node or changes a piece's
  values ends its undo entry with a `Kit_attachment_follow_operation`, whose
  first execute gathers the scene's records, marks the followers and joins
  them again in dependency order (`follow_attachments()`,
  `level/level_kits_attachment.cpp`). Live drags run the same operation at
  each step ("Flow preview"). A piece at or below a moved node is never
  joined again: it rides the scene graph.
- Snap to surface (`find_surface_placement()`, `level_queries.hpp`) returns
  the pose, the distance and `surface_found`. It does not say which mesh the
  piece came to rest on, nothing is recorded, and a placed piece goes under
  the scene root (`Kit_placement::parent` unset).
- The callers of snap to surface: the Brush tool's kit mode
  (`make_placement_pose()`, then `Level_kits::place()`), a Transform tool
  drag released surface-snapped, `Level_kits::snap_to_surface()` (the Level
  Kit window's snap buttons, `level_snap_to_surface`), the MCP placement
  tools and `transform_selection`, Replace, and kit layout generation.
- A piece already has children: its light nodes ("Lights on pieces").
- A locked kit layout node locks everything below it:
  `find_locking_kit_layout()` and the structure predicates refuse edits,
  moves, inserts and selection of any node below the layout, and a
  regeneration removes each changed child with its subtree
  (`level_kits_layout.cpp`).

## Terms

- Surface attachment: a piece that is a child of the piece it rests on and
  carries the record of its place there.
- Attached piece (A): the child; it carries the record and follows.
- Support (B): the parent; the piece A rests on. It never responds to A.
- Rest point: the point of A that lies on the surface - the center of A's
  resting face, or A's origin with the anchor rest.
- Place: the rest point as a proportion of B's node-space bounds, and the
  search direction in B's node space. A's rotation on B is its node's local
  rotation.
- Home: the first ancestor of A that is neither a kit piece nor a kit layout
  node - a group node, another plain node or the scene root.

## Requirements

### S1 Record

- S1.1 `Kit_piece.surface_attachment` on the attached piece, beside
  `Kit_piece.attachment`: a canonical JSON text with `target` (the glTF uid
  of the support), `position` (the rest point in B's node-space bounds, 0 at
  the bounds' minimum and 1 at their maximum on each axis; T2), `direction`
  (the unit search direction in B's node space), `orient` (whether the snap
  turned A onto the surface) and `rest` (`leading_face` or `anchor`). Floats
  are hexadecimal.
- S1.2 The attachment is the record together with the hierarchy: it is
  active only while A's parent is the node `target` names (T8). A record on
  a node with another parent is inert - the author moved A in the item tree,
  or a copy landed elsewhere - and the first change that reads it inert
  removes it.
- S1.3 Paste Properties does not paste the record. A copy that carries both
  A and B (Duplicate of B's subtree, a group copy) writes the copied B's uid
  into the copied A's record, so the copy is attached as its source is. A
  copy of A alone under the same parent is attached at the same place.
- S1.4 The record and the parent link are saved and loaded with the scene
  as every node property and the node hierarchy are.

### S2 What attaches

- S2.1 Every snap to surface of a kit piece that finds a place on a support
  makes A a child of the support, at A's place in the world, and writes the
  record, in the undo entry of the snap: the Brush tool's kit-mode placement
  (the new node is inserted under the support), a Transform tool drag
  released surface-snapped, `Level_kits::snap_to_surface()`, the MCP
  placement and transform tools with surface options, and Replace's re-snap
  (the last four move an existing node, with its subtree, under the
  support). A's world scale is kept at the moment it attaches.
- S2.2 The support is the piece that owns the face A's rest was found on:
  the hit of the base center's line for orient and for the anchor rest, the
  supporting hit of the taken place for snap alone (the base center's line
  first when several lines rest at the place). The snap excludes A's own
  subtree from its rays, so the support is never A or a node below A.
- S2.3 A support is a kit piece, a kit layout node's generated mesh (a
  floor layout, a stairs layout; A becomes a child of the node that carries
  that mesh) or a child of a locked kit layout node (T4, S5). Content that
  is no kit piece records nothing and leaves A's parent alone (T3).
- S2.4 Connectors come first (T1). A piece that holds a connector connection
  takes no surface attachment: a snap of it changes neither its parent nor
  a record. A join made on an attached piece removes the record in the same
  entry and moves the piece to its home (S6.3). A Brush tool placement that
  joins a connector is placed as today, under the scene root.
- S2.5 A snap of an attached piece that finds no place, or a place on
  something that is no support, detaches it (S6.3) in the same entry.
- S2.6 Kit layout elements record nothing: a surface-snapped layout snaps
  its elements at every generation. The kit layout node itself is a piece
  for S2.1 when it is snapped as a node.
- S2.7 The surface settings gain one switch, Attach To Surface, on by
  default (T5). Off, a snap changes no parent and writes no record; an
  attached piece is still judged by S6. The MCP tools that snap take
  `attach` (default true), as `level_connect_pieces` does.

### S3 Following the support

- S3.1 The support moved or turned, by anything - the author, a connector
  follow, a floor fit, a group move, its own support: A rides the scene
  graph. Nothing is computed and no record is read (T9).
- S3.2 The support's shape changed - its parameter values, its shape id
  (Replace), a floor layout's polygon, a regenerated layout child: A is
  carried. The rest point goes to the recorded proportion of B's new bounds
  and A's local translation follows from the rest point, A's local rotation
  and A's own bounds. A picture a quarter along a wall and halfway up stays
  a quarter along and halfway up when the wall grows, and a crate on a
  table top (proportion 1 along Y) stays on the top at any height. A's
  local rotation and scale are kept. An axis on which B's bounds have no
  extent carries the bounds' value there.
- S3.3 Rest again: after the carry A searches along the recorded direction,
  against the faces of B only, with the recorded `orient` and `rest`,
  forward and backward by the surface settings' max distance, and takes the
  place found. On a support whose surface is a face of its bounds (a wall,
  a floor, a table top) the carry is at rest already and the search answers
  zero; on another (a hill, an arch) it puts A on the surface, and an
  oriented piece on the new normal. The record's place is then written
  again from the pose taken.
- S3.4 No surface of B on A's line (an opening that grew under a picture, a
  floor polygon whose corner moved away from under a crate): A keeps the
  carried pose, the record stays and its status reads `off_surface`; the
  next shape change of B that puts a face on the line rests A again (T6).
- S3.5 The support's scale changed: A scales with it as any child node
  does.
- S3.6 The change of B, every carry below it and every record write are one
  undo entry.
- S3.7 Live: a parameter drag of B, a floor corner drag and a layout's
  curve point drag show A at each step where the release puts it. Moves
  and turns of B show A live through the scene graph.

### S4 Chains

- S4.1 Surface attachments form a forest: each attached piece has one
  support, its parent, and S2.2 rules out a support below the piece, so
  there is no cycle among them. A on B on C is A a child of B a child of C.
- S4.2 C moved: B and A ride (S3.1).
- S4.3 C's shape changed: B is carried on C and rests again (S3.2, S3.3); A
  rides B, whose shape did not change. Each changed piece's attached
  children are carried, parents before children, and the walk goes on below
  a carried piece only where that piece's own shape changed in the entry.
- S4.4 B's shape changed: A is carried on B; C is not touched.
- S4.5 Mixed chains: a support that follows a connector join (a table
  joined to a wall that is fitted to a floor) is moved by the connector
  follow and its attached pieces ride it; a connector follow that changes
  the support's values (a wall taking a new length) makes it a changed
  piece for S3.2 in the same entry. The surface pass runs after the
  connector follows and the value flow of the entry have settled, so it
  reads each support's final shape once.
- S4.6 A connector attachment of a piece that rides below a surface-attached
  piece is judged by the existing rules for a piece riding below a follower
  ("Attachments", detaching).
- S4.7 Moving A carries its own attached pieces with it (they are below A);
  attaching A to another support carries them too.

### S5 Locked kit layouts

- S5.1 An attached piece below a child of a locked kit layout node is the
  author's piece, not the layout's: the lock ends at it.
  `find_locking_kit_layout()`, the structure predicates and the selection
  redirect treat it and everything below it as unlocked, and an insert
  under a locked layout's child is allowed when it is a surface attachment.
- S5.2 A regeneration keeps the attached pieces. Child i is kept by the
  existing rule, its attached pieces not counted as children. Where the
  layout replaces child i by a new node, the attached pieces of the old
  child move to the new child before the old one is removed, their records
  take the new child's uid, and they are carried on the new shape (S3.2).
- S5.3 The attached pieces of an element the regeneration removes without a
  successor (the count shrank) are detached (S6.3) at their world poses, in
  the regeneration's entry, with an `editor.scene` info line each.
- S5.4 A regeneration's preview steps carry the attached pieces as its
  release does (S3.7).
- S5.5 Unlocking or locking a layout changes nothing for attached pieces.

### S6 The author's own moves of A

- S6.1 A moved alone and released surface-snapped onto a support attaches
  there: the same support at a new place (the record written again), or
  another (parent changed, S2.1).
- S6.2 A moved alone without a snap that still rests on its support (its
  rest point on a face of B within 0.1 mm) keeps its attachment, and the
  place is written again. So a nudge along a table top, or a turn about the
  surface normal, keeps the piece attached.
- S6.3 Detach: A moved alone off its support, the Properties window's
  Detach and `level_detach_piece` remove the record and move A, with its
  subtree, to its home, keeping its world transform, in the same entry,
  with an `editor.scene` info line.
- S6.4 A's own shape change keeps the attachment: A rests again on B from
  its pose (its bounds changed), as S3.3.
- S6.5 A reparented by the author in the item tree: the record is inert
  under the new parent and is removed (S1.2).

### S7 Exposure

- S7.1 Properties, Kit Piece group: one row naming the support ("On:
  <name>", with the status) beside the connection rows; Detach removes it.
- S7.2 `level_get_pieces` reports `surface_attachment` (`state`: `active`,
  `off_surface` or `inert`; `reason`, `target_uid`, `target_id`,
  `target_name`, `position`, `direction`, `orient`, `rest`); the placement
  and snap tools' replies name the support each piece attached to.
- S7.3 Level tools that list or act on the pieces of a group, a region or a
  scene reach attached pieces through the subtree: group copy and mirror,
  the level check, the overlap and support reports, Replace over a
  selection.

### S8 Verification

- S8.1 Unit tests (`editor_level_tests`, scene free): the record's text
  round trip, the carry (bit exact for unchanged bounds; a rotated piece; a
  zero-extent axis), the support choice among a place's hits.
- S8.2 `scripts/level_tools_verify.py --only-surface-attachment`: placement
  on a wall and on a table makes a child with a record; the wall moved and
  turned carries the piece; a floor corner move that fits the wall carries
  a piece on the wall at its proportion of the new length, in the same
  entry and at each preview step; a table height change keeps the piece on
  the top; a hill's form change rests the piece again; chains (a vase on a
  table on a floor: the floor moved, the floor's polygon changed, the
  table's height changed); a table joined to a wall by connectors with a
  vase on it; a piece on a wall of a locked segments layout - selectable
  and movable, following a control point drag, re-homed when the element is
  replaced, detached when the element goes; a nudge on the surface keeps
  the attachment, a move off it detaches to the home; a snapped move
  attaches to another support; a connector join removes the record; group
  copy keeps the copies attached; deleting the support; undo and redo of
  each; save and load; Replace; `off_surface` and `inert` states.

## Design sketch

- `Surface_placement` gains `support_mesh` (`const erhe::scene::Mesh*`): the
  hit records of `level_queries.cpp` carry their mesh (`Surface_hit` has it;
  `Surface_hit_point` gains it), and `find_support_height()` returns the
  index of the supporting hit. `Kit_placement_pose` and `Kit_snap_result`
  pass it on.
- `find_surface_placement()` gains an optional `only` span of meshes beside
  `excluded`, used by S3.3 with the support's mesh.
- `Kit_surface_attachment` with `make_surface_attachment_text()`,
  `parse_surface_attachment_text()` and `read_kit_surface_attachment()` in
  `kit_piece.hpp`, and the attached property
  `Kit_piece::surface_attachment_property`.
- The carry is a scene-free function of the record, B's bounds, A's bounds
  and A's local rotation and scale, giving A's local translation; B's
  bounds are `mesh_local_bounds()` of its mesh.
- Attaching: `Kit_placement::parent` is the support for a new piece (the
  local pose is the world pose taken into the support's space). For an
  existing piece one helper appends an `Item_parent_change_operation`, the
  `Node_transform_operation` that keeps the world pose, and the record
  write to the entry the caller already records; detaching is the same
  helper toward the home.
- Following: `Kit_attachment_follow_operation` gets a surface pass after
  its fits, flow and connector follows. Its input is the entry's changed
  pieces (the ones whose values or shape changed, by the author or by the
  flow); for each it walks the direct children that hold an active record,
  carries and rests them, and appends their moves and record writes. The
  pass reads no record for a moved node (S3.1). The S6 judgement of a moved
  attached piece (still at rest, snapped, or off) runs in the same pass,
  from the entry's moved nodes.
- `has_attachments()` counts both kinds, so a scene without records still
  adds no operation; there is no per-frame work.
- Rest again needs B's faces at the new values: the pass runs after B's
  mesh is replaced and commits the scene's raytrace scene before the
  search, as `snap_to_surface()` does. The parameter drag preview
  (`step_parameter_follow_preview()`) reads only the dragged piece's
  connectors today; for S3.7 it runs the surface pass against the step's
  mesh. Whether that mesh is in the raytrace scene at each step is the
  first thing step 4 establishes; where it is not, the step's primitive is
  traced directly.
- Locked layouts: `find_locking_kit_layout()` stops at the first node with
  an active record on its way up; the kept-child test of
  `level_kits_layout.cpp` skips attached pieces as it skips the light
  nodes; the removal of a replaced child is preceded by the parent changes
  of S5.2 or S5.3.
- Copies: Duplicate and the group copy know each source node's copy; they
  rewrite `target` where the copied parent is a copy of the record's
  target.

## Build order

Each step is one commit, checked with the test cases and the verify group it
adds; the slow suites run once at the end (`AGENTS.md` "When to run the
slow test suites").

1. Support of a place: `support_mesh` through `find_surface_placement()`,
   `make_placement_pose()` and `snap_to_surface()`; the `only` filter; unit
   tests; the `level_snap_to_surface` reply names each piece's support.
2. Record and status: `Kit_surface_attachment`, its text, the property, the
   carry function with its unit tests, the status query, `level_get_pieces`,
   the Properties row.
3. Attaching and detaching: the parent change helper; the Brush tool
   placement, `place()` with surface options, `snap_to_surface()`, the
   Transform tool release, Replace; S2.4, S2.5, S6; the Attach To Surface
   setting; Detach.
4. Following: the surface pass (carry, rest again, chains, order after the
   connector follows), one undo entry; live previews of parameter drags and
   floor corner drags.
5. Locked layouts: the lock ends at an attached piece; regeneration keeps,
   re-homes or detaches attached pieces; its preview steps.
6. Copies (Duplicate, group copy), the subtree reach of S7.3, persistence,
   Replace; the verify group complete.
7. Documentation: `doc/editor/level_tools.md` "Surface attachments" under
   "Connectors" beside "Attachments", the changed rules of "Kit layout
   nodes", and the MCP tool descriptions. Final gates: `editor_level_tests`,
   `level_tools_verify.py`, `curve_verify.py`, `ctest -L editor`.

## For the user to review

Planner's choices the rulings do not cover:

- P1 Detach moves the piece to its home, the first ancestor that is no kit
  piece and no kit layout node (S6.3), not to the scene root and not to the
  support's parent.
- P2 A nudge that leaves the piece at rest on its support keeps the
  attachment (S6.2); only a move off the surface detaches.
- P3 Deleting a support deletes the pieces attached to it, as the delete of
  any node removes its subtree: deleting a floor deletes the furniture
  snapped onto it. Undo brings all back.
- P4 A scaled support scales its attached pieces (S3.5). Kit pieces are
  made at scale 1, so this shows only where the author scales a support
  with the Transform tool.
- P5 A floor polygon edit moves the pieces on the floor proportionally in
  the floor's bounds (T2, T7), and a piece that ends over no floor stays
  attached as `off_surface` (S3.4).
- P6 A piece with a connector connection is never surface-attached, and a
  join detaches it (S2.4): the reading taken of T1.
- P7 The pieces attached to a layout element that a regeneration removes
  for good are kept, detached at their world poses (S5.3), not deleted.

## Open questions

None.
