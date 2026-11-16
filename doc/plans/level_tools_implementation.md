# Level tools: implementation steps

Status: in progress

The build order for `doc/plans/level_tools_design.md` (D-numbers), which
meets `doc/plans/level_tools.md` (R-numbers). One step is one commit. A
step is done when its verification passes; a done step is removed from
this list, and what it built is described in
`doc/editor/level_tools.md`.

## Built

Steps 1 - 7, the first version a user can test (the shape library, pieces
over MCP, parameter changes, surface placement, walkable area reads, the
Level Kit window, persistence, placing in the viewport with the Brush
tool, live parameter drags, derive from seed, rock forms, surface settings in the toolbar and the
Transform tool), step 8 (region contents, height map and support report as MCP tools and
Level Kit window actions), step 9 (overlap report and level check, likewise)
step 10, the pattern expansion (line, grid and ring runs) and the
group tools (group, ungroup, duplicate and mirror), over MCP and in the Level
Kit window, step 11, material slots per shape (the tree's bark and
foliage, a palette default and alternatives per slot, a material per slot at
placement and on a placed piece), step 23, the kit layout node (its
values with count and gap modes, generation, the lock with its operation, the
refusals for children of a locked node, persistence and its MCP tools), step
24, the kit layout node in the Level Kit window (Make Layout from a piece and
the rows of every value, items, lock and refresh), and step 25, a refused
Properties row write of a `Kit_layout` value recording no undo entry, and
step 22, the path layout as one curve node carrying the layout values
(R5.12.8, `doc/plans/curves.md` C6, C7: count and gap over open and closed
curves, upright and tangent orientation, laid out again in the entry of each
curve edit and live during a point drag), and the kit layout node's values in
the Properties window with the Level Kit window holding the initial values of
a new layout node (R5.12.9), and step 27, the kit layout move hook and its
live preview for a node moved through the Properties window's transform rows
or `set_item_property`, step 30, the Escape cancel of a Transform
tool node drag (the moved layouts get their children from before the drag
back), and of step 29 the snap rest that agrees with itself for one
exclusion set (a layout element rests on its own brush's bounds), step 35,
the oriented rest that agrees with itself likewise (its line passes through
the base center, the pivot of the turn), and step 34, surface rays whose
answer does not depend on hit order among coincident faces (hit filters in
`erhe::raytrace` judge each candidate on its own), and step 33, a Transform tool edit recording an undo entry only when
an entry's local transform at its end differs from its start (an unmoved
pointer gives an exact identity delta, which puts the entries back at their
start pose), and of step 12 replace (R5.8: the shape of selected pieces
swapped in one entry, node and transform kept, parameters carried by name
and type, slot materials by name, over MCP and in the Level Kit window) and
scatter (R5.5: the kit layout kind `scatter` over a rectangle or a disc, by
count or density, with minimum spacing and a yaw range from the layout seed,
without per-piece scale) and connectors (R5.3: named frames per shape that
follow the parameters, a new piece placed or a placed piece moved so its
connector meets another piece's, with twist steps and a gap, over MCP and in
the Level Kit window), and step 43, snap to connectors during Transform
translate drags (R5.13: a distance setting, the closest connector pair
within it co-located by translation at every step and released beyond it),
and step 42, connector indicators with connectors that follow their node's
full world transform, scale included (U22: an arrow and up tick per
connector of the selected or all pieces, a snapping drag's pair
highlighted, scaled pieces and targets in joins and the drag snap),
and steps 45 and 46, snap to surface as a search (U23, U30, U31:
the nearest place along the snap direction, up or down, where the base rests
on a surface and no hit point lies inside the piece's span, within a maximum
distance, 1 m by default, in the surface settings, the Move rows, the
toolbar, the Level Kit window, the kit layout node and the MCP tools; no
place leaves the pose unmoved), called "snap to surface" in the UI texts,
MCP descriptions and documents, and step 49, the kit layout switch Per Piece
Seed (U24, U32: `Kit_layout.per_piece_seed`, the MCP key `per_piece_seed` and
the setting `layout_per_piece_seed`, each still reading its former name
`random_seeds`), step 47, Replace snapping the replaced pieces to the surface
again in the same undo entry when Snap To Surface is on (U24), and step 48,
children of a locked kit layout node never selected by any selection path,
a lock handing their place to the layout node, and Replace Selected and
`level_replace_pieces` replacing what they can and listing the skipped nodes
with their reasons (U24), and steps 50 and 51, a scatter picking each
point's item at random from the layout seed by an integer weight per item
(0 - 1000, default 1, an exact integer pick from the candidate's fourth
draw) and holding `min_spacing` as the gap between piece footprints (the
circle about each piece's vertical axis around its item's xz bounds) (U25),
and step 52, connector switches per piece (U26, U33: a switch for all of a
piece's connectors and one per connector name, saved with the node, in the
Properties window and over MCP; a disabled connector is not drawn, offered,
joined or snapped), and steps 53, 54 and 55, the connector snap of a
translate drag refined (U27: a Connector Snap On Axis setting that applies
only the offset's part along the dragged axis or in the dragged plane, a
bindable suspend key, and the snap starting only after the
pointer has moved, so a press and release changes nothing; the default
suspend key, Shift, confirmed in U35), and step 56, rotate and scale
drags snapping to connectors (U27: after the step's rotation or scale the
moved set translates by the full offset of the closest pair, each step from
its unsnapped pose), and the drawing of step 44, line, grid, ring and
scatter kit layout nodes drawn flat in the node's plane with a tick per
element and a selected scatter's footprint and spacing circles, under the
Debug Visualization popup's Kit Layouts setting and read over MCP (U28;
an unlocked layout's outline showing the pending values during a value
drag is the orchestrator's choice, for the user's review), and the rest of
step 44, the scatter region handles (U28: a selected scatter's rectangle
corners and edge midpoints or disc points as a point set of the curve
tool's kind, the gizmo on the selected handle resizing the region
symmetrically about the node's origin, the live layout preview of a locked
layout or the pending outline of an unlocked one during the drag, one undo
entry per release, Escape restoring the start; shown while the node is the
only selected item whatever the Kit Layouts setting, the coder's choice, for
the user's review), and step 66, the segments kit layout (U37,
R5.12.10: a kind `segments` on a Curve node, switched in place with
`path`, one piece per segment between two neighbouring control points,
scaled along its own x axis so its item's x bounds span the straight chord;
laid out again in the entry of each curve edit and live during a point drag;
the collision shape at the piece's scale; the orchestrator's choices listed
under U37, for the user's review), and step 67, the segment handles (U38,
R5.12.11: on a segments layout that is the curve tool's target, a handle at
each segment's chord midpoint beside the point handles; one segment
selected at a time, exclusive with the point selection; the gizmo drag
moves the segment along its horizontal normal alone, each end point sliding
along its neighbouring segment's line or along the normal where there is
none or it is parallel; live layout preview, one `Curve_set_operation` per
release, Escape restoring the start; MCP `level_select_segment_handle`,
`level_get_segment_handles`, `level_move_segment`; the orchestrator's
choices listed under U38, for the user's review), and step 68, the
layout connector switches (U39, R5.12.12: seven values on a path or
segments layout node, Hide All and one per direction, +/- tangent, normal
and binormal in each child's own frame; a locked layout's inner children,
every child of a closed curve, follow them, an open curve's first and last
child only for the connector facing its neighbour; the hiding is part of
the one connector query, `Level_kits::get_connector_switches()`, so a
hidden connector is not drawn, snapped to or joined; the values in the
Properties window's Kit Layout group, the Level Kit window's initial values
and over MCP; the orchestrator's choices listed under U39, for the user's
review; switches per layout item stay step 60, on hold), and step 69,
collision follows scale (U40: every body's shape is its source shape at the
node's world scale, rebuilt in a pass each frame when that scale changes, by
any writer, also while the simulation is off; the implicit mass follows the scaled shape; a sphere or capsule
under a non-uniform scale becomes its mesh's hull; the orchestrator's and
the coder's choices listed under U40, for the user's review), and step 71,
the spline staircase (U42, R5.12.13: a kit layout kind `stairs` on a Curve
node, switched in place with `path` and `segments`, whose one child is a
staircase generated along the curve: flights of equal steps and level
landings that keep the walking surface within one step rise of the curve's
height, risers square to the curve's plan direction, side edges following
the curve at an edge tolerance, a mitred landing at a corner point, a
soffit at a stated thickness; a curve too steep, too tight or closed is
refused naming the place and the previous staircase stays; a static
triangle mesh body, navigation input; regenerated in the entry of each
curve edit and live during a drag; the fit readback over MCP and in the
Properties Run line; the orchestrator's and the coder's choices listed
under U42, for the user's review),
are built and described in
`doc/editor/level_tools.md` and `doc/editor/navigation.md`.

## Next steps

### Active

The steps to build, in this order (user, 2026-11-15).

62. Expand the kit with more shapes (user, 2026-11-15), after step 44. The
    user's list: geological rock formations such as cliffs, in diverse
    sizes; tropical plants, in diverse sizes; palm trees of different
    species; natural features found on beaches (U34: man-made harbour
    shapes belong to step 63); architecture modules of different
    historical cultures. It takes in the nature and architecture shapes of
    R4.3 and R4.4 that are not built. First a shape list per category
    (which shapes, their parameters and derive ranges, material slots,
    connectors, the physics body kind) in
    `doc/plans/level_kit_more_shapes.md`, chosen by the orchestrator (user,
    2026-11-15: build from it, the user reviews the shapes afterwards), then
    one commit per
    shape or small family, each with its checks in
    `scripts/level_tools_verify.py` and its entry in
    `doc/editor/level_tools.md`. Hanging vines (foliage) and "path" from the
    user's list are future work, not part of this step.
63. Extra shapes of the orchestrator's choice (user, 2026-11-15), after
    step 62 and only while the account's weekly usage is below 85 percent
    of its limit: the orchestrator picks the topic and the shapes, whatever
    it judges useful or good-looking, and builds them as step 62's shapes
    are built. The figure is the seven-day usage percentage the status line
    shows; read it before starting and before each further shape. The
    inventory, with the rulings of U34, is
    `doc/plans/level_kit_extra_shapes.md`.
70. Recreate the kit shapes (user, 2026-11-16, U41): every existing shape
    written afresh from its description by a coder on the Fable model at
    medium effort, registered beside its original
    (`doc/editor/level_tools.md` "Recreated shapes"), one commit per batch,
    each with the batch's rows in `generated_meshes_are_pinned`, its checks
    in `scripts/level_tools_verify.py` and its images from
    `scripts/level_shape_compare.py`. Built: the parallel-version
    infrastructure (`recreates`, the pairing rule and its test, the
    comparison script) and the basic kit. Batches to build, in this order:
    architecture (wall, door wall, pillar, arch); japanese and classical
    (torii, column, entablature); medieval and roof (battlement, tower,
    roof); nature (rock, land mass, hill, tree, bush); nature rocks (rock
    spire, basalt columns); nature terrain (cliff, shore, water, dune,
    ground patch); nature plants (palm, leaf plant, palm clump, palm
    shrub). The user then rules per shape which version keeps the
    canonical id. The ruling step (to build): the kept version takes the
    canonical id, the other definition is removed, and the dropped id
    becomes a former id of the kept shape that `Kit_shape_library::find()`
    resolves, so saved pieces and layout item texts of either id keep
    loading, as `erhe::property` former names do. A later batch extends
    `recreated_originals()` in `test_kit_shapes.cpp` and `RECREATED_SHAPES`
    in `scripts/level_tools_verify.py`.

### On hold

Every step below is on hold until the user chooses to make it active; none
is started from this list alone.

64. Viewport handles for line, grid and ring kit layouts (the line's step or
    length, the grid's spacings, the ring's radius and arc), as the scatter
    region handles are built (U28 asks for the scatter's only).
    For the user's review (step 44): the Kit Layouts "selected" rule walks
    upward only (a selected group above layouts shows no outlines), while
    the Connectors "selected" rule walks downward; the region handles show
    whatever the view's Kit Layouts setting, and a handle under the node
    gizmo's arrow is picked only where it is clear of the arrow; during a
    drag not along the handle's side (an axis drag of a turned node's
    handle, tangential motion of a disc handle) the gizmo parts from the
    handle until the release.
65. Roofs and navigation (for the user): a roof has a triangle mesh body and
    at pitches of 15 - 45 degrees its slopes are walkable navigation input, a
    walkable island over every roofed room; decide whether a roof is
    navigation input, passes through (no input) or counts only where reachable.
12. The `[should]` requirements not named above, each as its own step:
    assemblies (R5.7), the further shapes
    (R4.2 - R4.4), texture coordinates (R2.8), the walkable area picture and
    "why not" (R9.7, R9.8), the remaining spatial tools (R10.8 - R10.12), the
    agent workflow document (R6.7).
    Replace (R5.8): ruled in U24 (steps 47 and 48; step 49 is built). Not yet asked: the carry
    matches parameters by name and type only, not by unit; a same-shape
    replace ignores given parameters and materials; the kept node name is the
    old shape's display name (a rock "Block").
    Scatter (R5.5): ruled in U25 (steps 50, 51; re-deciding every point and
    the Run row stay as built). Not yet asked: the region is centred on the
    node's origin and Make Layout centres it on the piece, which is not kept
    as element 0; the points come from the layout seed whether the per-piece
    seed switch is on or not; density rounds to the nearest count and holds
    at least one piece; at most 64 candidates per piece asked for, a disc's
    candidates outside it counting against them; a density-mode scatter
    changes its piece count when the region is resized; per-piece scale is
    step 39.
    Connectors (R5.3): ruled in U26 (step 52 is built; the active piece as the target
    stays as built). Not yet asked: Place Connected's default new-piece
    connector is the shape's first (base, start or bottom); a side connector
    against a top connector tips the piece, the twist picking among the four
    headings; the nature shapes have no connectors; `level_connect_pieces`
    moves a kit piece only, never a group or a kit layout node; a join within
    0.1 mm and 1e-4 rad of the current pose records nothing; under a mirror
    the connector frame stays right-handed and a mirrored piece's paired
    connector names change sides; the target of a placement is an existing
    node, not a piece of the same `level_place_pieces` call; a joined piece
    keeps no link to its target (step 41).
    Connector snap (R5.13): ruled in U27 (steps 53 - 55 built, 56 open; the locked channel
    rule and the scripted drags' default stay as built). Not yet asked:
    connectors of kit pieces below the dragged nodes count; the snap comes
    after the snap to surface and is not rested again; ties at equal
    distance go to the smallest (moved piece id, connector index, target
    piece id, connector index); a drag that pulls a jointed body through
    physics does not snap; the moved connectors are fixed at drag begin; a
    selected piece locked against viewport transforms is neither moved nor a
    snap target.
    Connector indicators (U22), not yet asked: Connectors defaults to
    Selected and a snapping drag shows its connectors even with Off; a drag
    shows targets within 4 snap distances of a moved connector; the Connect
    rows' target connector is highlighted with one piece selected too and
    stays highlighted while the Level Kit window is closed, until the
    selection changes; no connector name labels; drawn x-ray, as curves.
13. Shape thumbnails in the Level Kit window (R4.5) and a live preview
    while a parameter row is dragged (R6.2).
14. `Level_kits::refresh_scene()` for USD opens, foreign glTF imports and
    prefab instances of a level file (D3.5 covers erhe glTF opens).
16. A kit brush stays cached after its piece is placed with
    `level_place_pieces`, selected, the placement undone and the undo
    history cleared (`cached_brush_count` one too high): find what still
    holds the brush.
17. Whether a cancelled parameter drag leaves the navigation mesh marked
    stale (the preview steps swap primitives).
18. `scripts/mcp_transform_test.py` fails three checks that read a
    parent's position right after `place_brush` / `reparent_item`: find
    whether the script or the editor is at fault.
19. `mouse_drag` drops its last move when it releases in the same frame.
20. Scripted gizmo drags in a wide viewport (a 1395 x 535 viewport window,
    seen on a machine whose ImGui layout splits the viewports) are not
    measured since the scripts press and release on whole pixels
    (`doc/agents/mcp_ui_driving.md` "Coordinate space"): there, X drags
    landed 0.016 m short per metre before. `scripts/level_tools_verify.py`
    closes its earlier scenes so the gizmo drag checks keep one viewport;
    measure the wide layout on such a machine, then remove those closes.
21. An exact overlap depth for two triangle mesh pieces
    (`find_overlaps()` measures such a pair against convex hulls, which
    reports a piece standing inside another piece's opening as an
    overlap).
31. A Paste Properties of a transform onto several items runs the kit
    layout move hook once per item (one `Property_set_apply_operation`
    each): a layout below two pasted items regenerates twice, and layouts
    whose pieces support each other can rest differently than one hook for
    the whole paste would rest them.
32. A row edit left open when the Node Properties window or the Grid tool
    window is hidden holds its items until that window is next drawn (the
    Properties window gives the edit up in `hidden()`).
36. A piece with a negative Y scale stands on its bounds' maximum Y face, but
    `find_surface_placement()` takes the minimum Y face as the base (and the
    maximum as the top) for snap and orient alike, so its rays start at its
    world bottom and it rests buried by its height (or finds no surface when
    standing on one). The fix picks the base and top faces by the sign of
    the Y scale (the up axis is right as it is: from the maximum face to the
    minimum one it is the rotated +Y). A check needs a piece created
    mirrored: a `set_node_transform` that flips the sign of a placed mesh's
    determinant trips the VERIFY on runtime determinant flips in
    `draw_list_scene.cpp` `flush_pending()` (Debug).
37. An oriented rest with the base center on an edge where both faces end (a
    ramp tile beside a floor tile that ends at the seam, a ridge) is not a
    fixed point: the tie rule of `find_surface_placement()` sees both faces
    only from a start exactly on the edge, and a start one rounding step off
    (off-centre bounds, a direction off the axes, a parented piece such as a
    layout child) sees one face and turns the piece by the crease angle. Fix
    direction: gather the normals over a small lateral neighbourhood of the
    base center, or snap the ray onto the edge. The verify scenes run ground
    under their ramps, so they do not reach this.
38. A body created static and switched to dynamic (`edit_physics_body`
    `motion_mode`, the Properties window's Rigid Body motion mode) falls
    through static bodies: `Jolt_rigid_body::set_motion_mode()` changes the
    Jolt motion type only, and the object layer chosen at creation
    (`Layers::get_layer()`, `NON_MOVING` for a static kit piece) stays, so
    the filter (`NON_MOVING` collides only with `MOVING`) drops every pair
    with another static body. A kit block on a kit floor slab made dynamic
    with gravity falls through the slab to the respawn at y = -100. The fix
    sets the object layer with the motion type (`BodyInterface::SetObjectLayer()`),
    and a check makes a placed block dynamic over a slab and sees it rest.
39. Per-piece random scale for scatter (R5.5): a uniform scale range per
    scatter, each piece's scale from the layout seed. A node scale leaves the
    piece's collision shape unscaled (bodies take no scale), so a scaled
    piece takes its brush's scaled form (`Brush::get_scaled()`) and the
    `Kit_piece` record carries the scale, so follows-kit (D3.5), load,
    snap to surface (scaled bounds), replace and the child match of D3.9
    keep it; the scale key quantization bounds the meshes per item.
40. Placing a connected piece from the Brush tool's viewport preview (R5.3):
    the kit mode preview snaps to the nearest connector of the hovered piece
    and a click places through `Kit_placement::connect`.
41. A persistent connector attachment (R5.3): a joined piece remembers its
    target and connectors and follows a move or parameter change of the
    target.
57. Future work, skipped in this plan (user, 2026-11-15): snap to surface
    considering solidness (U31): free space from
    the solids along a base-point line, so a piece inside a solid taller than
    itself is not snapped to a surface inside that solid (the slab top under a
    hill or a wall).
58. The scatter footprint as the oriented box of the item's bounds at the
    piece's yaw instead of the circle around it (a refinement of the
    built step 51, U25): a tighter pack for long pieces, at the cost of an exact
    box-against-box test in the candidate loop and a yaw-dependent reach for
    the spacing grid.
59. Scatter spacing by the seeded brush's footprint (a refinement of the
    built step 51, U25): with Per Piece Seed a scatter element is spaced by
    its item's unseeded brush, while its geometry comes from
    `derive_layout_element_seed(seed, i)` (`nature.rock`, `nature.bush`
    derive their size from it), so seeded pieces can come closer than
    `min_spacing` or overlap. To build: the element index of a candidate is
    the count of points accepted before it, so the sampler needs, per
    candidate, the footprint of the brush that element i's seeded values
    make: a callback from `expand_scatter()` into `Level_kits` (brushes made
    or found in the cache during the dart throwing, which makes a resolve
    without the scene impossible: the unit-level resolve and the Run row
    would need the same callback), or a per-shape bound of the footprint
    over the seeded range (cheaper, looser). The Run row's count and the
    prefix stability of a raised count stay as they are, since element i's
    seed depends on i alone.
60. Connector switches per kit layout item (a follow-up of the built step
    52, U33): a locked layout's children cannot be edited, so their
    own switches are all on (a path or segments layout hides connectors by
    direction through its own values, step 68); an item could carry `connectors_enabled` and
    the disabled names, written on each generated child and compared by the
    kept-child rule.
61. Connector snap on axis, the candidate choice (U27 follow-up, for the user
    to rule on): with Connector Snap On Axis the pair is picked by its full
    distance, so a near pair lying across the axis wins with an applied
    offset near zero and reports snapped, shadowing a farther pair along the
    axis that would co-locate. The alternative picks by the distance left
    after the constrained offset.
