# USD survey gap loop

How the usd-wg survey (`scripts/usd_wg_asset_survey.py`, results in
`doc/usd-wg-assets.md`) is driven to zero gaps one entry at a time, with
the coding delegated so that no single context fills up. Roles, brief
shape and review are those of `doc/agent-orchestration-harness.md`; this
document states only what the gap loop adds: the command, the triage
rules, the worker's stopping rule and the hand-over between workers.

## The loop

One run of the survey with `--stop-on-gap` surveys the entries in this
order and halts after the first whose verdict is not `works`:

```
py -3 scripts/usd_wg_asset_survey.py --root <usd-wg-assets> --usd-root <usd_root> --only test_assets --exclude full_assets --exclude MaterialX --record-test-db --unrecorded-first --failing-first --stop-on-gap
```

`--unrecorded-first` runs entries the test database has never seen,
`--failing-first` then the recorded failures and gaps, shortest run first,
and the passing entries last, so a run reaches the next open gap in one
or two entries. MaterialX is excluded because it is scoped future work
(`doc/usd-compatibility-plan.md` section 6). Set `ERHE_AI_DRIVER=1` in the
environment. The `<usd_root>` and `<usd-wg-assets>` paths are per machine
(`memory-bank/local/context.md`).

The state the loop keeps between runs, all committed except the first:

- `logs/usd_wg_survey/test_database.json`: per entry `pass` / `gap` /
  `fail`, verdict and run time (gitignored; `--clear-test-db` empties it).
- `doc/usd-wg-assets-expected.json`: the issues an entry reports by
  design (`diagnostics`, `gaps`, `appearance` patterns with a `reason`).
- `doc/usd-wg-assets-eye.json`: the by-eye verdicts.
- `doc/usd-compatibility-plan.md` section 6: the gaps deferred as future
  work.

## Triage of a stopped entry

The worker reads the verdict line, the entry's record in
`logs/usd_wg_survey/summary.json` (diagnostics with their example lines,
`meshes` against `composed_meshes`, `bounds_deviation`, `storm_match`),
the asset file and its folder's `README.md`, and the capture beside the
Storm render (`logs/usd_wg_survey/<slug>.png`,
`logs/usd_wg_survey/storm/<slug>.png`) before deciding which of four
outcomes the entry is. Each outcome ends in a commit.

1. An erhe defect: the composed stage, Storm or the README says one thing
   and erhe does another. Fix the cause in `src/erhe/usd`, the editor or
   the renderer, add or extend a gtest under `src/erhe/usd/test/` (fixture
   under `test/data/`), rebuild `erhe_usd_tests` and the headless editor,
   run the tests, and update the owning `notes.md`.
2. A survey defect: the comparison itself is wrong (the pxr leg measured
   something erhe does not frame, a conversion bug, a truncated example).
   Fix the script, run its bookkeeping self-check
   (`check_bookkeeping()` through an `importlib` load, no editor), and
   prove the fix on the entry's record from `summary.json`.
3. By design: the asset authors the issue on purpose, its README says so,
   and Storm renders what erhe renders. Add an item to
   `doc/usd-wg-assets-expected.json` naming the exact diagnostic or gap
   pattern and quoting the README statement and the Storm match in
   `reason`. An expectation is evidence-backed or it is not written.
4. Scoped future work: the gap is one the plan's section 6 already lists
   (MaterialX, 16-bit and CMYK images, Radiance `.hdr`, load performance)
   or one the worker cannot close in one commit. Add the plan bullet when
   it is missing, and either `--exclude` the entry from the loop command
   (a whole family, like MaterialX) or record the diagnostic in the
   expected sidecar with a reason that names the plan bullet and says it
   is a known limitation, not a by-design result.

Measure before deciding: a bounds row is compared against the pxr stage
script's numbers (`scripts/usd_wg_pxr_stage.py`, run under
`<usd_root>/scripts/set_usd_env.bat`), a "renders wrong" row against the
Storm render, a count row against the composed count with inactive and
class-prototype prims removed. A verdict read by eye alone is not a
diagnosis.

## Verification a commit needs

- `erhe_usd_tests` (`build_vs2026_vulkan`, `--config Debug`) green when
  `src/erhe/usd` changed; `erhe_scene_renderer_tests` and
  `erhe_primitive_tests` too when their libraries changed.
- The headless editor rebuilt (`build_vs2026_vulkan_headless`, target
  `editor`) before the survey is rerun; kill a running `editor.exe` first
  (a link against a running binary fails with LNK1168).
- The loop command rerun: the fixed entry reads `works` and the run moves
  on to the next stop.

## The worker

A worker is a fresh `Agent` (`subagent_type: general-purpose`, `model:
opus`) given the brief below. It works the loop on its own: run, triage,
fix, verify, commit, run again. It stops when one of these holds and then
reports:

- its context is about half full - the token counter the harness shows,
  or, failing that, after four entries closed;
- the loop completes with no stop (every entry `works`);
- an entry needs a decision the plan does not make (a new deferral, an
  API change outside `src/erhe/usd`, a LightUSD fork change).

Its report is the harness's coder report plus three lines: the commits
it made (`git log --oneline` range), the entry the loop stopped on last
and what it found about it so far, and the entries it deferred or
excluded. It commits its own work (this loop's commits are small and
verified in the loop itself; the orchestrator reviews after the fact),
pushes nothing, and writes nothing into the memory bank or the queue.

## The orchestrator

The interactive session holds the loop's state and nothing else: which
worker is running, what the last report said, what the queue says. It:

1. Checks that no `editor.exe` is running and that
   `build_vs2026_vulkan_headless/src/editor/Debug/editor.exe` and
   `build_vs2026_vulkan/src/erhe/usd/test/Debug/erhe_usd_tests.exe` exist
   (build them first if not).
2. Launches one worker with the brief, in the background, and waits for
   its report. One worker at a time: they share the build trees and the
   MCP port.
3. On the report: reads `git log --oneline` for the range, reads the diff
   of the one commit that changed the most outside `doc/` and `scripts/`,
   and checks each new expected-results item against its reason (a
   README quote or a Storm match, or it is reverted to a plan bullet).
   Records the state in `memory-bank/activeContext.md` and, when the loop
   is handed to a later session, in `prompt_queue.txt`.
4. Launches the next worker with the same brief while a stop remains and
   the user has not said to stop. The brief does not change between
   workers: the test database and the sidecars carry the state, so a fresh
   worker starts exactly where the previous one stopped.

When `test_assets` runs clean, the scope widens to `full_assets`
(`--only full_assets`, same command otherwise) and then to `intent-vfx`,
and the full run without `--stop-on-gap` regenerates `doc/usd-wg-assets.md`
once every identified gap is closed (`feedback-survey-rerun-policy`).

## The brief

Copy this into the worker's prompt, filling the two paths.

```
Work the usd-wg survey gap loop described in doc/usd-survey-gap-loop.md.
Read that document first, then doc/agent-orchestration-harness.md
("Coder report format"). Paths on this machine: usd-wg assets clone
<usd-wg-assets>, OpenUSD tools <usd_root> (memory-bank/local/context.md).

Repeat: run the loop command with ERHE_AI_DRIVER=1; triage the entry it
stops on (four outcomes, evidence first); fix, verify and commit as the
document says; run again. Commit after every closed entry with a message
that names the asset, the cause and the fix. Push nothing. Do not edit
prompt_queue.txt or memory-bank/.

Stop and report when your context is about half full, when the loop
completes without a stop, or when an entry needs a decision the plan does
not make. The report: files changed, the commits (git log --oneline
range), the entry the loop last stopped on and what you found, the
entries deferred or excluded, and every test and build outcome verbatim.
```
