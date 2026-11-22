# Foliage paint tool: implementation steps

Status: in progress

The build order for `doc/plans/foliage_paint_tool_design.md` (FD-numbers).
It meets `doc/plans/foliage_paint_tool.md` (F-numbers; GitHub issue 302).
One step is one commit. A step is done when its verification passes. A
done step is removed from this list, and what it built is described in
`doc/editor/level_tools.md` (a new section "Foliage paint", and the edits
to "Surface placement", "Placing" and "Level Kit window" each step names).

Each step is one coder brief (`doc/agents/orchestration_harness.md`): the
coder builds, runs the step's own checks and leaves the changes
uncommitted; the orchestrator reviews the diff and commits. A step runs
its own checks only (`AGENTS.md` "When to run the slow test suites"): the
test cases it adds (`editor_level_tests --gtest_filter=<suite>.*`) and its
verify group (`scripts/level_tools_verify.py --only-<group>`). Step 9 runs
the gates:

- `editor_level_tests`;
- `scripts/level_tools_verify.py` (full);
- `scripts/curve_verify.py`;
- `scripts/navigation_verify.py`;
- `ctest -C Debug -L editor -j1`.

MCP tool changes go into `config/editor/mcp_tools.json` in the step that
makes them.

## Built

Step 1, the foliage flag and the placement rays
(`doc/editor/level_tools.md` "Surface placement", verify group
`--only-foliage-flag`); step 2, the foliage index (design FD4,
`Foliage_index` tests of `editor_level_tests`); step 4, the scene-free
sampler and palette (design FD7, FD8, FD10.1, FD10.2; the `Foliage_sampler`
and `Foliage_palette` tests); step 3, the foliage system, the radius of
shapes and pieces and `level_get_foliage` (`doc/editor/level_tools.md`
"Foliage paint", the `Foliage_system` tests, verify group
`--only-foliage-index`); step 5, paint strokes: foliage placement, the
stroke as a live edit of the undo stack and `level_foliage_paint`
("Foliage paint", `doc/editor/operations.md` "Live edits", verify group
`--only-foliage-paint`); step 5b, live edits excluding other changes
(`doc/editor/operations.md` "Live edits", the `live_edit` tests of
`editor_asset_tests`); step 6, remove strokes and `level_foliage_remove`
("Foliage paint", verify group `--only-foliage-remove`); step 7, the tool:
commands, brush, the travel walk, Tool Properties, `Foliage_paint_config`
and the settings tools ("Foliage paint", verify group
`--only-foliage-tool`, which also holds the live edit checks that need a
held stroke); step 7b, the palette entries' parameter rows; step 8, scale:
the child name index of `erhe::item` (`doc/erhe/item.md`), the timed stamp
case, the measured numbers ("Foliage paint", Scale) and the costs left
open (`doc/plans/open_items.md` "Foliage scale").

Step 9, the final pass: the series was reviewed as one diff, its findings
were fixed, and that fix round was reviewed twice more. The gates on the
Windows headless Debug tree, with the committed config: `editor_level_tests`
993, `editor_asset_tests` 89, `erhe_item_tests` 211, `erhe_commands_tests`
19, `level_tools_verify.py` (one full run) 2432 of 2432,
`viewport_context_menu_verify.py` 42, `navigation_verify.py` 76,
`curve_verify.py` 156 of 157 and `ctest -C Debug -L editor -j1` 108 (the
one `curve_verify.py` check is the known one of `doc/plans/open_items.md`
"Verify scripts").

## Open

- Unit tests of `Operation_stack`'s live edit rules: the stack is an ImGui
  window and calls editor parts directly, so `editor_asset_tests` cannot
  hold it.
- A curve point insert or add that is deferred behind a scene save does
  not select the new point when it later runs.
- The user test below, the rulings of `doc/plans/foliage_paint_tool.md`
  "For the user to rule on" and "For the user to review", and the review
  of the design's FD0. For review as well: the Brush tool's pick and wheel
  rotate over a foliage item act on the support behind it; a stroke counts
  a candidate on a mesh inside a reference instance as spent
  (`on_sealed_support`); the Foliage Radius row has its own "Foliage"
  group.
- The push, on the user's instruction.

## User test (windowed)

Build the windowed Debug editor, reset `config/editor/desktop_windows.json`
and `editor_settings.json`, launch it for the user
(`doc/agents/editor_runs.md` "Handing the editor to the user") and ask for
these checks:

1. The Foliage Paint tool in the hotbar; Tool Properties shows its values
   and an empty palette; Add Entry lists the ten vegetation shapes.
2. A palette of grass tuft (weight 5), bush (1) and tree (1): a left drag
   over a floor paints them, upright, overlapping, grass most often.
3. Painting the same ground again adds more until it stops taking any.
4. Shift + wheel changes the brush circle; the plain wheel zooms.
5. A right drag removes; Remove Density 0.25 thins instead.
6. One undo per stroke; Escape during a drag leaves nothing.
7. Paint on a slope and a wall, with and without Align To Surface.
8. Move the floor: the foliage rides. Drag a floor corner or a wall's
   length: the foliage stays on it.
9. The Brush tool places a crate on painted grass: it lands on the floor.
10. Select a painted bush in the viewport, move it, change its Foliage
    Radius in Properties, delete it.
11. Save, close and open the scene: the foliage is there and painting
    respects it.
