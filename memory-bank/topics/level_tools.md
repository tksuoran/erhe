# Level tools

Working notes for the level kit, kit layouts, curves, foliage paint and the
navigation reads. Behavior, plans, rulings and open items live in the
documents (`doc/editor/level_tools.md`, `doc/plans/level_tools*.md`,
`doc/plans/level_kit_*.md`, `doc/plans/open_items.md`).

## Shape work preferences (user, 2026-11-16)

- New kit shapes get light verification per batch: "We want to use more
  lightweight verification for new shapes - generate all shapes first and do
  verification afterwards" (build, `editor_level_tests`, renders per batch;
  the full verify scripts, `ctest` and one review pass once after all shapes).
- "Do all shapes first, in one or few commits, then verification."
- The renders of each shape are shown to the user when the shape is done,
  before the separate verification (a recreated shape side by side with its
  original).

## Debugging traps

- Injected input in a headless run is not SDL input: a UI bug seen only in
  the windowed editor may not reproduce headless.
- Reproduce a UI bug with the user's config: a Properties row drag freeze
  (2026-11-14) hid in headless runs because developer mode was off there.
- A lost ImGui drag: a logging breakpoint on `ImGui::SetActiveID` (condition
  `id != ActiveId && MouseDown`) gives the call stack of what took the drag.
- Running `scripts/level_tools_verify.py` while the user's editor is open:
  the script restores `config/editor/*.json`, `user_state.json` and the ImGui
  ini at its end, so the two fight over those files. Run it only when the
  user's editor is closed.
- A snap check's fixture starts within the search distance of its surface
  (1 m forward by default) or passes a distance; otherwise the snap finds no
  surface and the piece stays where it was.
- On GCC, Recast's DebugUtils target fails under -Werror: build the named
  targets, never `all`.

## Agent workflow traps

- A review of a whole multi-commit series finds defects that the per-commit
  reviews missed (15 in the level kit connections series, 2026-11-20): run
  one at the end of every multi-commit series.
- A coder agent stopped by the five-hour usage limit is resumed with
  `SendMessage` after the reset; its worktree edits persist.
- A gates agent finishes before a coder rebuilds the same build tree.
- Coder builds write their output to a log file; never pipe a build into
  `Select-Object -First` (the build stalled) and never stop a running build
  (orphaned `cl.exe` processes kept `erhe_pch.pdb` locked).
