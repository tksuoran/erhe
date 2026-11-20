# Level kit surface attachments: plan

Status: proposed

A plan for GitHub issue 304, "Level kit nodes should remember where they
were attached": a kit piece A placed on another piece B with snap to surface
remembers B and its place on B, and when B is updated - moved, turned, or
changed in shape, as a wall whose floor corners move - A's position and
orientation are updated so that A stays attached to B. The issue covers the
surface case only (user, 2026-11-20): a piece joined by connectors already
remembers its join (`doc/editor/level_tools.md` "Attachments") and is
outside this plan.

This document states the requirements (S-numbers), sketches the design and
gives a build order. Nothing of it is built. It extends
`doc/editor/level_tools.md` "Attachments", "Surface placement" and "Surface
settings". The choices marked "proposed" are the planner's and wait for the
user's ruling ("Open questions").

## Where the code stands

- A connector join is remembered in `Kit_piece.attachment` (`Kit_attachment`,
  `kit_piece.hpp`): owner uid, target uid, the two connector names, twist
  and gap. Every change that moves a node or changes a piece's values ends
  its undo entry with a `Kit_attachment_follow_operation`, whose first
  execute gathers the scene's records (`gather_attachments()`), marks the
  followers and joins them again in dependency order
  (`follow_attachments()`, `level/level_kits_attachment.cpp`). Live drags
  run the same operation at each step ("Flow preview").
- Snap to surface (`find_surface_placement()`, `level_queries.hpp`) finds a
  resting pose along a direction and returns the pose, the distance and
  `surface_found`. It does not say which mesh the piece came to rest on, and
  nothing is recorded: a piece snapped onto a wall stays in the air when the
  wall moves.
- The callers of snap to surface: the Brush tool's kit mode
  (`make_placement_pose()`, then `Level_kits::place()`), a Transform tool
  drag released surface-snapped, `Level_kits::snap_to_surface()` (the Level
  Kit window's snap buttons, `level_snap_to_surface`), the MCP placement
  tools and `transform_selection`, Replace, and kit layout generation.

## Terms

- Surface attachment: the record of a piece resting on a surface of another
  piece.
- Attached piece (A): the piece that carries the record and follows.
- Support (B): the piece A rests on. It never responds to A.
- Place: A's pose and the search direction, in B's node space.

## Requirements

### S1 Record

- S1.1 A surface attachment is a value of the attached piece,
  `Kit_piece.surface_attachment`, beside `Kit_piece.attachment`: a canonical
  JSON text with `owner` and `target` (glTF uids), `position` and `rotation`
  (A's pose in B's node space), `direction` (the unit search direction in
  B's node space), `orient` (whether the snap turned A onto the surface) and
  `rest` (`leading_face` or `anchor`). Floats are hexadecimal.
- S1.2 A piece has at most one surface attachment.
- S1.3 The record follows the rules of `Kit_piece.attachment` for owner,
  copies and uids: read only on the node `owner` names, not pasted by Paste
  Properties, an ambiguous target uid makes it dormant, and it survives a
  save and a load.
- S1.4 Asset files written before this feature read as pieces without a
  surface attachment; no migration.

### S2 What attaches

- S2.1 Every snap to surface of a kit piece that finds a place records the
  attachment when the surface under the piece belongs to a support that
  qualifies (S2.3): the Brush tool's kit-mode placement, a Transform tool
  drag released surface-snapped, `Level_kits::snap_to_surface()`, the MCP
  placement and transform tools with surface options, and Replace's
  re-snap.
- S2.2 The support is the piece that owns the face A's rest was found on:
  the hit of the base center's line for orient and for the anchor rest, the
  supporting hit of the taken place for snap alone (the base center's line
  first when several lines rest at the place).
- S2.3 Proposed: a support is a kit piece or a kit layout node's generated
  mesh (a floor layout, a stairs layout). Content that is no kit piece (an
  imported mesh) records nothing (Q2). A child of a locked kit layout node
  is not a support and is not attached, as for connector joins
  (`check_attach()`): its layout owns it and regenerates it (Q3).
- S2.4 Connectors come first. A piece that holds a connector connection
  records no surface attachment, and a join made on a piece removes its
  surface attachment in the same undo entry. A Brush tool placement that
  joins a connector records the join only.
- S2.5 A snap that finds no place, or a place on no qualifying support,
  removes an existing surface attachment of the piece (it now rests
  elsewhere) in the same entry.
- S2.6 Kit layout elements record nothing: a surface-snapped layout snaps
  its elements at every generation. The kit layout node itself is a piece
  for S2.1 when it is snapped as a node.
- S2.7 Proposed: the surface settings gain one switch, Attach To Surface, on
  by default; off, snaps record nothing and keep existing records only
  while the piece stays at its place (Q4). The MCP tools that snap take
  `attach` (default true), as `level_connect_pieces` does.

### S3 Following

- S3.1 A is a follower when its support, or a node above its support, was
  moved or changed in the entry (moved by the user, by a connector follow,
  by a floor fit or by another surface follow), and A is not at or below a
  moved node.
- S3.2 Carry: A's new pose is the recorded place taken through B's new node
  transform, so A keeps its position and orientation relative to B through
  any move and turn of B. A's own scale is kept.
- S3.3 Rest again: when B's shape changed (its parameter values, its shape
  id, its scale), A then searches from the carried pose along the carried
  direction, against the faces of B only, with the recorded `orient` and
  `rest`, forward and backward by the surface settings' max distance, and
  takes the place found. So a crate on a table follows the table's new
  height, a lamp on a wall the wall's new thickness, and an oriented piece
  the new normal. The record's place is then written again from the pose
  taken.
- S3.4 Proposed: the place is a fixed offset in meters from B's origin, not
  a proportion of B's bounds: a picture 1 m from a wall's center stays 1 m
  from its center when the wall grows (Q1).
- S3.5 No surface of B on A's line (B shrank away from under A): proposed, A
  keeps the carried pose, the record stays and its status reads
  `off_surface`; the next change of B that puts a face on the line rests A
  again (Q5).
- S3.6 Order: followers move in dependency order through one graph with the
  connector attachments - a vase on a table joined to a wall follows after
  the table. A surface follower never moves its support, so a cycle needs a
  connector link; `Kit_follow_order` resolves it as today.
- S3.7 The change, every follower's move and every record write are one undo
  entry.
- S3.8 Live: a Transform tool drag, a Properties transform row drag and a
  parameter drag of B show A at each step where the release puts it.
- S3.9 Not followed: a kit layout regeneration of the support's parent, as
  for connector attachments.

### S4 The author's own moves

- S4.1 A moved together with B (both selected, or A below B) keeps its
  record and place.
- S4.2 A moved alone and released surface-snapped onto a qualifying support
  attaches there (the same support at a new place, or another).
- S4.3 A moved alone and released otherwise loses its record, with an
  `editor.scene` info line, in the same entry.
- S4.4 A's own parameter change keeps the record: A rests again on B from
  its pose (its bounds changed), as S3.3.
- S4.5 Detach (the Properties window's Detach, `level_detach_piece`) removes
  the surface attachment too.

### S5 Dormant

- S5.1 The record stays and is not followed while the target is not in A's
  scene, the target uid is ambiguous, or a side is a child of a locked kit
  layout node. `get_surface_attachment_status()` reports `active`,
  `off_surface` or `dormant` with the reason.

### S6 Exposure

- S6.1 Properties, Kit Piece group: one row naming the support ("On: <name>",
  with the status) beside the connection rows; Detach removes it.
- S6.2 `level_get_pieces` reports `surface_attachment` (`state`, `reason`,
  `target_uid`, `target_id`, `target_name`, `position`, `rotation`,
  `direction`, `orient`, `rest`).
- S6.3 Replace: a replaced support keeps its uid, so its attached pieces
  rest again on the new shape (S3.3); a replaced attached piece rests again
  on its support.

### S7 Verification

- S7.1 Unit tests (`editor_level_tests`, scene free): the record's text
  round trip, the carry (place through a new transform, bit exact for an
  unmoved support), the support choice among a place's hits.
- S7.2 `scripts/level_tools_verify.py --only-surface-attachment`: placement
  on a wall and on a table records; the wall moved and turned carries the
  piece (position and rotation compared); a floor corner move that fits the
  wall carries a piece on the wall in the same entry and at each preview
  step; a table height change rests the piece again; oriented and anchor
  rests; chains (vase on table on floor); own move detaches, snapped own
  move re-attaches; a connector join removes the record; undo and redo;
  save and load; Replace; dormant and `off_surface` states.

## Design sketch

- `Surface_placement` gains `support_mesh` (`const erhe::scene::Mesh*`): the
  hit records of `level_queries.cpp` carry their mesh (`Surface_hit` has it;
  `Surface_hit_point` gains it), and `find_support_height()` returns the
  index of the supporting hit. `Kit_placement_pose` and `Kit_snap_result`
  pass it on.
- `find_surface_placement()` gains an optional `only` span of meshes beside
  `excluded` (one test in `is_surface_mesh()`'s caller), used by S3.3 with
  the support's meshes.
- `Kit_surface_attachment` with `make_surface_attachment_text()`,
  `parse_surface_attachment_text()` and `read_kit_surface_attachment()` in
  `kit_piece.hpp`, and the attached property
  `Kit_piece::surface_attachment_property`.
- One maker, `Level_kits::make_surface_attachment(node, pose, support,
  direction, options)`, and one writer operation
  (`make_surface_attachment_operation()`); every caller of S2.1 appends it
  to the entry it already records. `Kit_placement` and the Transform tool's
  release hand the support through.
- Following lives in `follow_attachments()`: `gather_attachments()` also
  reads surface records into `Attached_piece` entries of a second kind (a
  kind enum; the connector fields unused), `mark_followers()` treats their
  target as it treats a connector target, and the join step of a surface
  entry runs carry and rest again in place of `attachment_motion()`. The
  detach rule of S4.3 joins `Follow_detach`. `has_attachments()` counts
  both kinds, so a scene without records still adds no operation.
- Rest again needs B's faces at the new values: the follow operation runs
  after B's mesh is replaced, and commits the scene's raytrace scene before
  the search, as `snap_to_surface()` does. The parameter drag preview
  (`step_parameter_follow_preview()`) reads only the dragged piece's
  connectors today; for S3.8 it rests surface followers against the
  preview mesh of the step. Whether that mesh is in the raytrace scene at
  each step is the first thing step 4 establishes; where it is not, the
  step's primitive is traced directly.
- A follower's move is a `Node_transform_operation` inside the follow
  operation, as today; a place rewritten by S3.3 is a record write in the
  same operation.
- No per-frame work: records are read when a change ends its entry or a
  preview steps.

## Build order

Each step is one commit, checked with the test cases and the verify group it
adds; the slow suites run once at the end (`AGENTS.md` "When to run the
slow test suites").

1. Support of a place: `support_mesh` through `find_surface_placement()`,
   `make_placement_pose()` and `snap_to_surface()`; the `only` filter; unit
   tests; the `level_snap_to_surface` reply names each piece's support.
2. Record and status: `Kit_surface_attachment`, its text, the property, the
   status query, `level_get_pieces`, the Properties row, Detach. Records
   written by MCP only.
3. Recording: the Brush tool placement, `place()` with surface options,
   `snap_to_surface()`, the Transform tool release, Replace; S2.4 and S2.5;
   the Attach To Surface setting.
4. Following: carry and rest again in `follow_attachments()`, order with
   connector attachments, S4 detach rules, one undo entry; live previews of
   transform drags and of parameter drags.
5. Persistence, Replace and dormant states; the verify group complete.
6. Documentation: `doc/editor/level_tools.md` "Surface attachments" under
   "Connectors" beside "Attachments" and the MCP tool descriptions. Final
   gates:
   `editor_level_tests`, `level_tools_verify.py`, `curve_verify.py`,
   `ctest -L editor`.

## Open questions

- Q1 (S3.4) When the support changes size, does A keep its offset in meters
  from the support's origin (proposed), or its proportional place in the
  support's bounds (a picture a quarter along a wall stays a quarter
  along)?
- Q2 (S2.3) Does a piece snapped onto content that is no kit piece (an
  imported building) attach to it? Proposed: no; the follow engine can take
  it later (carry only, on node moves).
- Q3 (S2.3) Children of a locked kit layout node as supports: walls of a
  segments layout are common supports, but the layout regenerates them and
  their uids. Proposed: not supported in this plan; attaching to the layout
  node by element index is a follow-up.
- Q4 (S2.7) Is the Attach To Surface switch wanted, and is it on by default?
- Q5 (S3.5) A piece whose support no longer lies under it: keep it carried
  and attached (proposed), detach it, or snap it to whatever else is there?
- Q6 A floor is a support, so moving a floor layout node carries every
  piece that was snapped onto it, and each furniture piece placed in a room
  records the floor. Is that the wanted reading of the issue (proposed:
  yes)?
- Q7 (S2.4) A piece with a connector connection never records a surface
  attachment (proposed). The other reading - both records, the connector
  followed first - is not planned.
