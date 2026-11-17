# Level kit: verification pass

Status: in progress

The work that closes the level kit shape sessions of 2026-11-16 and
2026-11-17: tests, verify-script tables, documentation and review for the
shapes that were built under the light gate, and the user's review of the
choices made on the way. It runs after
`doc/plans/level_kit_more_shapes.md`, `doc/plans/level_kit_extra_shapes.md`
and `doc/plans/level_kit_machine_elements.md`, whose shapes are all built.
Read `doc/agents/orchestration_harness.md` and `doc/testing.md` first.

## State

- `main` holds every shape: the original and recreated (`.v2`) shapes, build
  order lines 1 - 24 of `level_kit_more_shapes.md`, the 36 shapes of
  `level_kit_extra_shapes.md` (batch files `kit_shapes_extra_01_08.cpp` to
  `kit_shapes_extra_33_36.cpp`) and the 18 shapes of
  `level_kit_machine_elements.md` (`kit_shapes_machine_01_08.cpp`,
  `kit_shapes_machine_09_18.cpp`), all under `src/editor/level/`.
- Every batch up to `extra_33_36` passed the light gate: `editor_level_tests`
  (753 tests after the last of them) and one render per shape that the coder
  looked at. The full verify scripts ran for none of them.
- The machine kit was built without a gate, on the user's instruction to
  save model usage. `kit_shapes_machine_01_08.cpp` has a test file and a
  skeleton rows file (`test/kit_shape_rows_machine_01_08.cpp`: no component
  counts, pins, triangle counts or worst cases); it was rendered once, and
  the extension spring hooks, the wing nut wings and the pear cam profile
  changed after that render. `kit_shapes_machine_09_18.cpp` has no rows file
  and no test file, and its meshes were never checked or rendered.
- The only gate run after the machine kit landed is that the `editor` target
  builds. `editor_level_tests` was not rebuilt on `main` after it.
- Other work of the same sessions on `main`, each reviewed and gated when it
  landed: connector snap orientation (U44), Wrap with Path (U45), path stairs
  from the UI and the Create menu's Level Kit submenu (U46, U47), the snap to
  surface base face (U48), the mesh upload order fix, `ERHE_DEFAULT_LAYOUT`,
  the deterministic clock and the verify-script speedups.

## Work, in order

Each numbered item is one commit. Coders for shape generators run on the
Fable model at medium effort (U41); test, script and documentation work
runs on Opus.

1. Machine kit tests. Build `editor_level_tests` and run it; expect failures
   in `Kit_shape_library.every_shape_passes_the_mesh_checks_at_defaults`,
   `Kit_shape_library.generated_meshes_are_pinned` and every library-wide
   test that needs a shape's rows. Fill
   `test/kit_shape_rows_machine_01_08.cpp`, add
   `test/kit_shape_rows_machine_09_18.cpp` and
   `test/test_kit_shapes_machine_09_18.cpp` after the pattern of
   `kit_shape_rows_extra_33_36.cpp` and `test_kit_shapes_extra_33_36.cpp`,
   and register them (`test/CMakeLists.txt`, `kit_shape_test_support.*`).
   The checks may find real mesh faults in the second batch (duplicate
   vertex positions, a base not at y = 0, a budget below the default count,
   derived values at other level lengths); the coder named the worm, the
   lock washer, the bevel gear teeth and the crankshaft as the likeliest.
   Fix the generator, not the check. Declare mirror planes where a machine
   shape is symmetric and update the count in `test_level_group.cpp`.
2. Machine kit renders. `python3 scripts/level_shape_compare.py --editor
   <headless editor> --single --only <shape id>` writes
   `logs/shape_compare_<id>.png`; look at every shape and each form, fix
   what reads wrong, and post the images to the user. The parts that mate
   are listed in `level_kit_machine_elements.md`; the pulley and pillow
   block bores do not match the default shafts.
3. Verify-script shape tables. `scripts/level_tools_verify.py` expects the
   shape set of an earlier state: the kit list and original counts in the
   catalog check, `EXACT_DEFAULT_SIZES` / `NOMINAL_DEFAULT_SIZES` for every
   catalog row, the pass-through rows of "every reloaded piece has its
   static body" (`nature.leaf_plant.v2`, `nature.grass_tuft`, `nature.shell`
   and the lit or body-less new shapes), `RECREATED_SHAPES`, and any
   hard-coded shape or kit count. Bring them to the present library; the
   full run then reports no failure.
4. Verify groups clean up after themselves. The groups of
   `level_tools_verify.py` leave their scenes open (eight at the end of a
   full run) and their tool active. Each group closes what it created and
   restores what it changed, so no group depends on its position in the run
   (MCP `reset_editor_state` exists for a harness that wants a clean
   editor between groups).
5. Documentation merge. The batch texts written by the shape coders
   (`logs/batch_docs/01_*.md` to `12_*.md`, untracked, on the machine the
   sessions ran on; where they are absent, write the text from the
   generators) go into `doc/editor/level_tools.md` under the kit shape
   sections, in the document style of `doc/README.md`. Add the machine kit
   from its generators. Update the `palette` key list in the "MCP" section
   (`metallic`), and set the status lines of the three inventory plans.
6. Review. One independent review of all shape generators written since
   `kit_shapes_architecture_v2.cpp` (correctness of the mesh contracts,
   randomness declared truthfully, one random draw per statement, topology
   from integer parameters only, connectors and seams), then the fixes.
7. Final gate on `main`, headless Debug tree: `editor_level_tests`; the full
   `python3 scripts/level_tools_verify.py --editor
   build_xcode_metal_headless/bin/Debug/editor`; `curve_verify.py`;
   `navigation_verify.py`; `ctest -C Debug -L editor -j1`;
   `check_doc_links.py`. Reset the tracked `config/` files to `HEAD` before
   any editor run (`doc/agents/editor_runs.md` "Config files: reset before
   testing"); the launchers set `ERHE_DEFAULT_LAYOUT=1` and
   `ERHE_DETERMINISTIC_CLOCK=1` themselves.
8. Hand the result to the user: the push of `main` needs the user's
   go-ahead for every commit after `7ac76fc0b`, and CI is checked after it
   (GCC and MSVC differ from Clang in argument evaluation order and in
   floating point contraction; the pin tests are tolerance based).

## User checks and rulings outstanding

- Windowed Metal: the staircase no longer shows stale geometry while a
  path stairs control point is dragged (the mesh upload order fix was
  verified headless only).
- Snap to surface in dense scenes. A piece rests on the highest surface
  under its base center or its four footprint corners, and a place is
  refused when any face lies inside the piece's height on one of those
  lines; in the Bistro scene a chair under a table finds no place and a
  block rests on clutter. Options offered: keep; only the center line can
  block; fall back to the surface under the center when blocked.
- U44: only free translate drags turn the set to face the connector pair,
  and alignment overrides Orient To Surface.
- U45: Wrap with Path of several pieces makes one path with all of them as
  items; the refusal list; kit light edits and node state are not carried
  to the copies.
- U46, U47: shape entries of the Create menu stay enabled under an unlocked
  kit layout node; the Level Kit window acts on the last hovered viewport's
  scene while Commands > Create acts on the active scene.
- U48: kit pieces keep their local -Y base at any tilt; other meshes take
  the base face from their world transform.
- The choices each shape batch made alone, listed in the "Built" paragraphs
  of `level_kit_extra_shapes.md` and the questions Q1 - Q4 of
  `level_kit_machine_elements.md` (among them the tunnel bore along X and
  the oversized machine part defaults).
- Earlier ones: the stone lantern's light values and its shadow on the
  ground beside it, box collision shapes for simple pieces, which version
  keeps the canonical id of each recreated shape, and the second-pass
  candidates `nature.boulder_cluster`, `nature.rock_arch` and the conch.

## Defects recorded on the way

`doc/plans/open_items.md` holds them: mirroring a registered mesh at run
time is fatal ("Editor defects found in passing"); the verify scripts not
yet on the deterministic clock and the optimized editor build for script
runs ("Verify scripts"). The tracked files `desktop_windows.json` and
`editor_settings.json` still change a headless run when a windowed session
modified them; a test launcher that ignores them, as `ERHE_DEFAULT_LAYOUT`
does for the dock layout, is a possible follow-up.

## When the pass is done

Remove the worktrees and branches the shape sessions used (`git worktree
list`; branches `local/shapes-a`, `local/shapes-b`), remove this plan's line
from `doc/plans/open_items.md` "Work order", and set this document's status.
