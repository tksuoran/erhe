# Foliage paint tool: requirements

Status: in progress

Requirements for GitHub issue 302, "Foliage paint tool": a viewport tool
that paints kit pieces - grass, plants, bushes, trees, stones - onto the
surfaces under a spherical brush and removes them again, as single pieces
placed directly in the scene. It is a tool of its own beside the scatter
kit layout (`doc/editor/level_tools.md` "Scatter layouts"): a scatter is one
node that generates its children from a seed and a region, the foliage
paint tool leaves no node behind but the pieces themselves.

This document states the rulings, the terms and the requirements
(F-numbers). The build is in progress
(`doc/plans/foliage_paint_tool_implementation.md` "Built"). A statement marked "proposed" is the
planner's choice where neither the issue nor a ruling settles the point;
"For the user to review" lists them. The design is
`doc/plans/foliage_paint_tool_design.md` and the build order
`doc/plans/foliage_paint_tool_implementation.md`. The document extends
`doc/editor/level_tools.md` "Surface placement", "Surface attachments",
"Scatter layouts" and "Placing".

## The issue

The statements of the issue, each with the requirements that carry it:

1. Foliage shape nodes / meshes get an item flag that says they are
   foliage (F1).
2. Items with the flag are invisible to placement rays, so foliage can be
   placed overlapping and close together (F1.3).
3. The tool has no parent layout node: foliage is placed directly in the
   scene, parented to the shape or mesh it is attached to (F7).
4. The scene maintains helper data structures that give the local foliage
   density, a kd-tree or similar over the foliage items (F8).
5. The tool has a palette: a set of foliage shapes with placement weights
   (F3).
6. Each foliage kit shape type gains a radius, the minimum distance to a
   neighbour foliage item (F2).
7. The brush is a sphere of configurable radius, set with the mouse wheel
   (F4).
8. Left click and drag increases the density in the brush by placing more
   palette shapes pseudorandomly inside it (F5).
9. New foliage may overlap existing foliage, and the overlap is bounded by
   the minimum placement point distance: a point too close to an existing
   foliage item is rejected and rolled again, and of all placed shapes none
   is closer to another than the radius of each (F6).
10. Right click and drag removes placed foliage (F9).

## Rulings

User, 2026-11-21:

- G1 The flag is carried by the items the tool places and by whatever the
  user flags by hand (F1.2).
- G2 The radius has a default on the shape, a value on the palette entry
  and the placed value on the item (F2).
- G3 A foliage item is navigation input and a body as its shape says
  (F1.5).
- G4 A held brush adds more by brush travel only (F5.1).
- G5 Foliage painted on a kit piece carries a surface attachment record
  (F7.4).
- G6 The palette is kept in the editor settings (F3.4).
- G7 Placement variation beyond yaw is future work ("Future work").
- G8 The Brush tool and viewport Create / Paste pass through foliage
  (F1.3).
- G9 Painting and removing each have a density of their own: the paint
  density in a metric of the planner's choice (F5.2), the remove density a
  share of the items in the brush, 1.0 removing all of them (F9.2).
- G10 Shift and the wheel set the brush radius (F4.2).
- G11 Foliage shapes are vegetation only; stones are not foliage (F2.1).

User, 2026-11-21, during the build:

- G12 1,000 items is the working size; a larger load is lowered when it
  runs into trouble (F12).

## Where the code stands

- `Editor_item_flags::pass_through` (`editor_item_bits.hpp`) is the one
  authored flag that hides a mesh from surface rays: `is_surface_mesh()`
  (`level/level_queries.cpp`) is the test every surface ray and search asks,
  and navigation input skips the mesh too (`scene/scene_navigation.cpp`).
  `Level_kits::place()` sets it on a piece of a bodiless shape
  (`Kit_collision::none`: water, leaf plant, grass tuft and the like).
- The pointer placement of the Brush tool's kit mode
  (`make_placement_pose()`) and of viewport Create / Paste
  (`Hover_placement`, `scene/hover_placement.*`) starts from the hovered
  content, which today includes every visible mesh.
- A scatter picks its items by integer weight (`pick_scatter_item()`,
  `Kit_layout_item.weight`, 0 - 1000) and spaces them by a footprint circle
  from the item's bounds plus `min_spacing`, in the layout node's xz plane,
  from one seeded stream (`Kit_random`). Its spacing grid lives for one
  generation; the scene keeps no spatial index of pieces.
- A surface attachment (GitHub issue 304) makes a piece the scene graph
  child of the kit piece it rests on, with a record of its place there, and
  carries it when the support changes shape. Content that is no kit piece
  is no support.
- `Level_kits::place()` inserts any number of pieces as one undo entry;
  pieces of one mesh key share the brush's geometry, GPU buffers and
  collision shape.
- The right button in a viewport: a command that consumes the right press
  comes before the viewport context menu and the right drags (the Brush
  tool's insert, the material paint tool's pick while their tool is active;
  `doc/editor/windows.md` "Viewport context menu").
- A brush radius on the mouse wheel exists in the mesh component selection
  tool's paint select, which takes the wheel from the camera zoom while its
  gesture is ready.

## Terms

- Foliage item: a scene item that carries the foliage flag (F1).
- Foliage shape: a kit shape that states a foliage radius (F2).
- Placement point: the world position of a foliage item's origin, on the
  surface it was placed on.
- Radius: the foliage item's minimum distance from its placement point to
  every other foliage item's placement point, in meters.
- Palette: the tool's list of entries, each a kit shape with its values, a
  weight and a radius (F3).
- Brush: the sphere the tool acts in, centred on the surface point under
  the pointer (F4).
- Stamp: one application of the brush at one center - a number of placement
  attempts, or one removal - during a stroke.
- Stroke: the stamps from a button press to its release; one undo entry.
- Support: the mesh whose face holds a placement point.
- Foliage index: the per-scene spatial structure over the foliage items
  (F8).

## Requirements

### F1 Foliage flag

- F1.1 `Editor_item_flags::foliage` is an authored item flag with the label
  "Foliage" and the saved name `foliage`, registered with the other editor
  flags and saved, loaded, copied and shown in the Properties window as
  `pass_through` is.
- F1.2 The tool sets the flag on every item it places. The flag is editable
  by hand on any mesh node, so imported content can be made foliage; an
  item made foliage by hand has radius 0 until it is given one (F2.3).
- F1.3 A foliage item is invisible to placement rays. The closed list of
  those rays:
  - every surface ray and search of `level_queries` (`is_surface_mesh()`):
    snap to surface, orient, the support report, the height map, the carry
    of a surface attachment;
  - the pointer ray that gives a placing tool its surface point: the
    foliage paint tool's brush center, the Brush tool's placement, the
    viewport context menu's Create / Paste placement (G8): a crate placed
    on painted grass lands on the ground.
- F1.4 Every other ray sees a foliage item as it sees any mesh: viewport
  hover and selection, the Transform tool, physics queries, picking for
  material paint.
- F1.5 The foliage flag and `pass_through` are independent. A foliage item
  of a shape with a body (a tree with a trunk hull) keeps its body and is
  navigation input; a foliage item of a bodiless shape carries both flags
  (G3).
- F1.6 Level Check leaves foliage items out of its `unsupported` and
  `overlap` findings: their overlap is intended and their support is the
  parent they were painted on (proposed).

### F2 Foliage shapes and radius

- F2.1 `Kit_shape_definition` gains a foliage radius in meters. A shape
  that states one is a foliage shape and is offered when the user adds a
  palette entry. The foliage shapes are the vegetation shapes of the
  nature kit (grass tuft, leaf plant, succulent, bush, palm shrub, bamboo,
  tree, blocky tree, palm, palm clump) (G11); the default radius of each
  is a table of the design, for the user to review.
- F2.2 A shape's radius is its default: a palette entry starts from it and
  holds its own value (F3.1).
- F2.3 A placed foliage item records its radius on itself
  (`Kit_piece.foliage_radius`, a hexadecimal-exact property outside the
  mesh key, so pieces of one shape and values share one brush whatever
  their radii) (G2). The spacing rule reads the radius of each
  item from the item, never from the palette, so a later palette edit
  leaves placed foliage as it is.
- F2.4 The radius is edited per item in the Properties window's Kit Piece
  group and over MCP; an edit updates the foliage index (F8.3) and moves
  nothing.

### F3 Palette

- F3.1 A palette entry holds: the shape id, its parameter values, a
  material per slot or the slot defaults, an integer weight in 0 - 1000
  (default 1; the scatter's weight and its pick, `pick_scatter_item()`), the
  radius (default the shape's), a yaw range in degrees (default 0 - 360)
  and Variants (1 - 64; default 8 for a shape that derives values from a
  seed, so two bushes differ, and 1 otherwise): each placed item takes one
  of that many seeded forms of the entry, so an entry needs at most that
  many meshes.
- F3.2 The palette is edited in the tool's window: add an entry from the
  foliage shapes, remove, reorder, edit each value; an entry shows the
  shape's thumbnail.
- F3.3 A stamp places from the entries of weight above 0. A palette without
  such an entry disables painting, and the window says why.
- F3.4 The palette is part of the editor's settings, saved with them and
  shared by every scene (G6).

### F4 Brush

- F4.1 While the tool is active and the pointer is over a viewport, the
  brush center is the nearest hit of the pointer ray on a surface mesh
  (F1.3: foliage is passed through). With no hit the tool has no brush and
  a stamp does nothing.
- F4.2 The brush is a sphere about that center. Its radius is a tool value
  in meters (proposed: default 2, range 0.1 - 50), edited in the tool's
  window and with a modifier and the mouse wheel while the tool is active
  and the pointer is over a viewport (G10: Shift, an
  editable binding of the Input Bindings window): one wheel step
  multiplies the radius by a fixed factor (proposed 1.1), so the step is
  proportional at every size. The plain wheel stays the camera zoom.
- F4.3 The brush is drawn in the viewport: the circle where the sphere
  meets the surface plane at the center, with the surface normal, drawn
  through the debug line renderer. It is redrawn from the hover and the
  radius it shows; it is not a scene item.
- F4.4 The tool is a `Tool` of the editor with the priority and the hotbar
  entry of the other placing tools; activating it leaves the selection as
  it is.

### F5 Painting

- F5.1 A left press on a brush begins a paint stroke and stamps once; while
  the button is held the tool stamps again each time the brush center has
  moved by half the brush radius from the last stamp's center (G4: brush
  travel alone adds stamps, so a stroke is the same on every machine and
  frame rate; the half radius is proposed). The release ends the stroke.
- F5.2 A paint stamp makes a number of attempts: the tool value Paint
  Density, in items per square meter of the brush circle that one stamp
  tries to add (G9; default 0.5, range 0.01 - 100), times the circle's
  area, rounded to the nearest count, at least 1 and at most 1024. Where
  the spacing rule lets every attempt in, one stamp raises the local
  density by the Paint Density; stamps overlap by F5.1, so a stroke adds
  about twice that along its path, and painting an area again adds more
  until the spacing saturates (F5.4).
- F5.3 An attempt picks a palette entry by weight, then a candidate point:
  a pseudorandom point of the brush circle (the disc through the center
  across the hovered normal) is projected along the normal onto the
  surface meshes, and the nearest surface face inside the sphere, facing
  the brush side, holds the placement point. A candidate with no such face
  is rolled again.
- F5.4 A candidate that fails the spacing rule (F6) is rejected and rolled
  again with the same entry. An attempt ends with its first accepted point
  or after 8 candidates (proposed), placing nothing then; a saturated area
  takes no more foliage however long it is painted.
- F5.5 The placed item stands at the placement point with its base on the
  surface (a kit shape's origin is at its base), upright along the world up
  axis, or turned onto the surface normal with the tool value Align To
  Surface (off by default), and turned about its up axis by a pseudorandom
  yaw of the entry's range.
- F5.6 All draws of a stroke come from one stream, `Kit_random` of the
  tool's Seed value, in a fixed order: per paint attempt the entry, then
  per candidate the point, then the yaw and the variant of the accepted
  one; per remove stamp one draw per item in the brush (F9.2). The same
  seed gives the same candidates on every platform, and the same stroke on
  the same scene places the same foliage in one build. The tool's window
  shows the seed, which advances after every stroke; the MCP stroke takes
  it as an argument.
- F5.7 Placed items appear while the stroke runs, each stamp on the frame
  it happens.

### F6 Spacing

- F6.1 A candidate point p of an entry of radius r is accepted when, for
  every foliage item q of the scene with placement point x_q and radius
  r_q, the distance |p - x_q| is at least max(r, r_q): no item lies inside
  the new item's radius and the new item lies inside no item's radius.
- F6.2 The distance is the straight distance in three dimensions in the
  scene root's space, in meters; node scale does not change a radius.
- F6.3 The rule counts every foliage item of the scene: of any shape, in
  the palette or not, placed by an earlier stroke, by an earlier stamp or
  attempt of this stroke, or made foliage by hand.
- F6.4 The neighbours are read from the foliage index (F8) with one query
  of the range max(r, the largest radius in the index).
- F6.5 Nothing else limits overlap: meshes of accepted items may intersect
  each other and any other content.

### F7 Parenting and following

- F7.1 A placed item is a child of the support's node, inserted with the
  world transform of F5.5. The tool makes no other node: no layout node, no
  group.
- F7.2 A support is any content mesh that is a surface mesh: a kit piece, a
  kit layout's generated mesh, a child of a locked kit layout node, or
  content that is no kit piece (an imported mesh, a brush instance).
- F7.3 The item follows its support through the scene graph when the
  support moves, turns or scales.
- F7.4 On a support that is a kit piece or a kit layout's generated mesh
  the item also gets a surface attachment record
  (`doc/editor/level_tools.md` "Surface attachments"), whatever the Attach
  To Surface setting says, so a shape change of the support carries it and
  a regeneration of the layout gives it its new home (G5).
- F7.5 Deleting a support deletes the foliage on it, as it deletes every
  child; the undo restores both.

### F8 Foliage index

- F8.1 Each scene (`Scene_root`) owns one foliage index over the items
  carrying the foliage flag that are in the scene: per item its placement
  point in the scene root's space and its radius.
- F8.2 Queries: the items within a distance of a point (into a
  caller-owned buffer that keeps its capacity), their count, and the
  largest radius held. The local density the issue asks for is the count
  within a sphere over the area of its great circle, in items per square
  meter.
- F8.3 The index is updated by the change, at the change: an item's insert
  into and removal from the scene (undo and redo included), a world
  transform change of the item or of an ancestor, a foliage flag edit, a
  radius edit, a scene load. No frame rebuilds or compares it, and a frame
  without such a change does no index work and no allocation.
- F8.4 The structure is a kd-tree or a uniform hash grid, chosen in the
  design; a query costs time in the number of items near the query, not in
  the number of items in the scene.
- F8.5 The index is derived state: it is not saved, and a loaded scene's
  index is built from its items.

### F9 Removing

- F9.1 A right press on a brush begins a remove stroke; it stamps as F5.1
  does. While the tool is active its command consumes the right press, so
  the viewport context menu and the right drags of the camera wait for
  another tool.
- F9.2 The tool value Remove Density is a share in 0 - 1, default 1.0
  (G9). A remove stamp takes the foliage items whose placement point lies
  inside the brush sphere (F8.2 query) in the order of their placement points and
  makes one draw per item: the item is removed, with its children, when
  the draw as a fraction of its range is below the Remove Density. At 1.0
  every draw passes and the stamp removes everything in the brush; at 0.25
  one stamp removes about a quarter and each further stamp a quarter of
  what is left, so removal thins as painting thickens.
- F9.3 The tool value Remove Only Palette Shapes (off by default) keeps a
  remove stamp to the items whose shape is in the palette (proposed).
- F9.4 Only items carrying the foliage flag are removed; the tool removes
  nothing else.

### F10 Undo, cancel, persistence

- F10.1 A stroke is one undo entry: a paint stroke the inserts of all its
  items (and the palette materials the scene lacked), a remove stroke all
  its removals. A stroke that changed nothing leaves no entry.
- F10.2 Escape during a stroke cancels it: the scene is as before the
  press and no entry is recorded.
- F10.3 A foliage item is an ordinary kit piece: it is saved and loaded
  with the scene with its flag, its radius and its parent, and it is
  selected, moved, replaced, duplicated, copied and deleted like any piece.
  A copy keeps the flag and the radius.

### F11 Settings, MCP, verification

- F11.1 The tool values (brush radius, Paint Density, Remove Density, Align
  To Surface, Remove Only Palette Shapes, seed) and the palette are a versioned editor settings
  config, like `Surface_snap_config`.
- F11.2 MCP tools take every input as an argument and read no widget
  state: a paint stamp or stroke (scene, brush centers and normals or
  pointer rays, radius, density, seed, the palette entries), a remove stamp
  or stroke, a read of the foliage items and the density in a sphere, and
  the read and write of the tool's settings and palette.
- F11.3 The scene-free parts - the index, the candidate sampler, the
  spacing rule - are built by `editor_level_tests` without the editor and
  tested there: the index against a brute-force search over pseudorandom
  item sets through inserts, moves and removals; the spacing rule over
  strokes (every pair of items at least the larger of the two radii
  apart); the same seed giving the same points.
- F11.4 A verify script group (`scripts/level_tools_verify.py
  --only-foliage-paint`) drives the headless editor: paint on a floor, a
  slope and a wall; paint again over painted ground (more items, the
  spacing held, items passed through by the rays); saturation; a palette
  of weights 1, 3 and 0; removal; undo and redo of both strokes; save and
  load; the support moved and its shape changed; the wheel and both
  buttons through the UI driving tools.

### F12 Scale

- F12.1 Working size: 1,000 foliage items in one scene (G12). At that
  size painting, removing, moving a support and opening the scene stay
  usable in a Release build, and a frame without a foliage change spends
  no time on foliage beyond drawing it. The index itself is not the
  limit: a stamp of 64 attempts spends at most 2 ms in the index queries
  and the spacing tests over 10,000 entries in a Release build.
- F12.2 Where a larger scene runs into a cost, the load is lowered and
  the size at which it began is recorded with its cause
  (`doc/editor/level_tools.md` "Foliage paint", Scale); drawing foliage as
  instances of one mesh is future work outside this document.

## Future work

- Placement variation beyond yaw (G7): a scale range per palette entry
  (and whether scale scales the radius), a slope limit per entry, a height
  offset.
- Drawing foliage as instances of one mesh (F12.2).

## For the user to rule on

Behaviors the build chose where the requirements leave room:

- F7.5 A kit layout regeneration that removes an element (fewer walls on
  a segments layout) detaches the foliage on it to its home with its world
  transform kept, as every surface-attached piece is; the tufts stay where
  they were, without their support. The other reading: foliage leaves with
  the element it stood on.
- F9.1 With the tool active a plain right press is consumed also where
  there is no brush (over the sky or the grid): no context menu and no
  camera drag until another tool is active or a modifier is held.
- F10.2 A stroke ends, keeping what it painted, when the tool is
  deactivated or its viewport is hidden; only Escape cancels.
- F5.6 The seed advances after every stroke that began, a cancelled one
  too.
- F7.4 An item painted with Align To Surface keeps its rotation in its
  support's space; it does not turn again when a later shape change tilts
  the surface under it (design FD0.10).

## For the user to review

The planner's choices that no ruling covers:

- F1.6 Level Check leaves foliage out of its support and overlap findings.
- F4.2 Brush radius default 2 m, range 0.1 - 50 m, wheel factor 1.1.
- F5.1 A new stamp each half brush radius of travel.
- F5.2 Paint Density as items per square meter one stamp tries to add,
  default 0.5, at most 1024 attempts per stamp.
- F9.2 Remove Density as a per-item chance per stamp.
- F5.4 At most 8 candidates per attempt.
- F5.5 Upright by default, Align To Surface as a tool value.
- F9.1 The tool consumes the right press while it is active.
- F9.3 Remove Only Palette Shapes.
- F12.1 The 2 ms stamp budget of the index.
