# Level tools: requirements

Status: proposed

Requirements for reusable content creation tools that build 3D game worlds
by kit-bashing: a catalog of clean modular shapes, the tools that place and
join them, and the spatial queries that tell the author - a person in the UI
or an agent over MCP - what the level is like to walk through. This document
holds requirements only; the design is in
`doc/plans/level_tools_design.md` and the build steps are in
`doc/plans/level_tools_implementation.md`. It extends
`doc/editor/brushes.md`, `doc/editor/create.md` and
`doc/editor/navigation.md`.

Each requirement is marked `[must]` (part of the first accepted scope),
or `[should]` (wanted, may land after it). "Later" lists what is out of
scope for now.

## Rulings

The user's rulings (2026-11-13):

- U1: the module length and the wall height are tunable parameters, not
  constants of a kit (R2.1).
- U2: the first scope is a small starter set of shapes, chosen in R4.1.
- U3: a navigation mesh build needs explicit seeds, as landed (R9.9).
- U4: every piece is walkable input of the navigation build; a per-shape
  navigation role is later work (R9.10, "Later").
- U5: the first set of spatial tools is chosen in R10.
- U6: authoring is desktop only at first, and stays possible to add on
  the Quest later (R1.6).
- U7: user-defined parametric shapes are later work ("Later").
- U8: a level follows its kit: a changed kit shape reaches saved levels at
  load. Kit versioning is later work (R8.3, "Later").
- U9: a scale target is later work ("Later").
- U10: the kits ship a flat-color material palette; texture coordinates
  come after the first scope (R2.8, R2.9).
- U11: collision shapes are in the first scope (R2.11).
- U12: snap to surface and orient to surface are placement options and
  tools for placed pieces (R5.11).
- U13: dragging a parameter updates the viewport live (R3.4).
- U14: the rock has several forms, among them a convex hull of points
  with one or more bevels (R4.1).
- U15: every shape has a derive-from-seed mask over its parameters, with
  the derive ranges stated in the kit (R3.9).
- U16: snap to surface and orient to surface have controls in the
  viewport toolbar and in the Transform tool, and apply to the Transform
  tool's moves (R5.11).

The user's ruling (2026-11-14):

- U17: a run of pieces is a kit layout node that owns its children, with
  a lock that says who edits them (R5.12).
- U18: a kit layout along a path is one node: the layout node is the curve
  node (R5.12.8).
- U19: a kit layout node's values are edited in the Properties window; the
  Level Kit window holds the values a new kit layout node starts with
  (R5.12.9).
- U20: pieces that overlap are accepted in kit-bashing (walls of a run
  sharing a corner, a rock sunk into the ground): an overlap is not a
  defect of a placement tool, and the overlap report (R10.3) stays a read
  the author asks for.

The user's rulings (2026-11-15):

- U21: transformed pieces snap to connectors within a set distance and
  unsnap beyond it, without interrupting the drag (R5.13). In the user's
  words: "We want 'snap to connectors' setting, as float max distance, 0
  meaning disabled, when transformed node a connector closer than the value
  to connector from other node in scene below the distance setting, the
  transformed node shall snap (translate) so that connectors are co-located
  - drag shall not be interrupted, and if the unsnapped position moves
  further than distance setting, the transformed node shall unsnap."
- U22: a connector is a point and frame of its node and follows the node's
  full world transform, scale included; connectors have visual indicators
  (R5.3, R5.13). In the user's words: "Connectors should have visual
  indicators. When I scale wall node a bit, it no longer connects; the
  connectors should be transforms along with the node"
- U23 (2026-11-15), snap to surface. The surface options are called "snap
  to surface" (not "surface rest"). In the user's words, for a piece buried
  in a solid: "raycast to both up and down, and as future work custom
  raycast direction, snap to nearest"; for a piece that would climb a stack
  or a coursed wall in one snap: "Add max distance setting to surface rest,
  and lets call it "snap to surface" from now on".
- U24 (2026-11-15), replace. A replaced piece is snapped to the surface again
  when Snap To Surface is on, and keeps its transform otherwise. "Children
  of locked layout should not be selectable without unlocking the layout node
  - replace selected should skip and report." A selected group or kit layout
  node is left out of replace for now. On seeds: "Layout kit nodes shall have
  boolean property "Per Piece Seed"".
- U25 (2026-11-15), scatter. Each point takes an item by a seeded random pick
  with a weight per item. The minimum spacing is the gap between piece
  bounds, not between piece centers. A change of the spacing or the region
  decides every point again, and the Run row's "n of m pieces fit" is the
  only report of a short scatter (both as built).
- U26 (2026-11-15), connectors. In the Connect rows the active piece is the
  target and the other selected piece moves (as built). On where a floor
  slab's side connectors sit: "Add property/properties so connectors can be
  enabled/disabled (per node)".
- U27 (2026-11-15), snap to connectors. "Any pair when both connectors are
  enabled" snaps. An axis or plane drag snaps by the full offset by default,
  and a setting keeps the piece on the dragged axis or plane instead ("Snap
  setting, default as built today, also enable stay on axis via setting
  change"). A modifier key held during the drag suspends the snap. The snap
  starts only after the pointer has moved: a press without motion changes
  nothing. A locked translate channel the offset needs leaves the step
  unsnapped (as built). Rotate and scale drags snap too. Scripted drags
  (`drag_selection`, `transform_selection`) snap only when the call passes
  the option (as built).
- U28 (2026-11-15), kit layout visualization. Line, grid, ring and scatter
  layout nodes draw their shape; the scatter also shows its minimum spacing,
  and its region has viewport handles to resize it.
- U29 (2026-11-15), window defaults. The PVS, LOD and Light Mixer windows
  are hidden by default.
- U30 (2026-11-15), snap to surface as a search. In the user's words: "Snap
  should try to snap to nearest free space where the piece fits (at least
  along the ray direction), along the ray direction, up to max search
  distance. If not found, stay unmoved, so at distance 0 for curve kit
  layout, at the path." It settles: the maximum distance limits the whole
  snap, measured from the start pose's base, in either direction; a piece
  fits where the span from its base to its top along the direction is free
  of solids at its base points (not a volume test); with no place within
  the distance the piece stays unmoved (a kit layout element at its
  generated position, a path layout's on the path). The default maximum
  distance is 1 m (confirmed in U35).
- U31 (2026-11-15), free space without solids. In the user's words: "Solid /
  non-solid sounds like risky (potentially complex) for snapping. If the fix
  turns out to be expensive (now or later): Try to come up with simpler rules
  ignoring face sidedness and only use vertices as evidence for "free" space
  for now, and mark considering solidness future work." It settles: the hit
  points on the base-point lines are the evidence; a span is free when no hit
  lies strictly inside it, whatever the face's facing; a hit is a surface to
  rest on when its face's normal is against the snap direction. A piece
  inside a solid taller than itself sees no hit in its span and can rest on a
  surface inside the solid; considering solidness is future work.
- U32 (2026-11-15), per-piece seed. "Per Piece Seed" (U24) is the kit layout
  switch shown as "Random Seeds" under a new name; a replace carries each
  piece's seed unchanged, as built.
- U33 (2026-11-15), connector switches. A kit piece has both a switch for
  all of its connectors and a switch per connector; a connector is enabled
  when both are on, and all are on by default.
- U34 (2026-11-15), more shapes (plan steps 62, 63). New shapes may join
  the existing kits, and the five new kits of
  `doc/plans/level_kit_extra_shapes.md` are wanted as well. The beach
  shapes of step 62 are natural shapes; a harbour kit of man-made shapes is
  free for step 63. A shape can carry a light source. An industrial kit
  yes; a science fiction kit is future work. Shapes may use more
  triangles: the limit can be raised for all kits. Hanging vines and
  "path" are future work.
- U35 (2026-11-15), confirmations. Shift is the default key that suspends
  the connector snap. The default maximum snap to surface distance is 1 m.
  The scatter is good as built; further scatter improvements, a weight
  setting in the Level Kit window among them, are future work. Disabled
  connectors are not drawn, as built.
- U36 (2026-11-15), extra shapes. Water: yes, a simple plane shape. A light
  a shape carries is a light node, and its values are that node's own
  properties, not shape parameters. No limit on the number of kit lights
  for now ("we may end up baking lights to vertices").

## Terms

- Level: one scene made mostly of kit pieces, meant to be walked through.
- Kit: a named collection of shapes that fit together (R4).
- Shape: one entry of a kit. A basic shape is a fixed mesh; a parametric
  shape is a recipe with named parameters that makes a mesh (R3).
- Piece: one placement of a shape in a level: a node with a transform, a
  material assignment and, for a parametric shape, parameter values.
- Module: the level's unit of length. Sizes and snap positions of shapes
  are multiples or simple fractions of it (R2.1).
- Connector: a named frame on a shape where another shape attaches (R5.3).
- Agent: the walker the navigation mesh is built for: the height, radius,
  climb and slope values of `Navigation_settings` (`doc/editor/navigation.md`
  "Settings").
- Walkable area: the polygons of the scene's navigation mesh. Reachable
  area: the walkable polygons reachable from the seeds
  (`doc/editor/navigation.md` "Seeds").
- Island: one connected component of the walkable area.
- Author: whoever builds the level: the user through the editor UI, or an
  agent through the MCP server. Every requirement that says "the author"
  holds for both (R6).

## What the work builds on

The landed features the requirements below assume and extend. Each line
names the owning document; this section adds no behavior.

| Feature | State | Document |
|---|---|---|
| Brush: a reusable mesh template whose placements share geometry and GPU buffers | landed | `doc/editor/brushes.md` |
| Create tool and MCP `create_shape`: box, sphere, cone, capsule, torus, disc, polyhedra, convex hull | landed | `doc/editor/create.md` |
| MCP `place_brush`, `place_brush_instances`, `create_node`, `set_node_transform` | landed | `config/editor/mcp_tools.json` |
| Geometry graph: node-based procedural meshes, live on a scene node | landed | `doc/editor/geometry_nodes.md` |
| Prefab instances of glTF files | landed | `doc/editor/scene_serialization.md` |
| Grid with snapping | landed | `doc/editor/grid.md` |
| Navigation mesh: build job, seeds, reachable area, visualization, MCP `navigation_build`, `get_navigation_status`, `get_navigation_mesh`, `navigation_set_seed`, `navigation_clear`, `navigation_set_visualization` | landed, desktop | `doc/editor/navigation.md` |
| MCP `raycast`, `geometry_query` (batched raycast and closest point), `get_node_details` (`world_aabb`), `render_scene_image` (with the navigation mesh overlay) | landed | `config/editor/mcp_tools.json` |
| Agent-built scenes and their workflow | landed | `doc/agents/creations.md` |

## R1 Goal and scope

- R1.1 `[must]` An author can build a complete walkable level from kit
  shapes alone: ground, elevation changes, enclosed and open spaces, and
  dressing, with no mesh modeling step.
- R1.2 `[must]` The look is simple, low-polygon and stylized: flat or
  gently curved surfaces, readable silhouettes, plain materials. A shape's
  form carries its identity; textures are optional.
- R1.3 `[must]` The tools are reusable: kits are data the editor ships
  with, available in every scene, and independent of any one level.
- R1.4 `[must]` A level made with these tools is an ordinary erhe scene:
  it saves, loads, exports and renders through the existing scene paths
  (R8).
- R1.5 `[must]` The end of the authoring loop is a navigation mesh of the
  level (R9), and the author can read back what is walkable and reachable
  (R9, R10) to correct the level.
- R1.6 `[must]` Platforms. The authoring tools (R5, R6) and the walkable
  area tools (R9) run in the desktop editor. A saved level opens and runs
  wherever erhe scenes do, the Quest included. Shape generation and the
  spatial queries of R10 depend on nothing a Quest build lacks, so
  authoring can be enabled there later.

## R2 Shape conventions

The rules that make shapes "neat and clean" enough to combine without
adjustment. They hold for every shape of every kit.

- R2.1 `[must]` Module. A level has two tunable lengths in meters, stored
  with the scene: the module (default 1.0) and the wall height (default
  3.0). A shape states its default footprint and height in modules and in
  wall heights, and a newly placed piece takes its default parameter
  values from the level's two lengths at the moment it is placed. The
  snap grid of R5.2 follows the module. Changing either length leaves the
  existing pieces as they are, and one action applies the new lengths to
  selected pieces that still hold their defaults.
- R2.2 `[must]` Origin. A shape's origin sits at a stated, predictable
  place - the center of its base for a free-standing shape, a base corner
  or base edge center for a shape that tiles - so that pieces placed at
  module positions with rotations of 90 degrees meet exactly.
- R2.3 `[must]` Axes. Y is up; a shape with a front (stairs, an arch, a
  door wall) faces one stated axis, the same for every shape.
- R2.4 `[must]` Seams. Faces of two shapes that are meant to meet are
  coplanar and have equal outlines, so adjacent pieces show neither a gap
  nor an overlap along the seam.
- R2.5 `[must]` Mesh quality. A shape is a closed manifold unless it is
  stated to be a surface (a ground patch, a leaf card); it has consistent
  outward winding, no degenerate faces and no coincident duplicate faces.
- R2.6 `[must]` Polygon budget. Each shape states a triangle budget for
  its default parameters, and a kit states the ceiling for its shapes
  (every kit: 2000 triangles, binding at every in-range value).
  Curved shapes expose their segment counts as parameters.
- R2.7 `[must]` Normals. Each shape states its shading: faceted, smooth,
  or smooth with hard edges above a stated angle.
- R2.8 `[should]` Texture coordinates. Each shape has texture coordinates
  whose scale is stated in world meters per unit, so one tiling material
  has the same texel density on every shape of a kit.
- R2.9 `[must]` Material slots. A shape has one or a few named material
  slots (for example `wall`, `trim`, `floor`); a piece assigns a material
  per slot. A kit supplies a small palette of flat-color materials, one
  default per slot name plus a few alternatives per slot (stone, wood,
  plaster, foliage, bark, earth, grass), so a level reads clearly with no
  texture at all.
- R2.10 `[must]` Walkability by construction. A shape meant to be walked
  on - stairs, a slope, a floor, a bridge - is walkable for the default
  agent at its default parameters: step rise at most `agent_max_climb`,
  slope at most `agent_max_slope`, opening height and width at least
  `agent_height` and twice `agent_radius` plus a stated margin.
- R2.11 `[must]` Collision. Each shape supplies a collision shape for the
  physics backend - its own mesh, a convex hull or stated primitives -
  that follows the piece's parameter values, and a placed piece is a
  static body by default.

## R3 Basic and parametric shapes

- R3.1 `[must]` A basic shape is one mesh shared by all of its pieces. A
  piece differs from another piece of the same shape only by transform and
  material assignment, and adding a piece adds no geometry.
- R3.2 `[must]` A parametric shape has named, typed parameters with a
  default, a valid range and a unit each (meters, degrees, count). Its
  mesh is a function of the parameter values alone: equal values give an
  identical mesh.
- R3.3 `[must]` Pieces of one parametric shape with equal parameter values
  share one mesh, as in R3.1.
- R3.4 `[must]` A piece of a parametric shape keeps its parameter values:
  the author can read them and change them later, and the piece's mesh
  follows. While a value is dragged in the UI the viewport shows the mesh
  of the current value on every change; the whole drag is one undoable
  step.
- R3.5 `[must]` Parameters are stated in the terms of the result, not of
  the construction: stairs take total rise, width and a step rise limit,
  and report the step count and the tread depth they produce.
- R3.6 `[must]` A parametric shape rejects parameter values outside its
  valid ranges with a message that names the parameter and its range.
- R3.7 `[should]` A parametric shape with a seed parameter (a rock, a
  tree) gives the same mesh for the same seed on every platform.
- R3.9 `[must]` Derive from seed. Every shape has a seed parameter, and
  a kit states for each other parameter whether it can be derived from
  the seed and the range it is derived within (inside the parameter's
  valid range). A piece carries a mask: the set of its parameters that
  are derived. A derived parameter's value is a function of the seed, the
  parameter and its derive range alone; it shows read-only, and changing
  the seed changes every derived value. A shape states its default mask.
  Placing in the viewport has a setting that gives each new piece a new
  seed, so one shape with a mask yields varied pieces.
- R3.8 `[should]` A piece can be made unique: its mesh becomes an ordinary
  editable mesh that the mesh modeling tools change, and it stops
  following its shape.
## R4 Kits

The shapes, by kit. "P" marks a parametric shape. R4.1 is the first scope;
the shapes of R4.2 - R4.4 follow once a level can be built and walked with
the starter set.

- R4.1 `[must]` Starter set, 14 shapes. Each one that has a size takes it
  from the module and the wall height by default (R2.1).
  - Basic kit: block (P: size), wedge / slope (P: run, rise, width),
    cylinder (P: radius, height, segments), straight stairs (P: rise, width,
    step rise limit), floor slab (P: size, thickness).
  - Architecture kit: wall (P: length, height, thickness), wall with door
    opening (P: plus opening width and height), pillar (P: round or square,
    width, height), arch (P: span, rise, depth, thickness).
  - Nature kit: rock (P: form, size, roughness, bevels, seed; the forms
    are a jittered blob, an angular convex hull of few points, a worn
    convex hull of many points, and a flat slab-like hull, the hull forms
    with zero, one or two bevel passes over their edges), land mass blob (P:
    footprint, height, roundness, seed) with a flat top, hill / mound (P:
    radius, height, seed), tree (P: trunk height, canopy size, round or
    conical canopy, seed), bush (P: size, seed).
- R4.2 `[should]` Basic kit, further shapes: corner slope inner and outer,
  half cylinder, cone, pyramid, sphere and dome, quarter-round, ramp with
  landing.
- R4.3 `[should]` Architecture kit, further shapes: wall with window
  opening (P), wall corner and T-junction, roof (P: gable, hip, shed),
  stairs with side walls (P), L and U stairs with landing (P), spiral
  stairs (P), railing and fence run (P: length, post spacing), bridge deck
  (P), platform, door frame, battlement run (P).
- R4.4 `[should]` Nature kit, further shapes: boulder cluster (P), cliff
  segment (P), ground patch (P), blocky tree family, grass tuft, log and
  stump, stepping stone.
- R4.5 `[must]` A kit is a folder of the content library with its shapes,
  its default materials and its polygon ceiling (R2.6). The author browses
  a kit with a thumbnail per shape.
- R4.6 `[must]` Each shape carries a short description and tags (for
  example `walkable`, `obstacle`, `opening`, `decoration`, its size in
  modules, its connector names) that the catalog query of R6.3 returns, so
  an agent can choose shapes without seeing them.
- R4.7 `[should]` Adding a shape to a kit, or a kit, is a matter of adding
  data in one place; the UI and the MCP tools pick it up without further
  change.
- R4.8 `[should]` Kits of other styles can exist beside the first three.

## R5 Kit-bashing tools

- R5.1 `[must]` Place. The author places a piece of a chosen shape at a
  position and rotation, with parameter values and materials, as one
  undoable step.
- R5.2 `[must]` Snap. Placement and later moves snap to the module grid
  and to rotation steps (90 degrees by default, a stated finer step on
  request).
- R5.3 `[should]` Connectors. A shape can name connectors (top and bottom
  of stairs, both ends of a wall, the opening of a door wall). The author
  places a piece by naming a connector of the new piece and a connector of
  an existing piece; the tool computes the transform that joins them.
- R5.13 `[should]` Snap to connectors. A setting, a maximum distance in
  meters, 0 disabled. While a node is transformed, when one of its
  connectors comes closer than the distance to a connector of another node
  of the scene, the node snaps by translation so the two connectors are
  co-located; the drag goes on. The snap is judged from the unsnapped drag
  pose: when that pose's connector is farther than the distance from the
  other connector, the node unsnaps (U21).
- R5.4 `[must]` Repeat. The author places a run of pieces in one step: a
  line (count and spacing), a grid, a ring, or pieces along a polyline (a
  wall run that turns corners, a fence, a path of stepping stones).
- R5.5 `[should]` Scatter. The author scatters pieces of chosen shapes
  over a region or over a surface with a density, a random seed, random
  rotation and scale ranges and a minimum spacing (nature dressing).
- R5.6 `[must]` Group. Pieces can be grouped under one node, and a group
  can be duplicated, moved, mirrored and deleted as a unit.
- R5.7 `[should]` Reuse of assemblies. A group can be saved as a reusable
  assembly (a tower, a house shell) and placed again, in the same level
  and in other levels.
- R5.8 `[should]` Replace. The author swaps the shape of selected pieces
  for another shape, keeping transforms and matching material slots.
- R5.9 `[should]` Join. Selected pieces can be combined into one mesh by
  union, and one piece can cut a hole in another (a door through a rock),
  using the existing CSG and merge operations.
- R5.11 `[must]` Surface placement. Two independent options: snap to
  surface (the piece's base comes to rest on the surface found from the
  placement point along a stated direction, down by default) and orient to
  surface (the piece's up axis takes the normal of that surface and the
  piece keeps its heading; off, the piece stays upright). Both are
  placement settings - the UI remembers them between placements, an MCP
  call states them - and both are tools that act on already placed pieces
  (R10.5). A placement that finds no surface reports it and places the
  piece at the given point. The two options are one pair of settings
  shown in the Level Kit window, in the viewport toolbar and in the
  Transform tool's settings, and they also apply to the Transform tool: a
  move of the selection with them on rests each moved mesh on the surface
  under it, and tilts it with orient on, at every step of the drag.
- R5.12 `[must]` Kit layout node. A run of pieces (R5.4, and the scatter
  of R5.5) is a node in the scene graph, the kit layout node, with the
  pieces as its children.
  - R5.12.1 Its properties state the arrangement (line, grid, ring,
    polyline; scatter when R5.5 is built) with that arrangement's values,
    which kit item or items are placed (shape, parameter values and slot
    materials each), the snap settings used (module, snap to surface,
    orient to surface), whether each piece gets its own random seed, and
    the seed mask (which parameters derive from the seed, R3.9).
  - R5.12.7 A line, a ring and a polyline each have two count modes: the
    author states the piece count, or the author states the gap between
    neighboring pieces and the layout determines the count from the space
    of the arrangement (the line's length, the ring's arc, the path's
    length).
  - R5.12.8 A kit layout along a path is one node: a curve node
    (`doc/plans/curves.md`) that carries the kit layout values, with the
    pieces as its children, so the author selects one node to edit the
    path's points and the layout's values. A polyline is a curve of corner
    points.
  - R5.12.9 The values of a kit layout node (R5.12.1, the lock included)
    are edited in the Properties window with the node selected. The Level
    Kit window keeps a set of the same values as well: they are the
    initial values of a kit layout node the author adds to the scene, and
    editing them changes no node.
  - R5.12.2 The kit layout node owns its children and has a lock setting.
  - R5.12.3 Locked: the kit layout node creates, removes and adjusts its
    children as its properties need, and the children cannot be edited
    directly.
  - R5.12.4 Unlocked: the kit layout node leaves its children alone, and
    the children are edited directly as any piece is.
  - R5.12.5 Unlocking changes no child. Locking discards the edits made
    while unlocked: the children become what the properties state.
  - R5.12.6 Locking and unlocking are each one undoable operation; undoing
    a lock restores the children as the user left them.
- R5.10 `[must]` Every tool of this section is one undoable operation and
  leaves the scene valid when undone or redone.

## R6 Exposure to the user and to MCP

- R6.1 `[must]` Parity. Every action and query of R3 - R5, R9 and R10 is
  available both in the editor UI and as an MCP tool, and the two run the
  same code path and produce the same result.
- R6.2 `[must]` UI. A kit browser with thumbnails, a parameter panel for
  the shape being placed and for a selected piece (with a live preview
  while a value is dragged), and the placement and repeat tools reachable
  from it.
- R6.3 `[must]` MCP catalog. One query lists the kits and, per shape: its
  name, kind (basic or parametric), description, tags, parameters with
  type, default, range and unit, default size, connectors and material
  slots.
- R6.4 `[must]` MCP placement. Tools for R5 take shape names and parameter
  values, reject unknown keys, and return the created nodes with their ids
  and world bounds.
- R6.5 `[must]` MCP batch. Placing many pieces takes one call, and a
  failure reports which entry failed and what was applied.
- R6.6 `[must]` MCP tools follow `doc/agents/mcp_api_guidelines.md`.
- R6.7 `[should]` The agent workflow for building a level - choose a kit,
  block out, check walkability, dress - is written down as a skill or a
  `doc/agents/` document with a worked example script.

## R7 Performance

- R7.1 `[must]` Changing a parameter of a parametric piece regenerates its
  mesh off the main thread when it takes longer than a frame, with the
  piece showing its previous mesh until then.
- R7.2 `[should]` A static level exports through the existing world-space
  bake and prepared asset paths with no special handling of kit pieces.

## R8 Persistence

- R8.1 `[must]` A saved level reloads with every piece still tied to its
  shape: shared meshes stay shared (R3.1, R3.3) and parametric pieces keep
  their editable parameter values (R3.4).
- R8.2 `[must]` A saved level is a valid glTF file that a viewer without
  erhe extensions shows with the correct geometry.
- R8.3 `[must]` A level follows its kit. A piece's mesh and collision
  shape are made from the kit's current shape definition and the piece's
  parameter values when the level loads, so a changed shape definition
  reaches every saved level at its next load. A piece made unique (R3.8)
  keeps its own mesh.
- R8.4 `[must]` A piece whose shape the kit no longer has, or whose saved
  parameter values the shape now rejects (R3.6), loads with the mesh the
  file holds (R8.2), and the load reports each such piece by name.

## R9 Walkable area: update and read

The landed tools (`navigation_build`, `get_navigation_status`,
`get_navigation_mesh`; "What the work builds on") already update the
navigation mesh and return its polygons with their reachable flags. The
requirements here are what a level author additionally needs from them.

- R9.1 `[must]` Update. The author starts a navigation mesh build of the
  level and learns when it is done and whether it succeeded, from the UI
  and over MCP.
- R9.2 `[must]` Staleness. After any edit that changes walkable geometry,
  the status reports the navigation mesh as stale until it is rebuilt.
- R9.3 `[must]` Summary read. One query returns a compact description of
  the walkable area sized for an agent's context: total and reachable
  area, the islands (R9.4), and per failed seed the reason. The polygon
  list stays available as the detailed read.
- R9.4 `[must]` Islands. The query of R9.3 lists each island with an id,
  its area, its bounds, its height range, whether it is reachable, and a
  representative point on it.
- R9.5 `[must]` Point query. Given a position, the tool returns the
  nearest walkable point within a stated search extent, its island and
  whether it is reachable; given a node, the same for the node's position.
- R9.6 `[must]` Path query. Given two positions, the tool returns whether
  a walkable path joins them and, when it does, the path's length and its
  corner points.
- R9.7 `[should]` Why not. Given a position that is not walkable, or two
  positions without a path, the tool states the nearest cause it can
  determine: slope steeper than the limit, step higher than the climb
  limit, clearance below the agent height, passage narrower than the agent,
  no surface, or a gap of a stated width.
- R9.8 `[should]` Picture. The author gets a top-down orthographic image
  of the level with the walkable area drawn over it, islands in distinct
  colors and seeds marked, with the image's world extents returned so that
  pixels map to positions.
- R9.9 `[must]` Seeds. A build needs at least one seed
  (`doc/editor/navigation.md` "Job"). The author adds a seed at a stated
  position in one step, in the UI and over MCP, and the islands of R9.4
  and the queries of R9.5 and R9.6 cover the whole walkable area, the
  unreachable part included.
- R9.10 `[must]` Input. Every piece is input of the build as the static
  mesh it is, so every upward surface with room above it becomes walkable
  area - a rock top and a tree canopy included. Such surfaces show up as
  unreachable islands; the summary of R9.3 lists them and the level check
  of R10.7 reports only the unreachable islands larger than a stated
  area.
- R9.11 `[should]` Incremental update. After a local edit the rebuild
  costs time proportional to the edited region, so the check in the
  authoring loop stays within seconds on a large level.
## R10 Spatial understanding tools

Queries that let an author who cannot simply look - an agent, or a user
checking a large level - verify the level's shape. The landed base is
`raycast`, `geometry_query`, `get_node_details` and `render_scene_image`.
R10.2 - R10.7 are the first set: the reads and the one action an author
needs between placing pieces and building the navigation mesh.

- R10.1 `[must]` Every query of this section and of R9 takes and returns
  positions in one stated space (the scene root's space, in meters), and
  names nodes by id.
- R10.2 `[must]` Region contents. Given a box or a sphere, list the pieces
  whose bounds intersect it, with shape, bounds and transform. ("What is
  here?")
- R10.3 `[must]` Overlap report. List pairs of pieces that interpenetrate
  by more than a tolerance, with the penetration depth; the caller can
  leave out pieces by tag (a rock sunk into ground).
- R10.4 `[must]` Support report. List pieces that rest on nothing: no
  surface within a tolerance under their base.
- R10.5 `[must]` Snap to surface. Given pieces, move each down (or along
  a stated direction) until it rests on the surface below, with the orient
  option of R5.11, as one undoable step; the query form returns the
  resting transforms without moving anything.
- R10.6 `[must]` Height map. For a rectangular region and a cell size, a
  grid of top surface heights, and with a navigation mesh also of walkable
  flags and island ids, returned as numbers and optionally as a text
  picture a language model can read directly.
- R10.7 `[must]` Level check. One call that runs R10.3 and R10.4, the
  unreachable islands of R9.10 and the walkable shapes whose parameter
  values break R2.10, and returns one list of findings, each with a
  position and the pieces involved.
- R10.8 `[should]` Scene outline. A compact spatial summary of the level:
  overall bounds, piece counts per shape, and the groups with their
  bounds - the first read of an unfamiliar level.
- R10.9 `[should]` Gap report. List seams between adjacent pieces that are
  open by more than a tolerance and less than a stated maximum - the near
  misses that break walkability or show daylight. It joins the level
  check.
- R10.10 `[should]` Clearance probe. Given a position, the free distance
  up, down and in the horizontal directions, and whether an agent-sized
  capsule fits there.
- R10.11 `[should]` Measure. Distance, height difference and horizontal
  offset between two positions, two pieces or two connectors, in meters
  and in modules.
- R10.12 `[should]` Section image. An orthographic image of a vertical or
  horizontal slice through the level at a stated plane, with world
  extents, for checking floor heights, ceilings and stair runs.

## R11 Verification

- R11.1 `[must]` Each shape has an automated check of R2.4 - R2.6 at its
  default parameters and at the ends of its parameter ranges.
- R11.2 `[must]` Each walkable shape has an automated check of R2.10: a
  navigation mesh built over the shape alone on a floor joins its bottom
  to its top.
- R11.3 `[must]` A scripted reference level, built over MCP from the
  starter set (R4.1), exercises the tools of R5, saves and reloads (R8),
  builds its navigation mesh and passes stated walkability expectations
  (which islands exist, which points are joined by a path). It runs
  headless.
- R11.4 `[should]` The reference level's images are kept as documentation
  of the kits.

## Later

Out of scope for now; each needs its own ruling before it becomes a
requirement.

- User-defined parametric shapes: a user defines a new parametric shape
  without writing C++, for example as a geometry graph with exposed
  parameters (U7).
- Kit versioning: a level records the kit version it was made with and
  the author chooses when it moves to a newer one (U8).
- Scale target: the piece count and level size the tools must stay
  interactive at (U9).
- Authoring on the Quest (U6), which needs the navigation mesh there too
  (`doc/plans/navigation_mesh.md` "Later").

- Navigation role: a shape states how its pieces enter the navigation
  build - walkable surface, obstacle only (a tree canopy, a bush, a roof)
  or ignored (small decoration) - with a per-piece override (U4;
  `doc/plans/navigation_mesh.md` "Later").
- A navigation build without seeds, for walkability reads during early
  block-out (U3).
- Links: connections the navigation mesh cannot find on its own - a
  ladder, a jump down, a door - that the reachable area crosses.
- Area kinds: walkable area carries a kind (ground, stairs, water, hazard)
  taken from the shape.
- Line of sight: whether two positions see each other, and the pieces
  visible from a position.
- Labeled image: a rendered image in which each visible piece carries its
  node id, with the pieces' screen rectangles.
- Walk test: move an agent along the navigation mesh from a start through
  waypoints and return where it ends up and what stopped it; a series of
  eye-height images along a path.
