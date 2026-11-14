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
a new layout node (R5.12.9), are built and described in
`doc/editor/level_tools.md` and `doc/editor/navigation.md`.

## Next steps

12. The `[should]` requirements not named above, each as its own step:
    connectors (R5.3), scatter (R5.5), assemblies (R5.7), replace (R5.8),
    the further shapes (R4.2 - R4.4), texture coordinates (R2.8), the
    walkable area picture and "why not" (R9.7, R9.8), the remaining
    spatial tools (R10.8 - R10.12), the agent workflow document (R6.7).
13. Shape thumbnails in the Level Kit window (R4.5) and a live preview
    while a parameter row is dragged (R6.2).
14. `Level_kits::refresh_scene()` for USD opens, foreign glTF imports and
    prefab instances of a level file (D3.5 covers erhe glTF opens).
15. A piece buried deeper than its own height rises onto the surface
    (D7.3 starts its rays one piece height above the base).
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
20. Gizmo X drags in `check_transform_surface` (`scripts/level_tools_verify.py`)
    land about 1 px short in a wide viewport. With one more scene open
    before that check (the spatial queries scene left open), its scene
    shows in viewport window 4 at 1395 x 535 instead of window 3 at
    1086 x 1120. Each drag that grabs the "Translate X" handle (handle 1)
    then moves 0.984 m for 1 m and 0.786 m for 0.8 m, while a drag of
    handle 2 by 2.7 m is exact. The shortfalls add up to 0.03 m, so the
    checks "mid-drag over the slab's edge ...", "a drag onto the wedge
    with snap ..." and "with Orient To Surface the drag onto the wedge
    ..." fail against their 0.02 m tolerance. Ruled out: the surface-ray
    rule (the failure stays with the old rule), a stale viewport
    rectangle or camera (`get_viewports` reports the same values before
    every drag), late settling (positions are equal 0, 0.5 and 1.5 s
    after a drag), the scroll in `click_site`, and pointer quantization
    (`mouse_drag` and `Mouse_move_event` carry floats). Not yet known:
    whether the script's projection (`window_point()`) or the Transform
    tool's drag mapping is off. `check_spatial_queries` closes its scene
    until this is found. `scripts/curve_verify.py` reproduces it on
    point drags in one fixed viewport: an X drag lands 0.84 px short at
    288.6 px/m (lattice point) and 0.85 px short at 311.7 px/m (curve
    point), a Y drag 0.59 px over at 282.7 px/m, so the error is a
    sub-pixel offset per drag, not a scale.
21. An exact overlap depth for two triangle mesh pieces
    (`find_overlaps()` measures such a pair against convex hulls, which
    reports a piece standing inside another piece's opening as an
    overlap).
27. A move of a node through the Properties window's transform rows runs
    the kit layout move hook (`Level_kits::append_layout_move_operations()`)
    and its live preview (`doc/editor/level_tools.md` "Live layout
    preview"), as the Transform tool and `set_node_transform` do: today a
    surface-snapped layout moved that way keeps its old children until a
    refresh. Those rows are generic `Dependency_property` rows that commit a
    `Property_set_operation` of the node's transform property, not the
    Transform tool's edit, so the hook needs a transform-property follow-up
    in `Property_set_operation` and the preview a drag hook in the generic
    row editor.
29. Surface resting disagrees with itself in three observed cases; find
    the cause (the pass-through rule of `cast_surface_ray()`, or bounds
    taken from another mesh than the piece's own):
    - a ring layout of trees with per-element seeds and snap to surface on
      a hill leaves some trees 0.02 - 0.05 m above the surface (the support
      report lists them);
    - on a hill, `level_snap_to_surface` with `query_only` answers 0.19 m
      away from where the layout generation rests the same piece;
    - overlapping 1.4 m rocks whose rays start inside a hill rest 0.36 m
      apart between a preview step and the release.
30. The Transform tool has no Escape cancel for a node drag (the release
    always records); with one, a cancelled drag of a surface-snapped layout
    node ends its preview and restores the children.
