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
Transform tool), are built and described in
`doc/editor/level_tools.md` and `doc/editor/navigation.md`.

## Next steps

7f. A dragged parameter row freezes in the windowed editor: the value
   stops changing after a few frames while the button is held, on every
   slider row, in the Properties window and the Level Kit window. The
   headless editor does not show it (a 300-frame held drag keeps
   working). Suspect, unconfirmed: the window's relative hold cursor mode
   (`Viewport_window::want_cursor_relative_hold()`, requested by the
   navigation gizmo and by `Fly_camera_tool::set_cursor_relative_mode()`,
   whose switch-off returns early when no viewport is hovered) is on
   during the row drag, so every mouse move reports one fixed position
   and ImGui sees no motion (`sdl_window.cpp` relative hold branch);
   second suspect: `Imgui_windows::flush_queue()` runs queued operations
   newest first, so an older preview step can run after a newer one.
   Method: temporary `kitdrag:` info log lines along the row, the
   editor, `Level_kits`, the cursor mode switches and the event handlers
   (uncommitted, in the working tree of the machine that added them);
   the user reproduces once in the windowed Debug editor, then
   `grep "kitdrag:" logs/log.txt` shows which signature it is ("active
   without change" with zero delta and the button down = no motion
   reaches ImGui; "active id changed" / "not submitted" = the row lost
   the drag; "re-read" during a drag = stored values overwrote it). Fix
   the root cause, add a check that fails before the fix where the
   headless editor can express it, remove every `kitdrag:` line and the
   temporary reason arguments.
8. Region contents, height map and support report (D7.1, D7.2, D7.4), as
   MCP tools and as actions of the Level Kit window.
9. Overlap report and level check (D7.5, D7.6).
10. Repeat and group tools (R5.4, R5.6).
11. Several material slots per shape (R2.9) and the tree's bark and
    foliage as separate slots.
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
