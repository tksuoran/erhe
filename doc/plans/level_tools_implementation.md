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
exclusion set
(a layout element rests on its own brush's bounds, snap casts again
after a rise until a cast rises no more), step 35, the oriented rest that
agrees with itself likewise (its ray passes through the base center, the
pivot of the turn, and it casts again after a rise), and step 34, surface rays whose
answer does not depend on hit order among coincident faces (a hit filter in
`erhe::raytrace` takes the nearest front face of a mesh that is not
excluded), and step 33, a Transform tool edit recording an undo entry only when
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
are built and described in
`doc/editor/level_tools.md` and `doc/editor/navigation.md`.

## Next steps

12. The `[should]` requirements not named above, each as its own step:
    assemblies (R5.7), the further shapes
    (R4.2 - R4.4), texture coordinates (R2.8), the walkable area picture and
    "why not" (R9.7, R9.8), the remaining spatial tools (R10.8 - R10.12), the
    agent workflow document (R6.7).
    Replace (R5.8): ruled in U24 (steps 47 - 49). Not yet asked: the carry
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
    Connectors (R5.3): ruled in U26 (step 52; the active piece as the target
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
    Connector snap (R5.13): ruled in U27 (steps 53 - 56; the locked channel
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
15. Ruled in U23: steps 45 and 46 replace the question of when a snap to
    surface lifts a piece that overlaps other pieces. Today a piece buried
    deeper than its own height, placed or moved, drops to the first surface
    under the solid's bottom, and a piece overlapping a stack or a coursed
    wall climbs to its top in one snap (D7.3).
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
    surface resting (scaled bounds), replace and the child match of D3.9
    keep it; the scale key quantization bounds the meshes per item.
40. Placing a connected piece from the Brush tool's viewport preview (R5.3):
    the kit mode preview snaps to the nearest connector of the hovered piece
    and a click places through `Kit_placement::connect`.
41. A persistent connector attachment (R5.3): a joined piece remembers its
    target and connectors and follows a move or parameter change of the
    target.
44. Kit layout shape visualization (user, 2026-11-15: "Scatter node does not
    have volume visualization"). Of the layout kinds only the path is drawn
    today, as its curve (`doc/editor/curves.md` "Drawing"); a line, grid,
    ring or scatter node shows nothing of its own. (The "Layouts" debug
    visualization draws `erhe::scene::Layout` volumes, which kit layout nodes
    do not have.) To build:
    - What is drawn, in the node's frame from its `Kit_layout_pattern`
      (`level/kit_layout_values.hpp`): scatter - the region outline (the
      `size_x` by `size_z` rectangle or the `region_radius` disc) centred on
      the origin; line - the segment from the first to the last element
      center (`step` times count, or `length`) with a tick per element; grid
      - the outline of the `count_x` by `count_z` extent with a tick per
      row and column; ring - the circle or `arc_degrees` arc of `radius`
      with a tick per element. The ticks come from the resolved run
      (`Kit_layout_run`), so they sit where pieces are generated before
      module snap and surface rest.
    - When: a setting "Kit Layouts: off / selected / all" next to Curves and
      Connectors in the Debug Visualization popup, default selected; selected
      means the layout node or any piece below it is selected. X-ray lines,
      screen-constant tick size, colors and widths in
      `Debug_visualizations_style`, as the curves and connectors.
    - Cost: the parsed pattern and resolved run are cached per layout node
      and dropped on `Kit_layout_changed_message`, item removal and scene
      close; a value drag's live preview sends that message, so the outline
      follows the drag. World matrices are read at draw time; scratch keeps
      its capacity (the pattern of `Kit_connector_indicators`).
    - Checks: an MCP read of what a viewport draws (as
      `level_get_connector_indicators`), expected from the pattern values;
      one screenshot difference between off and all; the outline follows a
      held value drag and a node move.
    Ruled (U28): the outline is drawn flat in the node's plane in the first
    version (the orchestrator's choice, left to it by the user); the scatter
    also shows its `min_spacing`, a circle per piece; the scatter region
    gets viewport handles to resize it (a point-set edit as the curve
    tool's, `doc/editor/point_set_editing.md`), one undo entry per drag with
    the live layout preview during it.
45. Snap to surface casts both up and down (U23): from a piece's base
    points one ray goes along the snap direction and one against it, and the
    piece snaps to the nearer surface, so a piece buried in a solid comes to
    the nearest of the surface above and the one below. The direction is
    down today; a custom direction is future work. Replaces the "drops to
    the floor under the solid" behaviour (D7.3, `find_surface_placement()`),
    for snap and for orient; the checks of the buried block and of the
    blocks in the hill change with it.
46. Snap to surface has a maximum distance setting (U23): a surface farther
    than the distance from the piece's base is not snapped to, in either
    direction, so a crate beside a stack or a coursed wall stays unless the
    step up is within the distance. Decide with the user whether the limit
    is per cast or for the whole snap (the chain of casts, D7.3), the
    default value, and where it is shown (with Snap To Surface in the
    toolbar, Move rows, Level Kit window; `Surface_snap_config`; the kit
    layout node's own surface snap). Rename "surface rest" to "snap to
    surface" in the UI texts, MCP descriptions and documents.
47. Replace snaps the replaced pieces to the surface again when Snap To
    Surface is on, in the same undo entry, and keeps the transform when it
    is off (U24).
48. Children of a locked kit layout node are not selectable until the layout
    is unlocked (U24): check every selection path (viewport click, box
    select, the hierarchy window, `select_items`) against the existing
    redirect of a child's selection to its layout node, and close what lets
    a child be selected. Replace Selected replaces what it can and lists
    the skipped pieces in its message and in the MCP reply, in place of all
    or nothing; a selected group or kit layout node stays left out.
49. The kit layout node has a boolean property "Per Piece Seed" (U24).
    Today the switch is `Kit_layout.random_seeds`, shown as "Random Seeds":
    confirm with the user that this is that switch under the new name (then
    rename the row, the MCP key with a reader for the old one, and the
    documents), or what else it adds - the ruling answered what a replace
    does with pieces that share one seed.
50. Scatter picks each point's item at random from the layout seed, with a
    weight per item (U25): a weight value in `Kit_layout_item` (default 1,
    in the canonical text, Properties and Level Kit window rows, MCP),
    drawn from the scatter's integer stream so the pick is the same bits on
    every platform; the other kinds keep item i modulo the item count.
51. Scatter spacing is the gap between piece bounds (U25): each candidate is
    accepted when its footprint (the item's bounds radius in the node's
    plane, from the item's own brush, with its yaw) keeps `min_spacing`
    clear of every accepted piece's footprint. State which footprint
    (circle of the bounds, or the oriented box) and keep the test exact and
    free of fused multiply-add as today.
52. Connectors can be enabled and disabled per node (U26): a property (or
    one per connector) on the kit piece, kept in the scene file, edited in
    Properties and over MCP; a disabled connector is not drawn as available,
    not offered by the Connect rows and Place Connected, not a snap source or
    target (U27: "any pair when both connectors are enabled"). Decide with
    the user: one switch per connector name or one for the whole piece, and
    the default (all enabled). This is the ruling on where a floor slab's
    side connectors sit: the author turns off the ones a piece should not
    offer.
53. Connector snap has a setting for axis and plane drags (U27): full
    offset (default, as built) or stay on the dragged axis or plane, where
    the snap applies only the offset's component along the handle's axis or
    in its plane.
54. A modifier key held during a drag suspends the connector snap (U27):
    choose the key with the user against the Transform tool's existing
    modifiers (`doc/editor/input_bindings.md`), bindable as the other
    commands are.
55. The connector snap starts only after the pointer has moved (U27): the
    zero-motion step of a drag does not snap, so a press and release on a
    handle changes nothing and records nothing; a drag out and back to the
    start may snap. Removes the documented "zero step snaps" rule and its
    check.
56. Rotate and scale drags snap to connectors too (U27): after the step's
    rotation or scale the moved set translates so the closest pair within
    the distance co-locates, evaluated from the unsnapped pose each step as
    the translate drag does; the moved connectors are gathered so they
    follow the step's rotation and scale (today they are fixed in each
    root's frame at drag begin, which already carries them).
